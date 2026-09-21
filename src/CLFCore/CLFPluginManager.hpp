// CLFPluginManager.hpp — 插件管理器（core 内建，阶段 2 §3.2；管理器头不进
// clf_plugin_api，插件不可见管理器类型）
// 职责：发现（扫描 plugins/*.dll）→ 元数据 → 版本闸门 → 依赖图（服务名）→
// 拓扑加载 → 服务注册表路由 → 生命周期（load/unload/reload）。
// 单插件失败（版本不符 / init 失败 / 缺依赖 / 环）→ 标记禁用或跳过，
// 不阻断其余插件（隔离兜底）。
// 线程模型：loadAll/load/unload/reload 于启动期或 quiesce 边界调用；
// getService/toolProviders/listPluginNames 只读（注册表查询）。
//
// example:
//   CLF::CLFCore::CLFPluginManager manager;   // 缺省 = exe 目录/plugins
//   int loaded = manager.loadAll();
//   auto* provider = static_cast<CLF::CLFPluginApi::ICLFToolProvider*>(
//       manager.getService("tool.provider"));

#pragma once

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "CLFPluginApi/CLFPluginApi.hpp"
#include "CLFPluginApi/CLFToolApi.hpp"

namespace CLF::CLFCore {

class CLFHostApiImpl;

class CLFPluginManager {
public:
    // pluginDir 缺省 = exe 目录/plugins（测试可注入临时目录）
    explicit CLFPluginManager(std::string pluginDir = "");
    // 析构语义（2026-09-21 评审修订）：显式**先卸载全部插件**
    // （shutdown → CLFPluginDestroy → FreeLibrary，函数指针须在卸库前调用）→
    // 再释放 host 与记录。不可依赖成员默认析构逆序（m_hostApi 声明在 m_plugins
    // 之后 → 先析构）：否则插件 shutdown 时 host 已死、DLL 句柄泄漏
    ~CLFPluginManager();

    CLFPluginManager(const CLFPluginManager&)            = delete;
    CLFPluginManager& operator=(const CLFPluginManager&) = delete;  // 持 DLL 句柄，禁拷贝

    // 发现 → 元数据 → 依赖图 → 拓扑加载。
    // 返回成功加载数；单个插件失败（版本不符/init 失败/缺依赖）→ 标记禁用，不阻断其余
    int  loadAll();

    // 重复加载语义（2026-09-21）：load 已加载插件 → false + warn（幂等拒绝，不重载）；
    // 禁用态插件可重试（先清旧记录再装载）；unload 未加载 → false + warn（无操作）
    bool load(const std::string& pluginName);
    bool unload(const std::string& pluginName);   // quiesce 前提由调用方保证
    bool reload(const std::string& pluginName);

    // 仅已加载（enabled）插件
    std::vector<std::string> listPluginNames() const;

    // 插件状态（/plugin list 状态表展示用，2.2c UX 增强 2026-09-21）
    enum class PluginState { Loaded, Unloaded, Disabled };
    struct PluginListEntry {
        std::string name;
        std::string version;
        PluginState state = PluginState::Unloaded;
        std::string error;      // Disabled 时原因（其余空）
    };
    // 全记录列表（含已卸载/已禁用；顺序稳定——unload 保留记录不 erase，
    // 序号索引依据）。Loaded 才有版本；Unloaded/Disabled 版本留空或保留
    std::vector<PluginListEntry> listPluginEntries() const;

    // 按名查状态（幂等提示用）；未找到返回 false、outState 不变
    bool pluginState(const std::string& name, PluginState& outState) const;

    // 服务路由（插件与宿主共用；不存在 → nullptr）；按服务名（2026-09-11 修订）
    CLF::CLFPluginApi::CLFService* getService(const char* service) const;

    // 工具提供者汇总（2.2b 装配用；仅返回已加载且提供 tool.provider 的插件）
    std::vector<CLF::CLFPluginApi::ICLFToolProvider*> toolProviders() const;

    // 宿主 API（传给插件的注入面；由 CLFHostApiImpl 实现）
    CLF::CLFPluginApi::CLFHostApi* hostApi() const;

private:
    struct PluginRecord;
    struct ServiceBinding;

    PluginRecord* findByName(const std::string& name);
    std::unique_ptr<PluginRecord> tryLoadDll(const std::string& dllPathUtf8);
    void insertServices(const PluginRecord& rec);
    void removePluginServices(const std::string& pluginName);
    void destroyRecord(PluginRecord& rec);   // shutdown（如已 init）→ Destroy → FreeLibrary

    std::vector<std::unique_ptr<PluginRecord>> m_plugins;
    std::unique_ptr<CLFHostApiImpl>            m_hostApi;   // 声明在 m_plugins 之后（析构逆序注意，见构造注释）
    std::string                                m_pluginDir;
    // 服务注册表：服务名 → 绑定列表（绑定按提供方插件名字典序维护；
    // 多提供方取首个 = 文件名字典序首个，与 §3.2 依赖解析同一索引同一规则）
    std::map<std::string, std::vector<ServiceBinding>> m_serviceIndex;
};

} // namespace CLF::CLFCore
