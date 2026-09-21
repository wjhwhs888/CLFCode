// qa_CLFPluginDomains.cpp — 2.3 铺开三域插件全链路测试（D 系列，2026-09-21）
// 加载真 tools.command/search/web.dll：元数据 + callTool 往返 + 卸载。
// 与 2.2a qa 同设施：每用例独立临时插件目录（复制 3 个生产 DLL）。

#include <boost/ut.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "CLFCore/CLFPluginManager.hpp"
#include "CLFPluginApi/CLFToolApi.hpp"
#include <nlohmann/json.hpp>

using namespace boost::ut;
namespace fs = std::filesystem;

using CLF::CLFCore::CLFPluginManager;
using CLF::CLFPluginApi::CLFToolCallbacks;
using CLF::CLFPluginApi::ICLFToolProvider;

namespace {

std::string pathToUtf8(const fs::path& p) {
    auto s = p.u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

std::atomic<int> g_dirCounter{0};
std::string makePluginDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path dir = fs::temp_directory_path() /
                   ("clf_domains_test_" + std::to_string(stamp) + "_" +
                    std::to_string(g_dirCounter.fetch_add(1)));
    fs::create_directories(dir);
    for (const char* name : {"tools.command.dll", "tools.search.dll", "tools.web.dll"}) {
        fs::copy_file(fs::path(CLF_PROD_PLUGIN_DIR) / name, dir / name);
    }
    return pathToUtf8(dir);
}

void cleanupDir(const std::string& dirUtf8) {
    std::error_code ec;
    fs::remove_all(fs::u8path(dirUtf8), ec);   // manager 已析构（DLL 已卸载）后调用
}

// callTool 便捷封装（onResult/onError 同收）
std::string callToolText(ICLFToolProvider* provider, const std::string& name,
                         const std::string& argsJson) {
    std::string content;
    CLFToolCallbacks cb{};
    cb.onResult = [](void* ctx, const char* s, size_t n) {
        static_cast<std::string*>(ctx)->assign(s, n);
    };
    cb.onError = [](void* ctx, const char* s) {
        static_cast<std::string*>(ctx)->assign(s);
    };
    provider->callTool(name.c_str(), argsJson.c_str(), &content, &cb);
    return content;
}

// 精确路由查找：提供指定工具名的 provider（装配 handler 同款逻辑）
ICLFToolProvider* findProvider(CLFPluginManager& mgr, const std::string& toolName) {
    for (auto* p : mgr.toolProviders()) {
        for (int i = 0; i < p->toolCount(); ++i) {
            const auto* m = p->toolMeta(i);
            if (m && std::string(m->name) == toolName) return p;
        }
    }
    return nullptr;
}

} // namespace

const boost::ut::suite<"CLFPluginDomains"> tests = [] {
    "D1 三插件加载"_test = [] {
        std::string dir = makePluginDir();
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 3_i);
            const auto names = mgr.listPluginNames();
            expect(names.size() == 3_u);
        }
        cleanupDir(dir);
    };

    "D2 元数据同值"_test = [] {
        std::string dir = makePluginDir();
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 3_i);
            auto* p = findProvider(mgr, "execute_command");
            expect(p != nullptr);
            if (p) {
                const auto* m = p->toolMeta(0);
                expect(m != nullptr);
                expect(m->risk == 2_i);              // Command
                expect(m->flags == 0_i);
            }
            auto* s = findProvider(mgr, "search_content");
            expect(s != nullptr);
            if (s) {
                expect(s->toolMeta(0)->flags == 1_i);   // ToolFlagSearch=1<<0（B1 跨边界映射）
            }
            auto* w = findProvider(mgr, "web_fetch");
            expect(w != nullptr);
        }
        cleanupDir(dir);
    };

    "D3 execute_command 往返"_test = [] {
        std::string dir = makePluginDir();
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 3_i);
            auto* p = findProvider(mgr, "execute_command");
            if (!p) return;
            nlohmann::json args{{"command", "echo clf-plugin-ok"}};
            const auto out = callToolText(p, "execute_command", args.dump());
            const auto parsed = nlohmann::json::parse(out);
            expect(parsed.value("success", false));
            expect(parsed.value("stdout", "").find("clf-plugin-ok") != std::string::npos);
        }
        cleanupDir(dir);
    };

    "D4 search_content 往返"_test = [] {
        std::string dir = makePluginDir();
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        fs::path tmpDir = fs::temp_directory_path() / ("clf_search_test_" + std::to_string(stamp));
        fs::create_directories(tmpDir);
        { std::ofstream(tmpDir / "a.txt") << "needle in haystack\n"; }
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 3_i);
            auto* p = findProvider(mgr, "search_content");
            if (!p) return;
            nlohmann::json args{{"pattern", "needle"},
                                {"directory", pathToUtf8(tmpDir)},
                                {"fileTypes", ".txt"}};
            const auto out = callToolText(p, "search_content", args.dump());
            const auto parsed = nlohmann::json::parse(out);
            expect(parsed.value("success", false));
            expect(parsed.value("content", "").find("needle") != std::string::npos);
        }
        cleanupDir(dir);
        std::error_code ec; fs::remove_all(tmpDir, ec);
    };

    "D5 web_fetch 错误路径不崩"_test = [] {
        std::string dir = makePluginDir();
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 3_i);
            auto* p = findProvider(mgr, "web_fetch");
            if (!p) return;
            // url 缺失 → 明确错误（不依赖网络）
            const auto out = callToolText(p, "web_fetch", R"({"url":""})");
            const auto parsed = nlohmann::json::parse(out);
            expect(!parsed.value("success", true));
            expect(parsed.value("error", "") == "url is required");
        }
        cleanupDir(dir);
    };

    "D6 卸载"_test = [] {
        std::string dir = makePluginDir();
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 3_i);
            expect(mgr.unload("tools.command"));
            expect(mgr.unload("tools.search"));
            expect(mgr.unload("tools.web"));
            expect(mgr.toolProviders().empty());
        }
        cleanupDir(dir);
    };
};

int main() {}
