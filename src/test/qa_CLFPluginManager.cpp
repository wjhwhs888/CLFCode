// qa_CLFPluginManager.cpp — 插件管理器测试（2.1 §六 测试计划，P1-P19）
// 管理器加载真 DLL（不做 LoadLibrary mock——那等于测 mock）；每用例独立临时
// 插件目录（从 CLF_TEST_PLUGIN_DIR 复制所需变体 DLL），保证用例间隔离。
// 日志断言：CLFLogger 单例重定向到临时文件（init 后即时 flush），
// "禁用原因可读"类断言读文件找子串。
//
// 变体插件清单（src/test/plugins/，§4.2）：
//   主 clf.teststub（tool.provider + test_echo + shutdown 锚点）
//   badinit / badabi / multisvc / loopA+loopB / svc1+svc2 / depA+depB / nosym

#include <boost/ut.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "CLFCore/CLFLogger.hpp"
#include "CLFCore/CLFPluginManager.hpp"
#include "plugins/TestPluginCommon.hpp"

using namespace boost::ut;
namespace fs = std::filesystem;

using CLF::CLFCore::CLFLogger;
using CLF::CLFCore::CLFLogLevel;
using CLF::CLFCore::CLFPluginManager;
using CLF::CLFPluginApi::CLF_PLUGIN_API_VERSION;
using CLF::CLFPluginApi::CLFToolCallbacks;
using CLF::CLFPluginApi::ICLFToolProvider;
using CLF::CLFPluginApi::LogCodeDebug;    // 非作用域枚举值须逐值 using
using CLF::CLFPluginApi::LogCodeInfo;
using CLF::CLFPluginApi::LogCodeWarn;
using CLF::CLFPluginApi::LogCodeError;

namespace {

// path → UTF-8 std::string：C++17 下 u8string() 直接返回 std::string；
// C++20 下返回 std::u8string（char8_t）——reinterpret 归一（qa 为 C++20）
std::string pathToUtf8(const fs::path& p) {
    auto s = p.u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

// 每用例独立插件目录：临时目录 + 从 CLF_TEST_PLUGIN_DIR 复制所需变体 DLL。
// 目录名含纳秒时间戳 + 原子计数，避免并行测试冲突
std::atomic<int> g_dirCounter{0};
std::string makePluginDir(std::initializer_list<const char*> dllNames) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path dir = fs::temp_directory_path() /
                   ("clf_plugintest_" + std::to_string(stamp) + "_" +
                    std::to_string(g_dirCounter.fetch_add(1)));
    fs::create_directories(dir);
    for (const char* name : dllNames) {
        fs::copy_file(fs::path(CLF_TEST_PLUGIN_DIR) / name, dir / name);
    }
    return pathToUtf8(dir);
}

void cleanupDir(const std::string& dirUtf8) {
    std::error_code ec;
    fs::remove_all(fs::u8path(dirUtf8), ec);   // manager 已析构（DLL 已卸载）后调用
}

std::string readFile(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// 日志文件（suite 开头 init 重定向；用例断言读此文件找子串）
const fs::path kLogPath = [] {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() / ("clf_plugintest_log_" + std::to_string(stamp) + ".log");
}();
std::string logContents() { return readFile(kLogPath); }

// 从日志文件取某次断言区间的内容：返回距文件尾 n 字符（避免前序用例日志干扰）
std::string logTail(size_t n) {
    std::string all = logContents();
    return all.size() > n ? all.substr(all.size() - n) : all;
}

const char* describeService(CLF::CLFPluginApi::CLFService* svc) {
    return static_cast<clftest::ITestSecondService*>(svc)->describe();
}

} // namespace

const boost::ut::suite<"CLFPluginManager"> tests = [] {
    // 日志重定向到临时文件（断言"禁用原因可读"用；Debug 级保证 debug 日志也落盘）
    CLFLogger::instance().init(CLFLogLevel::Debug, pathToUtf8(kLogPath), false);

    // ========== P1 空目录 ==========

    "P1 空目录"_test = [] {
        std::string dir = makePluginDir({});
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 0_i);
            expect(mgr.listPluginNames().empty());
            // 补 2026-09-21：从未存在的服务名 → nullptr（调用方自兜底链路的 2.1 形态）
            expect(mgr.getService("nonexistent") == nullptr);
            expect(mgr.toolProviders().empty());
        }
        cleanupDir(dir);
    };

    // ========== P2 发现与加载 ==========

    "P2 发现与加载"_test = [] {
        std::string dir = makePluginDir({"clf.teststub.dll"});
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 1_i);
            const auto names = mgr.listPluginNames();
            expect(names.size() == 1_u);
            expect(names[0] == "clf.teststub");
            // listPlugins（2.2c /plugin list 展示用）：名字 + 版本对
            const auto plugins = mgr.listPlugins();
            expect(plugins.size() == 1_u);
            if (!plugins.empty()) {
                expect(plugins[0].first == "clf.teststub");
                expect(plugins[0].second == "1.0.0");
            }
        }
        cleanupDir(dir);
    };

    // ========== P3 元数据 ==========

    "P3 元数据"_test = [] {
        std::string dir = makePluginDir({"clf.teststub.dll"});
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 1_i);
            auto providers = mgr.toolProviders();
            expect(providers.size() == 1_u);
            auto* provider = providers[0];
            expect(provider->toolCount() == 1_i);
            const auto* meta = provider->toolMeta(0);
            expect(meta != nullptr);
            if (meta) {
                expect(std::string(meta->name) == "test_echo");
                expect(meta->risk == 0_i);                              // CLFToolRisk::Read
                expect(meta->flags == 0_i);                             // ToolFlagNone
                expect(meta->concludesTurn == 0_i);
            }
            expect(provider->toolMeta(1) == nullptr);                   // 越界 → nullptr
        }
        cleanupDir(dir);
    };

    // ========== P4 服务路由 ==========

    "P4 服务路由"_test = [] {
        std::string dir = makePluginDir({"clf.teststub.dll"});
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 1_i);
            auto* svc = mgr.getService("tool.provider");
            expect(svc != nullptr);
            if (svc) {
                auto* provider = static_cast<ICLFToolProvider*>(svc);   // 单继承链 downcast
                expect(provider->toolCount() == 1_i);                   // downcast 后可用
            }
        }
        cleanupDir(dir);
    };

    // ========== P5 callTool 往返 + 回调判空义务 ==========

    "P5 callTool 往返"_test = [] {
        std::string dir = makePluginDir({"clf.teststub.dll"});
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 1_i);
            auto* provider = static_cast<ICLFToolProvider*>(mgr.getService("tool.provider"));
            expect(provider != nullptr);
            if (!provider) {
                return;
            }

            std::string content;
            CLFToolCallbacks cb{};
            cb.onResult = [](void* ctx, const char* s, size_t n) {
                static_cast<std::string*>(ctx)->assign(s, n);
            };
            expect(provider->callTool("test_echo", "{\"x\":1}", &content, &cb));
            expect(content == "{\"x\":1}");

            // 补 2026-09-21：回调判空义务（§1.3 实现侧调用前判空）
            expect(provider->callTool("test_echo", "{}", &content, nullptr));    // cb 整体 null
            CLFToolCallbacks nullCb{};                                           // 全 null 函数指针
            expect(provider->callTool("test_echo", "{}", &content, &nullCb));
            expect(content == "{\"x\":1}");    // 判空跳过回调 → 结果无处可送，content 保持旧值
        }
        cleanupDir(dir);
    };

    // ========== P6 卸载 ==========

    "P6 卸载"_test = [] {
        std::string dir = makePluginDir({"clf.teststub.dll"});
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 1_i);
            expect(mgr.unload("clf.teststub"));
            expect(mgr.getService("tool.provider") == nullptr);
            expect(mgr.listPluginNames().empty());
        }
        cleanupDir(dir);
    };

    // ========== P7 热替换 ==========

    "P7 热替换"_test = [] {
        std::string dir = makePluginDir({"clf.teststub.dll"});
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 1_i);
            auto* before = mgr.getService("tool.provider");
            expect(before != nullptr);
            expect(mgr.reload("clf.teststub"));
            auto* after = mgr.getService("tool.provider");
            expect(after != nullptr);
            // 注：不断言 after != before——Windows 对刚卸载的同路径 DLL 重载
            // 常映射相同地址、CRT 堆可能复用同一块，实例地址变化不保证；
            // 热替换语义 = 卸载旧 + 加载新 + 路由可用（地址变化是实现细节）
            if (after) {
                auto* provider = static_cast<ICLFToolProvider*>(after);
                expect(provider->toolCount() == 1_i);   // 新实例可用
                std::string content;
                CLFToolCallbacks cb{};
                cb.onResult = [](void* ctx, const char* s, size_t n) {
                    static_cast<std::string*>(ctx)->assign(s, n);
                };
                expect(provider->callTool("test_echo", "reloaded", &content, &cb));
                expect(content == "reloaded");          // callTool 往返通
            }
        }
        cleanupDir(dir);
    };

    // ========== P8 ABI 版本不符 ==========

    "P8 ABI 版本不符"_test = [] {
        std::string dir = makePluginDir({"clf.teststub.dll", "clf.teststub.badabi.dll"});
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 1_i);           // 仅主 stub 成功
            const auto names = mgr.listPluginNames();
            expect(names.size() == 1_u);
            bool hasBad = std::find(names.begin(), names.end(), "clf.teststub.badabi") != names.end();
            expect(!hasBad);                        // 变体被拒
            // 禁用原因可读（日志）
            expect(logTail(2000).find("ABI version mismatch") != std::string::npos);
            expect(logTail(2000).find("rebuild required") != std::string::npos);
        }
        cleanupDir(dir);
    };

    // ========== P9 init 失败隔离 ==========

    "P9 init 失败隔离"_test = [] {
        std::string dir = makePluginDir({"clf.teststub.dll", "clf.teststub.badinit.dll"});
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 1_i);           // 主 stub 正常
            const auto names = mgr.listPluginNames();
            bool hasBad = std::find(names.begin(), names.end(), "clf.teststub.badinit") != names.end();
            expect(!hasBad);                        // init 失败 → 禁用
            expect(logTail(2000).find("init() returned false") != std::string::npos);
            // 第二次 loadAll：禁用记录跳过（不重复 init）
            expect(mgr.loadAll() == 0_i);
        }
        cleanupDir(dir);
    };

    // ========== P10 非插件 DLL ==========

    "P10a 缺符号 DLL"_test = [] {
        std::string dir = makePluginDir({"clf.teststub.nosym.dll"});
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 0_i);           // 合法 DLL 无工厂导出 → 跳过
            expect(mgr.listPluginNames().empty());
            expect(logTail(2000).find("missing factory symbols") != std::string::npos);
        }
        cleanupDir(dir);
    };

    "P10b 坏 PE 文件"_test = [] {
        std::string dir = makePluginDir({});        // 空目录
        {
            // 垃圾字节 .dll（测试运行时生成、用后删除）——LoadLibraryW 失败路径
            std::ofstream bad(fs::u8path(dir) / "clf.bad.dll", std::ios::binary);
            bad << "this is not a PE file";
            bad.close();

            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 0_i);           // 跳过不崩
            expect(mgr.listPluginNames().empty());
            expect(logTail(2000).find("LoadLibrary failed") != std::string::npos);
        }
        cleanupDir(dir);
    };

    // ========== P11 依赖缺失 ==========

    "P11 依赖缺失"_test = [] {
        std::string dir = makePluginDir({"clf.teststub.loopA.dll"});   // requires "svc.loopB"（不存在）
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 0_i);
            expect(mgr.listPluginNames().empty());
            expect(logTail(2000).find("missing dependency service") != std::string::npos);
        }
        cleanupDir(dir);
    };

    // ========== P12 宿主 API ==========

    "P12 宿主 API"_test = [] {
        std::string dir = makePluginDir({"clf.teststub.dll"});
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 1_i);
            auto* host = mgr.hostApi();
            expect(host != nullptr);
            if (!host) {
                return;
            }
            expect(host->apiVersion() == CLF_PLUGIN_API_VERSION);
            // 四级别日志不崩（Debug 级 init → 全部落盘）
            host->log(LogCodeDebug, "P12 host log debug");
            host->log(LogCodeInfo,  "P12 host log info");
            host->log(LogCodeWarn,  "P12 host log warn");
            host->log(LogCodeError, "P12 host log error");
            expect(logTail(500).find("P12 host log debug") != std::string::npos);
            expect(logTail(500).find("P12 host log error") != std::string::npos);
            // config() 2.1 骨架 = nullptr（2.2a 接真实现）
            expect(host->config("any", "key") == nullptr);
            // alloc/free 所有权转移通道往返
            void* p = host->alloc(16);
            expect(p != nullptr);
            host->free(p);
            // 服务查询经 host 转发
            expect(host->getService("tool.provider") != nullptr);
        }
        cleanupDir(dir);
    };

    // ========== P13 多服务插件（多继承 CLFService 子对象约束） ==========

    "P13 多服务插件"_test = [] {
        std::string dir = makePluginDir({"clf.teststub.multisvc.dll"});
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 1_i);
            auto* toolSvc = mgr.getService("tool.provider");
            expect(toolSvc != nullptr);
            if (toolSvc) {
                expect(static_cast<ICLFToolProvider*>(toolSvc)->toolCount() == 1_i);
            }
            auto* second = mgr.getService("test.second");
            expect(second != nullptr);
            if (second) {
                // 服务表项须指向对应接口视角的子对象——downcast 结果正确
                expect(std::string(describeService(second)) == "second service ok");
            }
        }
        cleanupDir(dir);
    };

    // ========== P14 多提供方歧义 ==========

    "P14 多提供方歧义"_test = [] {
        std::string dir = makePluginDir({"clf.teststub.svc1.dll", "clf.teststub.svc2.dll"});
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 2_i);
            auto* svc = mgr.getService("svc.dup");
            expect(svc != nullptr);
            if (svc) {
                // 取文件名字典序首个（clf.teststub.svc1 < clf.teststub.svc2）
                expect(std::string(describeService(svc)) == "svc1");
            }
            expect(logTail(2000).find("multiple plugins") != std::string::npos);   // warn 记录
        }
        cleanupDir(dir);
    };

    // ========== P15 依赖环 ==========

    "P15 依赖环"_test = [] {
        std::string dir = makePluginDir({"clf.teststub.loopA.dll", "clf.teststub.loopB.dll"});
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 0_i);           // 环上全部禁用
            expect(mgr.listPluginNames().empty());
            expect(logTail(2000).find("dependency cycle") != std::string::npos);
        }
        cleanupDir(dir);
    };

    // ========== P16 reload 失败语义 ==========

    "P16 reload 失败"_test = [] {
        std::string dir = makePluginDir({"clf.teststub.dll"});
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 1_i);
            expect(mgr.unload("clf.teststub"));
            // 模拟升级为不可用版本：删除 DLL（加载中文件被锁定，先卸载才能删）
            std::error_code ec;
            fs::remove(fs::u8path(dir) / "clf.teststub.dll", ec);
            expect(!ec);
            expect(!mgr.reload("clf.teststub"));    // 新插件失败 → 旧已卸载、服务已消失
            expect(mgr.getService("tool.provider") == nullptr);   // 调用方自兜底
            expect(mgr.listPluginNames().empty());
        }
        cleanupDir(dir);
    };

    // ========== P17 析构自动卸载（析构顺序检测锚点） ==========

    "P17 析构自动卸载"_test = [] {
        std::string dir = makePluginDir({"clf.teststub.dll"});
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 1_i);
        }   // 析构 → shutdown（调 host->apiVersion + log）→ Destroy → FreeLibrary
        // shutdown 确被调用（析构顺序错——host 先死——则当场崩，测试直接失败）
        expect(logTail(1000).find("clf.teststub shutdown") != std::string::npos);
        cleanupDir(dir);
    };

    // ========== P18 依赖满足正向拓扑 ==========

    "P18 依赖满足正向拓扑"_test = [] {
        std::string dir = makePluginDir({"clf.teststub.depA.dll", "clf.teststub.depB.dll"});
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 2_i);           // depB（提供方）先 init，depA 后 init
            const auto names = mgr.listPluginNames();
            expect(std::find(names.begin(), names.end(), "clf.teststub.depA") != names.end());
            expect(std::find(names.begin(), names.end(), "clf.teststub.depB") != names.end());
            // depA 的 init 里 getService("svc.b") 非空才成功——加载成功即证明拓扑序正确
            expect(mgr.getService("svc.b") != nullptr);
        }
        cleanupDir(dir);
    };

    // ========== P19 重复加载语义 ==========

    "P19 重复加载语义"_test = [] {
        std::string dir = makePluginDir({"clf.teststub.dll"});
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 1_i);
            expect(mgr.loadAll() == 0_i);                       // loadAll 跳过已加载
            expect(!mgr.load("clf.teststub"));                  // 幂等拒绝
            expect(logTail(1000).find("already loaded") != std::string::npos);
            expect(!mgr.unload("clf.teststub.nonexistent"));    // 未加载 → 无操作
            expect(logTail(1000).find("not loaded") != std::string::npos);
            expect(mgr.unload("clf.teststub"));
            expect(!mgr.unload("clf.teststub"));                // 已卸载再 unload → false
            expect(mgr.load("clf.teststub"));                   // 可重新 load
            expect(mgr.listPluginNames().size() == 1_u);
        }
        cleanupDir(dir);
    };
};

int main() {}
