// qa_CLFPluginFileOps.cpp — FileOps 域插件全链路测试（2.2a §六，F1-F12）
// 加载真 tools.fileops.dll（不做 mock——那等于测 mock）：
// file 服务（ICLFFileService 回调推送）+ tool.provider（4 工具 callTool 往返）。
// 与 2.1 qa 同设施：每用例独立临时插件目录（复制 DLL）+ 管理器注入。

#include <boost/ut.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFCore/CLFFileServiceProxy.hpp"
#include "CLFCore/CLFPluginManager.hpp"
#include "CLFTools/CLFBuiltinTools.hpp"   // 2.2b：registerPluginTools
#include "CLFPluginApi/CLFFileService.hpp"
#include "CLFPluginApi/CLFToolApi.hpp"
#include <nlohmann/json.hpp>

using namespace boost::ut;
namespace fs = std::filesystem;

using CLF::CLFCore::CLFPluginManager;
using CLF::CLFPluginApi::CLFDiffOpCode;
using CLF::CLFPluginApi::CLFFileCallbacks;
using CLF::CLFPluginApi::CLFFileInfo;
using CLF::CLFPluginApi::CLFToolCallbacks;
using CLF::CLFPluginApi::ICLFFileService;
using CLF::CLFPluginApi::ICLFToolProvider;

namespace {

std::string pathToUtf8(const fs::path& p) {
    auto s = p.u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

// 每用例独立插件目录：临时目录 + 复制 tools.fileops.dll
std::atomic<int> g_dirCounter{0};
std::string makePluginDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path dir = fs::temp_directory_path() /
                   ("clf_fileops_test_" + std::to_string(stamp) + "_" +
                    std::to_string(g_dirCounter.fetch_add(1)));
    fs::create_directories(dir);
    fs::copy_file(fs::path(CLF_TEST_PLUGIN_DIR) / "tools.fileops.dll",
                  dir / "tools.fileops.dll");
    return pathToUtf8(dir);
}

void cleanupDir(const std::string& dirUtf8) {
    std::error_code ec;
    fs::remove_all(fs::u8path(dirUtf8), ec);   // manager 已析构（DLL 已卸载）后调用
}

std::string readFileText(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// 在临时目录建测试文件
fs::path makeTempFile(const std::string& content) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path p = fs::temp_directory_path() / ("clf_fileops_" + std::to_string(stamp) + ".txt");
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << content;
    f.close();
    return p;
}

// —— 回调接收器 ——
struct ContentCollector { std::string content; };
struct ErrorCollector   { std::string error; };
struct DiffCollector {
    int added = -1, removed = -1, hunks = -1, truncated = -1;
    struct Line { int op; std::string text; };
    std::vector<Line> lines;
};

void onContent(void* ctx, const char* data, size_t len) {
    static_cast<ContentCollector*>(ctx)->content.append(data, len);
}
void onError(void* ctx, const char* msg) {
    static_cast<ErrorCollector*>(ctx)->error = msg;
}
void onDiffLine(void* ctx, int op, int, int, const char* text) {
    static_cast<DiffCollector*>(ctx)->lines.push_back({op, text});
}
void onStats(void* ctx, int added, int removed, int hunks, int truncated, const char*) {
    auto* s = static_cast<DiffCollector*>(ctx);
    s->added = added; s->removed = removed; s->hunks = hunks; s->truncated = truncated;
}

// callTool 便捷封装：返回结果 JSON 字符串
std::string callToolText(ICLFToolProvider* provider, const std::string& name,
                         const std::string& argsJson) {
    std::string content;
    CLFToolCallbacks cb{};
    cb.onResult = [](void* ctx, const char* s, size_t n) {
        static_cast<std::string*>(ctx)->assign(s, n);
    };
    // 错误文本同样是结果文本（onError 两参签名，不可直接赋 onResult）
    cb.onError = [](void* ctx, const char* s) {
        static_cast<std::string*>(ctx)->assign(s);
    };
    provider->callTool(name.c_str(), argsJson.c_str(), &content, &cb);
    return content;
}

} // namespace

const boost::ut::suite<"CLFPluginFileOps"> tests = [] {
    "F1 发现与加载"_test = [] {
        std::string dir = makePluginDir();
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 1_i);
            const auto names = mgr.listPluginNames();
            expect(names.size() == 1_u);
            expect(names[0] == "tools.fileops");
        }
        cleanupDir(dir);
    };

    "F2 file 服务路由"_test = [] {
        std::string dir = makePluginDir();
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 1_i);
            auto* svc = mgr.getService("file");
            expect(svc != nullptr);
            if (svc) {
                auto* fileSvc = static_cast<ICLFFileService*>(svc);
                // 单继承链 downcast 可用：getFileInfo 对不存在文件返回全 0
                expect(fileSvc->getFileInfo("nonexistent").size == 0_ull);
            }
        }
        cleanupDir(dir);
    };

    "F3 readFile 回调推送"_test = [] {
        std::string dir = makePluginDir();
        fs::path tmp = makeTempFile("hello fileops\nline2\n");
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 1_i);
            auto* fileSvc = static_cast<ICLFFileService*>(mgr.getService("file"));
            expect(fileSvc != nullptr);
            if (!fileSvc) return;

            CLFFileInfo info;
            ContentCollector col;
            CLFFileCallbacks cb{};
            cb.onContent = onContent;
            cb.onError   = onError;
            expect(fileSvc->readFile(pathToUtf8(tmp).c_str(), &info, &col, &cb));
            expect(col.content == "hello fileops\nline2\n");
            expect(info.size == col.content.size());
        }
        cleanupDir(dir);
        std::error_code ec; fs::remove(tmp, ec);
    };

    "F4 previewEdit 往返"_test = [] {
        std::string dir = makePluginDir();
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 1_i);
            auto* fileSvc = static_cast<ICLFFileService*>(mgr.getService("file"));
            if (!fileSvc) return;

            ContentCollector col;
            ErrorCollector err;
            CLFFileCallbacks cb{};
            cb.onContent = onContent;
            cb.onError   = onError;
            // 单次匹配 → 新内容整块推
            expect(fileSvc->previewEdit("aaa bbb", 7, "bbb", "ccc", &col, &cb));
            expect(col.content == "aaa ccc");
            // 零匹配 → false + onError（ctx 须为 ErrorCollector——onError 按其写入）
            ContentCollector col2;
            ErrorCollector err2;
            expect(!fileSvc->previewEdit("aaa bbb", 7, "zzz", "ccc", &err2, &cb));
            expect(!err2.error.empty());
        }
        cleanupDir(dir);
    };

    "F5 computeDiff 回调序列"_test = [] {
        std::string dir = makePluginDir();
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 1_i);
            auto* fileSvc = static_cast<ICLFFileService*>(mgr.getService("file"));
            if (!fileSvc) return;

            DiffCollector col;
            CLFFileCallbacks cb{};
            cb.onDiffLine = onDiffLine;
            cb.onStats    = onStats;
            cb.onError    = onError;
            expect(fileSvc->computeDiff("line1\nline2\n", "line1\nlineX\n", 3, &col, &cb));
            expect(col.added == 1_i);
            expect(col.removed == 1_i);
            expect(col.hunks == 1_i);
            expect(!col.lines.empty());
        }
        cleanupDir(dir);
    };

    "F6 getFileInfo"_test = [] {
        std::string dir = makePluginDir();
        fs::path tmp = makeTempFile("abcdef");
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 1_i);
            auto* fileSvc = static_cast<ICLFFileService*>(mgr.getService("file"));
            if (!fileSvc) return;

            const auto info = fileSvc->getFileInfo(pathToUtf8(tmp).c_str());
            expect(info.size == 6_ull);
            expect(info.mtime != 0_ull);
            const auto missing = fileSvc->getFileInfo("nonexistent_file_xyz");
            expect(missing.size == 0_ull);
            expect(missing.mtime == 0_ull);
        }
        cleanupDir(dir);
        std::error_code ec; fs::remove(tmp, ec);
    };

    "F7 tool.provider 路由 + 元数据"_test = [] {
        std::string dir = makePluginDir();
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 1_i);
            auto* provider = static_cast<ICLFToolProvider*>(mgr.getService("tool.provider"));
            expect(provider != nullptr);
            if (!provider) return;

            expect(provider->toolCount() == 4_i);
            // 元数据与静态注册同值（2.2b 切换后模型看到的工具定义零变化）
            const char* expectNames[] = {"read_file", "write_file", "edit_file", "list_directory"};
            const int expectRisks[] = {0, 1, 1, 0};
            const int expectFlags[] = {2 /*ToolFlagRead*/, 0, 0, 2 /*ToolFlagRead*/};
            for (int i = 0; i < 4; ++i) {
                const auto* meta = provider->toolMeta(i);
                expect(meta != nullptr);
                if (meta) {
                    expect(std::string(meta->name) == expectNames[i]);
                    expect(meta->risk == expectRisks[i]);
                    expect(meta->flags == expectFlags[i]);
                }
            }
            expect(provider->toolMeta(4) == nullptr);
        }
        cleanupDir(dir);
    };

    "F8 read_file callTool 往返"_test = [] {
        std::string dir = makePluginDir();
        // 2.2b：插件 init 经 host->config 取 workspace_root = 运行时 cwd——
        // 文件须建在 cwd 内（临时目录会被边界校验拒绝，qa cwd = 构建目录/src）
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        fs::path tmp = fs::current_path() / ("clf_plugin_read_" + std::to_string(stamp) + ".txt");
        {
            std::ofstream f(tmp, std::ios::trunc);
            f << "plugin read ok\n";
            f.close();
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 1_i);
            auto* provider = static_cast<ICLFToolProvider*>(mgr.getService("tool.provider"));
            if (!provider) return;

            nlohmann::json args{{"path", pathToUtf8(tmp)}};
            const auto out = callToolText(provider, "read_file", args.dump());
            const auto parsed = nlohmann::json::parse(out);
            expect(parsed.value("success", false));
            expect(parsed.value("content", "") == "plugin read ok\n");
            // 不存在文件 → 失败文本
            const auto bad = callToolText(provider, "read_file", R"({"path":"no_such_file_xyz"})");
            expect(!nlohmann::json::parse(bad).value("success", true));
            // 工作区外路径 → 边界校验拒绝（handler 层校验语义经 config 通道保持）
            const auto outside = callToolText(provider, "read_file",
                                              R"({"path":"C:/Windows/win.ini"})");
            const auto parsedOutside = nlohmann::json::parse(outside);
            expect(!parsedOutside.value("success", true));
            expect(parsedOutside.value("error", "").find("工作区") != std::string::npos);
        }
        cleanupDir(dir);
        std::error_code ec; fs::remove(tmp, ec);
    };

    "F9 write_file callTool 往返"_test = [] {
        std::string dir = makePluginDir();
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        fs::path tmp = fs::temp_directory_path() / ("clf_plugin_write_" + std::to_string(stamp) + ".txt");
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 1_i);
            auto* provider = static_cast<ICLFToolProvider*>(mgr.getService("tool.provider"));
            if (!provider) return;

            nlohmann::json args{{"path", pathToUtf8(tmp)}, {"content", "written by plugin"}};
            const auto out = callToolText(provider, "write_file", args.dump());
            const auto parsed = nlohmann::json::parse(out);
            expect(parsed.value("success", false));
            expect(readFileText(tmp) == "written by plugin");   // 落盘内容核对
        }
        cleanupDir(dir);
        std::error_code ec; fs::remove(tmp, ec);
    };

    "F10 edit_file callTool 往返"_test = [] {
        std::string dir = makePluginDir();
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        fs::path tmp = fs::temp_directory_path() / ("clf_plugin_edit_" + std::to_string(stamp) + ".txt");
        {
            std::ofstream f(tmp, std::ios::trunc);
            f << "before";
            f.close();
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 1_i);
            auto* provider = static_cast<ICLFToolProvider*>(mgr.getService("tool.provider"));
            if (!provider) return;

            nlohmann::json args{{"path", pathToUtf8(tmp)},
                                {"old_string", "before"}, {"new_string", "after"}};
            const auto out = callToolText(provider, "edit_file", args.dump());
            expect(nlohmann::json::parse(out).value("success", false));
            expect(readFileText(tmp) == "after");   // 改后内容核对
        }
        cleanupDir(dir);
        std::error_code ec; fs::remove(tmp, ec);
    };

    "F11 list_directory callTool 往返"_test = [] {
        std::string dir = makePluginDir();
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        fs::path tmpDir = fs::temp_directory_path() / ("clf_plugin_list_" + std::to_string(stamp));
        fs::create_directories(tmpDir);
        { std::ofstream(tmpDir / "a.txt") << "a"; }
        { std::ofstream(tmpDir / "b.txt") << "b"; }
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 1_i);
            auto* provider = static_cast<ICLFToolProvider*>(mgr.getService("tool.provider"));
            if (!provider) return;

            nlohmann::json args{{"path", pathToUtf8(tmpDir)}};
            const auto out = callToolText(provider, "list_directory", args.dump());
            const auto parsed = nlohmann::json::parse(out);
            expect(parsed.value("success", false));
            const std::string content = parsed.value("content", "");
            expect(content.find("a.txt") != std::string::npos);
            expect(content.find("b.txt") != std::string::npos);
        }
        cleanupDir(dir);
        std::error_code ec; fs::remove_all(tmpDir, ec);
    };

    "F12 卸载"_test = [] {
        std::string dir = makePluginDir();
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 1_i);
            expect(mgr.unload("tools.fileops"));
            expect(mgr.getService("file") == nullptr);
            expect(mgr.getService("tool.provider") == nullptr);
        }
        cleanupDir(dir);
    };

    // ========== G 系列（2.2b 装配与转发代理） ==========

    "G1 装配注册"_test = [] {
        std::string dir = makePluginDir();
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 1_i);

            CLF::CLFCore::CLFAgentConfig config;
            config.m_apiKey = "test-key";
            CLF::CLFCore::CLFAgentLoop agent(config);
            CLF::CLFTools::registerPluginTools(agent, mgr);

            const auto& tools = agent.getTools();
            expect(tools.size() == 4_u);   // read/write/edit/list 装配
            const char* expectNames[] = {"read_file", "write_file", "edit_file", "list_directory"};
            for (int i = 0; i < 4; ++i) {
                bool found = false;
                for (const auto& t : tools) {
                    if (t.m_name == expectNames[i]) {
                        found = true;
                        break;
                    }
                }
                expect(found) << expectNames[i];
            }
            // 标签映射（B1 跨边界：ToolFlagRead → m_isRead）
            for (const auto& t : tools) {
                if (t.m_name == "read_file" || t.m_name == "list_directory") {
                    expect(t.m_isRead);
                } else {
                    expect(!t.m_isRead);
                }
            }
        }
        cleanupDir(dir);
    };

    "G2 装配 handler 往返"_test = [] {
        std::string dir = makePluginDir();
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        fs::path tmp = fs::current_path() / ("clf_plugin_asm_" + std::to_string(stamp) + ".txt");
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 1_i);

            CLF::CLFCore::CLFAgentConfig config;
            config.m_apiKey = "test-key";
            CLF::CLFCore::CLFAgentLoop agent(config);
            CLF::CLFTools::registerPluginTools(agent, mgr);

            // 经装配的 handler 全链路：装配 → callTool → 插件 → 能力
            const auto& tools = agent.getTools();
            auto it = std::find_if(tools.begin(), tools.end(),
                                   [](const auto& t) { return t.m_name == "write_file"; });
            expect(it != tools.end());
            if (it == tools.end()) return;
            nlohmann::json args{{"path", pathToUtf8(tmp)}, {"content", "assembled write"}};
            const auto out = it->m_handler(args.dump());
            expect(nlohmann::json::parse(out).value("success", false));
            expect(readFileText(tmp) == "assembled write");
        }
        cleanupDir(dir);
        std::error_code ec; fs::remove(tmp, ec);
    };

    "G3 卸载后兜底"_test = [] {
        std::string dir = makePluginDir();
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 1_i);

            CLF::CLFCore::CLFAgentConfig config;
            config.m_apiKey = "test-key";
            CLF::CLFCore::CLFAgentLoop agent(config);
            CLF::CLFTools::registerPluginTools(agent, mgr);

            expect(mgr.unload("tools.fileops"));   // 热卸载（验证点 4）
            const auto& tools = agent.getTools();
            auto it = std::find_if(tools.begin(), tools.end(),
                                   [](const auto& t) { return t.m_name == "read_file"; });
            expect(it != tools.end());
            if (it == tools.end()) return;
            // 调用时 getService nullptr → 兜底错误文本（不崩，模型自兜底）
            const auto out = it->m_handler(R"({"path":"x"})");
            const auto parsed = nlohmann::json::parse(out);
            expect(!parsed.value("success", true));
            expect(parsed.value("error", "").find("插件已停用") != std::string::npos);
        }
        cleanupDir(dir);
    };

    "G4 proxy 转发"_test = [] {
        std::string dir = makePluginDir();
        fs::path tmp = makeTempFile("proxy forward ok\n");
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 1_i);
            CLF::CLFCore::CLFFileServiceProxy proxy(&mgr);

            CLFFileInfo info;
            ContentCollector col;
            CLFFileCallbacks cb{};
            cb.onContent = onContent;
            cb.onError   = onError;
            expect(proxy.readFile(pathToUtf8(tmp).c_str(), &info, &col, &cb));
            expect(col.content == "proxy forward ok\n");
            expect(info.size == col.content.size());
        }
        cleanupDir(dir);
        std::error_code ec; fs::remove(tmp, ec);
    };

    "G5 proxy 停用兜底"_test = [] {
        std::string dir = makePluginDir();
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 1_i);
            CLF::CLFCore::CLFFileServiceProxy proxy(&mgr);
            expect(mgr.unload("tools.fileops"));

            CLFFileInfo info;
            ErrorCollector err;
            CLFFileCallbacks cb{};
            cb.onError = onError;
            expect(!proxy.readFile("x", &info, &err, &cb));   // 停用 → false
            expect(err.error.find("插件已停用") != std::string::npos);
            expect(proxy.getFileInfo("x").size == 0_ull);     // 元信息全 0
        }
        cleanupDir(dir);
    };
};

int main() {}
