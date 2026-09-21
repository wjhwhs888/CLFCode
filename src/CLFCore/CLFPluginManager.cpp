// CLFPluginManager.cpp — 插件管理器实现（阶段 2 §3.2 加载状态机 + §3.3 服务注册表）
// 加载流程（loadAll）：扫描（文件名字典序）→ 逐个装载（Create + 版本闸门）→
// 服务名依赖解析（缺失禁用）→ Kahn 拓扑排序（环上禁用）→ 按序 init → 服务入注册表。
// 单插件失败不阻断其余（隔离兜底）；重复加载语义与析构语义见 hpp 注释。

#include "CLFCore/CLFPluginManager.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <utility>

#include "CLFCore/CLFHostApiImpl.hpp"
#include "CLFCore/CLFLogger.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace CLF::CLFCore {

namespace {

// 动态库句柄与符号访问的跨平台封装（Windows 为主；POSIX dl 兜底保持
// MSVC/GCC 双构建链对称——与 CLF_PLUGIN_EXPORT 宏同一理由）
#ifdef _WIN32
using DllHandle = HMODULE;
DllHandle loadDll(const fs::path& p) {
    // 抑制系统错误弹窗（SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX）：
    // 插件目录可能含损坏/非 PE 文件，LoadLibrary 失败时 Windows 默认弹
    // "损坏的映像"错误框——CLI 程序被系统弹窗卡住不可接受
    // （2026-09-21 用户实测弹窗实抓）。SetErrorMode 按线程继承，加载后立即恢复
    const UINT oldMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    HMODULE h = LoadLibraryW(p.c_str());
    SetErrorMode(oldMode);
    return h;
}
void* getSymbol(DllHandle h, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(h, name));
}
void freeDll(DllHandle h) { FreeLibrary(h); }
#else
using DllHandle = void*;
DllHandle loadDll(const fs::path& p) { return dlopen(p.c_str(), RTLD_NOW | RTLD_LOCAL); }
void* getSymbol(DllHandle h, const char* name) { return dlsym(h, name); }
void freeDll(DllHandle h) { dlclose(h); }
#endif

// 插件加载失败的公共日志前缀
constexpr const char* kLogPrefix = "[Plugin] ";

} // namespace

struct CLFPluginManager::PluginRecord {
    std::string name;                                   // 插件 ID（自述；约定与 DLL 文件名一致）
    std::string version;                                // 插件自述版本（unload 后保留——状态表展示）
    std::string dllPathUtf8;
    DllHandle   handle    = nullptr;
    CLF::CLFPluginApi::CLFPlugin*        plugin    = nullptr;
    CLF::CLFPluginApi::CLFPluginDestroyFn destroyFn = nullptr;
    bool enabled = false;    // init() 已通过（unload/析构时决定是否 shutdown）
    std::string error;       // 禁用原因（Disabled 时有效）
    // 状态表语义（2.2c UX 增强 2026-09-21）：unload 保留记录标记 Unloaded
    // （listPluginNames/toolProviders 只列 enabled 不受影响）
    PluginState state = PluginState::Unloaded;
};

struct CLFPluginManager::ServiceBinding {
    std::string pluginName;                       // 提供方插件 ID
    CLF::CLFPluginApi::CLFService* instance;
};

CLFPluginManager::CLFPluginManager(std::string pluginDir)
    : m_pluginDir(std::move(pluginDir)) {
    if (m_pluginDir.empty()) {
        // 缺省 = exe 目录/plugins（CLFConfigLoader GetModuleFileNameW 先例：
        // W 版本 + u8string——A 版本按 ANSI 代码页读路径，exe 位于中文目录时乱码）
#ifdef _WIN32
        wchar_t wbuf[MAX_PATH];
        DWORD len = GetModuleFileNameW(nullptr, wbuf, MAX_PATH);
        if (len > 0 && len < MAX_PATH) {
            m_pluginDir = (fs::path(wbuf).parent_path() / "plugins").u8string();
        }
#else
        char buf[PATH_MAX];
        ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len > 0) {
            buf[len] = '\0';
            m_pluginDir = (fs::path(buf).parent_path() / "plugins").string();
        }
#endif
    }
    m_hostApi = std::make_unique<CLFHostApiImpl>(this);
}

CLFPluginManager::~CLFPluginManager() {
    // 析构语义（2026-09-21）：显式先卸载全部插件——shutdown → Destroy → FreeLibrary
    // （函数指针指向 DLL 代码段，先卸库再调用 = 立即崩溃）。析构函数体执行期间
    // m_hostApi 成员仍存活（成员析构在函数体之后），插件 shutdown 内可安全访问 host
    for (auto& rec : m_plugins) {
        destroyRecord(*rec);
    }
    m_plugins.clear();
    m_serviceIndex.clear();
}

CLFPluginManager::PluginRecord* CLFPluginManager::findByName(const std::string& name) {
    auto it = std::find_if(m_plugins.begin(), m_plugins.end(),
                           [&](const auto& r) { return r->name == name; });
    return it == m_plugins.end() ? nullptr : it->get();
}

// 装载单个 DLL：LoadLibrary → 工厂符号 → 创建实例 → 版本闸门。
// 装载失败返回 nullptr（资源已按失败阶段清理，warn 日志已留）。
// 版本闸门固有次序风险（先有鸡还是先有蛋）：版本检查发生在 Create 之后
// （必须先构造实例才能问版本）——闸门与 init 之间不调用插件任何其他方法，
// 把风险面收窄到"构造"一步；此风险如实记录，不假装闸门能防住一切
std::unique_ptr<CLFPluginManager::PluginRecord>
CLFPluginManager::tryLoadDll(const std::string& dllPathUtf8) {
    const fs::path dllPath = fs::u8path(dllPathUtf8);
    DllHandle handle = loadDll(dllPath);
    if (!handle) {
        CLFLogger::instance().warn(std::string(kLogPrefix) + "skip '" + dllPathUtf8 +
                                   "': LoadLibrary failed");
        return nullptr;
    }
    auto createFn = reinterpret_cast<CLF::CLFPluginApi::CLFPluginCreateFn>(
        getSymbol(handle, "CLFPluginCreate"));
    auto destroyFn = reinterpret_cast<CLF::CLFPluginApi::CLFPluginDestroyFn>(
        getSymbol(handle, "CLFPluginDestroy"));
    if (!createFn || !destroyFn) {
        freeDll(handle);
        CLFLogger::instance().warn(std::string(kLogPrefix) + "skip '" + dllPathUtf8 +
                                   "': not a plugin DLL (missing factory symbols)");
        return nullptr;
    }
    CLF::CLFPluginApi::CLFPlugin* plugin = createFn(m_hostApi.get());
    if (!plugin) {
        freeDll(handle);
        CLFLogger::instance().warn(std::string(kLogPrefix) + "skip '" + dllPathUtf8 +
                                   "': CLFPluginCreate returned null");
        return nullptr;
    }

    auto rec = std::make_unique<PluginRecord>();
    rec->dllPathUtf8 = dllPathUtf8;
    rec->handle      = handle;
    rec->plugin      = plugin;
    rec->destroyFn   = destroyFn;
    rec->name        = plugin->name();

    // 版本闸门：精确匹配（§1.5 取舍）——不匹配拒绝加载 + 提示重建，不尝试兼容
    if (plugin->hostApiVersion() != m_hostApi->apiVersion()) {
        rec->error = "ABI version mismatch (plugin=" + std::to_string(plugin->hostApiVersion()) +
                     ", host=" + std::to_string(m_hostApi->apiVersion()) + "), rebuild required";
        CLFLogger::instance().warn(std::string(kLogPrefix) + "'" + rec->name +
                                   "' rejected: " + rec->error);
        destroyFn(plugin);
        freeDll(handle);
        return nullptr;   // 拒绝加载：不入记录（与缺符号 DLL 同待遇）
    }
    return rec;
}

// 卸载语义：shutdown（仅 init 成功后）→ CLFPluginDestroy → FreeLibrary。
// 顺序不可变：Destroy/FreeLibrary 顺序颠倒 = 函数指针指向已卸载代码段
void CLFPluginManager::destroyRecord(PluginRecord& rec) {
    if (rec.plugin) {
        if (rec.enabled) {
            rec.plugin->shutdown();
        }
        if (rec.destroyFn) {
            rec.destroyFn(rec.plugin);
        }
    }
    if (rec.handle) {
        freeDll(rec.handle);
    }
    rec.plugin    = nullptr;
    rec.destroyFn = nullptr;
    rec.handle    = nullptr;
}

// 服务入注册表：绑定列表按提供方插件名字典序维护（多提供方取首个 = 文件名字典序
// 首个，与依赖解析同一索引同一规则——§3.3）
void CLFPluginManager::insertServices(const PluginRecord& rec) {
    for (const CLF::CLFPluginApi::CLFServiceEntry* e = rec.plugin->services();
         e && e->name && e->instance; ++e) {
        auto& bindings = m_serviceIndex[e->name];
        ServiceBinding binding{rec.name, e->instance};
        auto pos = std::lower_bound(bindings.begin(), bindings.end(), binding,
                                    [](const ServiceBinding& a, const ServiceBinding& b) {
                                        return a.pluginName < b.pluginName;
                                    });
        bindings.insert(pos, std::move(binding));
    }
}

void CLFPluginManager::removePluginServices(const std::string& pluginName) {
    for (auto it = m_serviceIndex.begin(); it != m_serviceIndex.end();) {
        auto& bindings = it->second;
        bindings.erase(std::remove_if(bindings.begin(), bindings.end(),
                                      [&](const ServiceBinding& b) {
                                          return b.pluginName == pluginName;
                                      }),
                       bindings.end());
        it = bindings.empty() ? m_serviceIndex.erase(it) : std::next(it);
    }
}

int CLFPluginManager::loadAll() {
    // 1. 扫描 m_pluginDir 下 *.dll——按文件名字典序排序后处理：
    //    directory_iterator 枚举顺序无保证（跨机器/复制后可变），排序是
    //    "多提供方取首个"与日志可复现的前提（无目录 → 返回 0，静默降级）
    const fs::path dir = fs::u8path(m_pluginDir);
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        CLFLogger::instance().info(std::string(kLogPrefix) +
                                   "plugin dir not found, no plugins loaded");
        return 0;
    }
    std::vector<std::string> dlls;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && it->path().extension() == ".dll") {
            dlls.push_back(it->path().u8string());
        }
    }
    std::sort(dlls.begin(), dlls.end());

    // 2. 逐个装载（跳过已存在同名——重复加载语义：enabled 与 disabled 记录都跳过，
    //    重试由 load() 显式承担）
    std::vector<std::unique_ptr<PluginRecord>> pending;
    for (const auto& dllPath : dlls) {
        const std::string stem = fs::u8path(dllPath).stem().u8string();
        bool nameTaken = findByName(stem) != nullptr;
        for (const auto& p : pending) {
            nameTaken = nameTaken || p->name == stem;
        }
        if (nameTaken) {
            continue;
        }
        auto rec = tryLoadDll(dllPath);
        if (!rec) {
            continue;
        }
        // Create 后以自述 name() 再查一次（防御：文件名与自述 ID 不一致）
        bool dup = findByName(rec->name) != nullptr;
        for (const auto& p : pending) {
            dup = dup || p->name == rec->name;
        }
        if (dup) {
            CLFLogger::instance().warn(std::string(kLogPrefix) + "skip '" + dllPath +
                                       "': duplicate plugin id '" + rec->name + "'");
            destroyRecord(*rec);
            continue;
        }
        pending.push_back(std::move(rec));
    }
    if (pending.empty()) {
        return 0;
    }

    // 3. 依赖解析：requiresServices() 服务名 → 提供方解析（已加载注册表优先，
    //    pending 内取插件名字典序首个——与 §3.2 多提供方歧义规则同索引同规则）
    std::map<std::string, std::set<std::string>> pendingProvides;   // svc → 提供方名集合
    for (const auto& p : pending) {
        for (const CLF::CLFPluginApi::CLFServiceEntry* e = p->plugin->services();
             e && e->name; ++e) {
            pendingProvides[e->name].insert(p->name);
        }
    }
    auto resolveProvider = [&](const std::string& svc) -> const std::string* {
        // 已加载提供方优先（注册表绑定字典序首个）
        auto it = m_serviceIndex.find(svc);
        if (it != m_serviceIndex.end() && !it->second.empty()) {
            return &it->second.front().pluginName;
        }
        auto pit = pendingProvides.find(svc);
        if (pit != pendingProvides.end() && !pit->second.empty()) {
            return &*pit->second.begin();   // set 字典序最小 = 文件名排序后首个
        }
        return nullptr;
    };

    std::map<std::string, int> inDegree;                              // pending 名 → 未满足的 pending 内依赖数
    std::map<std::string, std::vector<std::string>> dependents;       // 提供方 → 依赖者
    std::set<std::string> disabledByDependency;                       // 依赖缺失/环
    for (const auto& p : pending) {
        for (const char* const* req = p->plugin->requiresServices(); req && *req; ++req) {
            const std::string svc = *req;
            if (pendingProvides.count(svc) == 0 && m_serviceIndex.count(svc) == 0) {
                p->error = "missing dependency service '" + svc + "'";
                disabledByDependency.insert(p->name);
                break;
            }
            const std::string* provider = resolveProvider(svc);
            // 已满足（提供方已加载或 pending 内）——pending 内提供方构成拓扑边；
            // requires 表重复同一服务名时去重（否则 Kahn 重复入队 → init 调两次）
            if (provider && pendingProvides.count(svc) > 0 &&
                std::any_of(pending.begin(), pending.end(),
                            [&](const auto& q) { return q->name == *provider; })) {
                auto& deps = dependents[*provider];
                if (std::find(deps.begin(), deps.end(), p->name) == deps.end()) {
                    inDegree[p->name]++;
                    deps.push_back(p->name);
                }
            }
        }
    }
    // 依赖缺失者已禁用；其提供方若依赖它（环）在 Kahn 剩余集中一并处理

    // Kahn 拓扑排序（pending 字典序稳定输出）：剩余节点 = 环上或依赖环上 → 全部禁用
    std::vector<std::string> topoOrder;
    std::vector<std::string> ready;
    for (const auto& p : pending) {
        if (inDegree[p->name] == 0 && !disabledByDependency.count(p->name)) {
            ready.push_back(p->name);
        }
    }
    std::sort(ready.begin(), ready.end());   // 同层按名字典序（init 序可复现）
    while (!ready.empty()) {
        std::string name = ready.front();
        ready.erase(ready.begin());
        topoOrder.push_back(name);
        for (const auto& dep : dependents[name]) {
            if (--inDegree[dep] == 0 && !disabledByDependency.count(dep)) {
                ready.push_back(dep);
                std::sort(ready.begin(), ready.end());
            }
        }
    }
    for (auto& [name, deg] : inDegree) {
        if (deg > 0 && !disabledByDependency.count(name)) {
            // 区分两类失败：requires 指向已被禁用的提供方（提供方缺依赖/环）
            // vs 纯依赖环——原因文案可读（P15 断言）
            bool dependsDisabled = false;
            auto it = std::find_if(pending.begin(), pending.end(),
                                   [&](const auto& p) { return p->name == name; });
            if (it != pending.end()) {
                for (const char* const* req = (*it)->plugin->requiresServices(); req && *req; ++req) {
                    const std::string* provider = resolveProvider(*req);
                    if (provider && disabledByDependency.count(*provider)) {
                        dependsDisabled = true;
                        break;
                    }
                }
                (*it)->error = dependsDisabled
                                   ? "depends on a disabled plugin"
                                   : "dependency cycle (or depends on a cycle member)";
                disabledByDependency.insert(name);
            }
        }
    }

    // 4. 按拓扑序 init：失败 → 标记禁用（其余继续）
    // 注意：move 后必须从 pending erase——下一轮 find_if 遍历 pending 时
    // 空 unique_ptr 解引用 = 段错误（2026-09-21 qa P14 实抓）
    int loaded = 0;
    for (const auto& name : topoOrder) {
        auto it = std::find_if(pending.begin(), pending.end(),
                               [&](const auto& p) { return p && p->name == name; });
        if (it == pending.end() || disabledByDependency.count(name)) {
            continue;
        }
        auto rec = std::move(*it);   // 取出所有权
        pending.erase(it);           // 从 pending 移除（防后续遍历踩空）
        if (!rec->plugin->init()) {
            rec->enabled = false;
            rec->state   = PluginState::Disabled;
            rec->error   = "init() returned false";
            CLFLogger::instance().warn(std::string(kLogPrefix) + "'" + rec->name +
                                       "' disabled: " + rec->error);
            m_plugins.push_back(std::move(rec));   // 保留禁用记录（load 可重试）
            continue;
        }
        rec->enabled = true;
        rec->state   = PluginState::Loaded;
        rec->version = rec->plugin->version();
        ++loaded;
        CLFLogger::instance().info(std::string(kLogPrefix) + "'" + rec->name +
                                   "' loaded (v" + rec->plugin->version() + ")");
        insertServices(*rec);   // 成功即注册：拓扑序保证依赖方 init 时提供方服务已可用
        m_plugins.push_back(std::move(rec));   // 成功记录入表（漏此则记录随 pending 析构）
    }

    // 依赖缺失/环上插件：销毁实例（shutdown 不调——init 从未成功），仅留禁用原因日志
    for (auto& rec : pending) {
        if (rec && disabledByDependency.count(rec->name)) {
            CLFLogger::instance().warn(std::string(kLogPrefix) + "'" + rec->name +
                                       "' disabled: " + rec->error);
            destroyRecord(*rec);
        }
    }

    // （原第 5 步"成功插件批量入注册表"已并入 init 循环——成功即注册，
    // 拓扑序保证依赖方 init 时提供方服务已可用；2026-09-21 qa P18 实抓）

    // 多提供方歧义 warn（§3.2：requires 解析/查询取文件名字典序首个，
    // 多提供方记录 warn 供诊断——P14 断言）
    for (const auto& [svc, bindings] : m_serviceIndex) {
        if (bindings.size() > 1) {
            CLFLogger::instance().warn(std::string(kLogPrefix) + "service '" + svc +
                                       "' provided by multiple plugins (" +
                                       std::to_string(bindings.size()) + "); using first '" +
                                       bindings.front().pluginName + "'");
        }
    }
    return loaded;
}

bool CLFPluginManager::load(const std::string& pluginName) {
    auto* existing = findByName(pluginName);
    if (existing && existing->enabled) {
        CLFLogger::instance().warn(std::string(kLogPrefix) + "load '" + pluginName +
                                   "' ignored: already loaded");
        return false;   // 幂等拒绝（重复加载语义 2026-09-21）
    }
    if (existing) {
        // 禁用态可重试：先清旧记录
        removePluginServices(pluginName);
        destroyRecord(*existing);
        m_plugins.erase(std::remove_if(m_plugins.begin(), m_plugins.end(),
                                       [&](const auto& r) { return r->name == pluginName; }),
                        m_plugins.end());
    }

    const fs::path dllPath = fs::u8path(m_pluginDir) / (pluginName + ".dll");
    std::error_code ec;
    if (!fs::exists(dllPath, ec)) {
        CLFLogger::instance().warn(std::string(kLogPrefix) + "load '" + pluginName +
                                   "' failed: DLL not found");
        return false;
    }
    auto rec = tryLoadDll(dllPath.u8string());
    if (!rec) {
        return false;
    }
    if (rec->name != pluginName) {
        CLFLogger::instance().warn(std::string(kLogPrefix) + "load '" + pluginName +
                                   "' failed: plugin self-reports id '" + rec->name + "'");
        destroyRecord(*rec);
        return false;
    }

    // 依赖检查（单点加载：提供方须已加载；pending 不存在于此路径）
    for (const char* const* req = rec->plugin->requiresServices(); req && *req; ++req) {
        if (!getService(*req)) {
            rec->error = std::string("missing dependency service '") + *req + "'";
            CLFLogger::instance().warn(std::string(kLogPrefix) + "'" + rec->name +
                                       "' disabled: " + rec->error);
            destroyRecord(*rec);
            return false;
        }
    }
    if (!rec->plugin->init()) {
        rec->enabled = false;
        rec->state   = PluginState::Disabled;
        rec->error   = "init() returned false";
        CLFLogger::instance().warn(std::string(kLogPrefix) + "'" + rec->name +
                                   "' disabled: " + rec->error);
        m_plugins.push_back(std::move(rec));   // 保留禁用记录（可重试）
        return false;
    }
    rec->enabled = true;
    rec->state   = PluginState::Loaded;
    rec->version = rec->plugin->version();
    insertServices(*rec);
    CLFLogger::instance().info(std::string(kLogPrefix) + "'" + rec->name +
                               "' loaded (v" + rec->plugin->version() + ")");
    m_plugins.push_back(std::move(rec));
    return true;
}

bool CLFPluginManager::unload(const std::string& pluginName) {
    auto it = std::find_if(m_plugins.begin(), m_plugins.end(),
                           [&](const auto& r) { return r->name == pluginName; });
    if (it == m_plugins.end() || !(*it)->enabled) {
        CLFLogger::instance().warn(std::string(kLogPrefix) + "unload '" + pluginName +
                                   "' ignored: not loaded");
        return false;   // 未加载 → 无操作
    }
    removePluginServices(pluginName);
    destroyRecord(**it);   // shutdown → Destroy → FreeLibrary
    (*it)->enabled = false;
    (*it)->state   = PluginState::Unloaded;   // 保留记录（状态表语义——序号索引稳定）
    CLFLogger::instance().info(std::string(kLogPrefix) + "'" + pluginName + "' unloaded");
    return true;
}

bool CLFPluginManager::reload(const std::string& pluginName) {
    // reload = unload + load；新插件失败 → 旧插件已卸载、服务已消失，调用方自兜底。
    // 不做"失败回滚旧实例"——回滚需重载 DLL 且掩盖根本原因（§3.3）
    unload(pluginName);
    return load(pluginName);
}

std::vector<std::string> CLFPluginManager::listPluginNames() const {
    std::vector<std::string> names;
    for (const auto& rec : m_plugins) {
        if (rec->enabled) {
            names.push_back(rec->name);
        }
    }
    return names;
}

std::vector<std::pair<std::string, std::string>> CLFPluginManager::listPlugins() const {
    std::vector<std::pair<std::string, std::string>> result;
    for (const auto& rec : m_plugins) {
        if (rec->enabled && rec->plugin) {
            result.emplace_back(rec->name, rec->plugin->version());
        }
    }
    return result;
}

std::vector<CLFPluginManager::PluginListEntry> CLFPluginManager::listPluginEntries() const {
    std::vector<PluginListEntry> result;
    result.reserve(m_plugins.size());
    for (const auto& rec : m_plugins) {
        PluginListEntry e;
        e.name    = rec->name;
        e.version = rec->version;
        e.state   = rec->state;
        e.error   = rec->error;
        result.push_back(std::move(e));
    }
    return result;
}

bool CLFPluginManager::pluginState(const std::string& name, PluginState& outState) const {
    auto it = std::find_if(m_plugins.begin(), m_plugins.end(),
                           [&](const auto& r) { return r->name == name; });
    if (it == m_plugins.end()) {
        return false;
    }
    outState = (*it)->state;
    return true;
}

CLF::CLFPluginApi::CLFService* CLFPluginManager::getService(const char* service) const {
    auto it = m_serviceIndex.find(service);
    if (it == m_serviceIndex.end() || it->second.empty()) {
        return nullptr;   // 未命中 → 调用方自兜底
    }
    return it->second.front().instance;   // 多提供方取文件名字典序首个（§3.3）
}

std::vector<CLF::CLFPluginApi::ICLFToolProvider*> CLFPluginManager::toolProviders() const {
    std::vector<CLF::CLFPluginApi::ICLFToolProvider*> result;
    for (const auto& rec : m_plugins) {
        if (!rec->enabled || !rec->plugin) {
            continue;
        }
        for (const CLF::CLFPluginApi::CLFServiceEntry* e = rec->plugin->services();
             e && e->name; ++e) {
            if (std::string(e->name) == "tool.provider") {
                result.push_back(static_cast<CLF::CLFPluginApi::ICLFToolProvider*>(e->instance));
            }
        }
    }
    return result;
}

CLF::CLFPluginApi::CLFHostApi* CLFPluginManager::hostApi() const {
    return m_hostApi.get();
}

} // namespace CLF::CLFCore
