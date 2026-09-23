// qa_CLFPluginDomains.cpp — 2.3 铺开三域插件全链路测试（D 系列，2026-09-21）
// 加载真 tools.command/search/web.dll：元数据 + callTool 往返 + 卸载。
// 与 2.2a qa 同设施：每用例独立临时插件目录（复制 3 个生产 DLL）。

#include <boost/ut.hpp>
#include "CLFTestTempDir.hpp"

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

// 唯一临时插件目录（RAII 统一设施：按值返回移动，用例离开作用域自动清理——
// 作用域顺序保证 mgr 内层块先析构（DLL 卸载）→ 目录后析构）
CLFTest::CLFTestTempDir makePluginDir() {
    CLFTest::CLFTestTempDir d("clf_domains_test_");
    for (const char* name : {"tools.command.dll", "tools.search.dll", "tools.web.dll"}) {
        fs::copy_file(fs::path(CLF_PROD_PLUGIN_DIR) / name, d.path() / name);
    }
    return d;
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
        auto dir = makePluginDir();
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 3_i);
            const auto names = mgr.listPluginNames();
            expect(names.size() == 3_u);
        }
    };

    "D2 元数据同值"_test = [] {
        auto dir = makePluginDir();
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
    };

    "D3 execute_command 往返"_test = [] {
        auto dir = makePluginDir();
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 3_i);
            auto* p = findProvider(mgr, "execute_command");
            if (!p) return;   // RAII：早退同样析构清理（dir 在外层作用域）
            nlohmann::json args{{"command", "echo clf-plugin-ok"}};
            const auto out = callToolText(p, "execute_command", args.dump());
            const auto parsed = nlohmann::json::parse(out);
            expect(parsed.value("success", false));
            expect(parsed.value("stdout", "").find("clf-plugin-ok") != std::string::npos);
        }
    };

    "D4 search_content 往返"_test = [] {
        auto dir = makePluginDir();
        CLFTest::CLFTestTempDir tmpDir("clf_search_test_");
        { std::ofstream(tmpDir.path() / "a.txt") << "needle in haystack\n"; }
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
    };

    "D5 web_fetch 错误路径不崩"_test = [] {
        auto dir = makePluginDir();
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
    };

    "D6 卸载"_test = [] {
        auto dir = makePluginDir();
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 3_i);
            expect(mgr.unload("tools.command"));
            expect(mgr.unload("tools.search"));
            expect(mgr.unload("tools.web"));
            expect(mgr.toolProviders().empty());
        }
    };

    "D7 ABI 取消通道：execute_command 长命令中途取消 → interrupted 结果"_test = [] {
        auto dir = makePluginDir();
        {
            CLFPluginManager mgr(dir);
            expect(mgr.loadAll() == 3_i);
            auto* p = findProvider(mgr, "execute_command");
            expect(p != nullptr);

            // 取消查询 ctx（对齐宿主 CLFToolCallCtx 形态：out 指针 + 取消状态；
            // 轮询计数——第 N 次查询后返回 true，≈750ms 后取消）
            struct CancelCtx { int polls = 0; std::string* out = nullptr; };
            std::string content;
            CancelCtx cctx;
            cctx.out = &content;
            CLFToolCallbacks cb{};
            cb.onResult = [](void* ctx, const char* s, size_t n) {
                static_cast<CancelCtx*>(ctx)->out->assign(s, n);
            };
            cb.onError = [](void* ctx, const char* s) {
                static_cast<CancelCtx*>(ctx)->out->assign(s);
            };
            cb.isCancelled = [](void* ctx) -> bool {
                return ++static_cast<CancelCtx*>(ctx)->polls > 15;
            };
            nlohmann::json args{
                {"command", "powershell -NoProfile -Command \"Start-Sleep 30\""},
                {"timeout", 60}};
            const auto t0 = std::chrono::steady_clock::now();
            p->callTool("execute_command", args.dump().c_str(), &cctx, &cb);
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();

            auto r = nlohmann::json::parse(content);
            // 中断语义（设计-中断时效性 A.4）：success=false + interrupted=true
            expect(r.value("success", true) == false);
            expect(r.value("interrupted", false) == true);
            expect(elapsedMs < 5000);   // 30s 睡眠命令被 ≤5s 取消（含 powershell 启动）
        }
    };
};

int main() {}
