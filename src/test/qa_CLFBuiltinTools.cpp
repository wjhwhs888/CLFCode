// qa_CLFBuiltinTools.cpp — 工具层辅助函数单元测试（设计 S2-1 / S2-3）
// B1: isWithinWorkspace 工作区边界（含兄弟目录前缀陷阱）
// B2: exitCodeMeansSuccess 退出码白名单
// B3: sliceLines 行范围切片

#include <boost/ut.hpp>

#include <filesystem>
#include <string>

#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFTools/CLFBuiltinTools.hpp"

using namespace boost::ut;
namespace fs = std::filesystem;
using CLF::CLFTools::detail::exitCodeMeansSuccess;
using CLF::CLFTools::detail::isWithinWorkspace;
using CLF::CLFTools::detail::sliceLines;
using CLF::CLFTools::todoWriteHandlerImpl;

const boost::ut::suite<"CLFBuiltinTools"> tests = [] {
    // ========== B1: 工作区边界 ==========

    "B1a 工作区内的相对路径放行"_test = [] {
        std::string err;
        expect(isWithinWorkspace("a.txt", err)) << err;
        expect(isWithinWorkspace("./sub/b.txt", err)) << err;
    };

    "B1b 工作区自身放行"_test = [] {
        std::string err;
        expect(isWithinWorkspace(fs::current_path().string(), err)) << err;
    };

    "B1c 工作区内的绝对路径放行"_test = [] {
        std::string err;
        const auto inside = (fs::current_path() / "nested" / "c.txt").string();
        expect(isWithinWorkspace(inside, err)) << err;
    };

    "B1d 上级目录被拒"_test = [] {
        std::string err;
        expect(!isWithinWorkspace("../outside.txt", err));
        expect(err.find("超出工作区边界") != std::string::npos);
    };

    // B1e 是本设计的关键陷阱：若用字符串前缀比较，"<cwd>-evil" 会被误判在
    // "<cwd>" 之内。逐段比较才能正确拒绝。
    "B1e 同名前缀的兄弟目录被拒（前缀比较陷阱）"_test = [] {
        std::string err;
        const auto cwd     = fs::current_path();
        const auto sibling = cwd.parent_path() / (cwd.filename().string() + "-evil");
        expect(!isWithinWorkspace((sibling / "x.txt").string(), err));
        expect(err.find("超出工作区边界") != std::string::npos);
    };

    "B1f 系统目录被拒"_test = [] {
        std::string err;
#ifdef _WIN32
        expect(!isWithinWorkspace("C:/Windows/System32/drivers/etc/hosts", err));
#else
        expect(!isWithinWorkspace("/etc/passwd", err));
#endif
    };

    // ========== B2: 退出码白名单 ==========

    "B2a 退出码 0 一律成功"_test = [] {
        expect(exitCodeMeansSuccess("anything at all", 0));
        expect(exitCodeMeansSuccess("cmake --build .", 0));
    };

    "B2b 白名单命令的退出码 1 视为成功"_test = [] {
        expect(exitCodeMeansSuccess("grep foo a.txt", 1));
        expect(exitCodeMeansSuccess("rg pattern src", 1));
        expect(exitCodeMeansSuccess("findstr foo a.txt", 1));
        expect(exitCodeMeansSuccess("diff a.txt b.txt", 1));
        expect(exitCodeMeansSuccess("fc a.txt b.txt", 1));
    };

    "B2c 首 token 归一化：去路径/扩展名/大小写/引号"_test = [] {
        expect(exitCodeMeansSuccess("grep.exe foo a.txt", 1));
        expect(exitCodeMeansSuccess("C:/tools/grep.exe foo a.txt", 1));
        expect(exitCodeMeansSuccess("/usr/bin/grep foo a.txt", 1));
        expect(exitCodeMeansSuccess("GREP foo a.txt", 1));
        expect(exitCodeMeansSuccess("\"grep\" foo a.txt", 1));
    };

    "B2d 非白名单命令的退出码 1 仍是失败"_test = [] {
        expect(!exitCodeMeansSuccess("cmake --build .", 1));
        expect(!exitCodeMeansSuccess("git push", 1));
        expect(!exitCodeMeansSuccess("", 1));
    };

    "B2e 白名单命令的其他非零退出码仍是失败"_test = [] {
        expect(!exitCodeMeansSuccess("grep foo a.txt", 2));   // grep 用 2 表示真错误
        expect(!exitCodeMeansSuccess("diff a b", 2));
        expect(!exitCodeMeansSuccess("grep foo a.txt", -1));
    };

    // ========== B3: 行范围切片 ==========

    "B3a 未指定范围时原样返回"_test = [] {
        const std::string src = "l0\nl1\nl2\n";
        expect(sliceLines(src, 0, 0) == src);
        expect(sliceLines(src, 0, -1) == src);
    };

    "B3b offset 跳过前 N 行"_test = [] {
        const std::string src = "l0\nl1\nl2\nl3\n";
        expect(sliceLines(src, 2, 0) == std::string("l2\nl3\n"));
    };

    "B3c limit 限制行数"_test = [] {
        const std::string src = "l0\nl1\nl2\nl3\n";
        expect(sliceLines(src, 0, 2) == std::string("l0\nl1\n"));
    };

    "B3d offset + limit 组合"_test = [] {
        const std::string src = "l0\nl1\nl2\nl3\nl4\n";
        expect(sliceLines(src, 1, 2) == std::string("l1\nl2\n"));
    };

    "B3e offset 超出总行数返回空"_test = [] {
        const std::string src = "l0\nl1\n";
        expect(sliceLines(src, 10, 5).empty());
    };

    // ========== B4: todo_write 面板状态接线（2026-09-02 实机验收修复） ==========

    // 实机验收抓出的 bug：跨轮场景"新回合清空"置 done 后，模型 update 不恢复面板——
    // 面板在第三步操作时消失。修复：update 与 create 一致清 done（dsh projection 语义）
    "B4 update 清面板隐藏标志（跨轮重现）"_test = [] {
        using CLF::CLFCore::CLFAgentLoop;
        using CLF::CLFCore::CLFAgentConfig;
        CLFAgentConfig config;
        config.m_apiKey = "k";
        CLFAgentLoop agent(config);   // 默认 http 客户端，构造不发起请求

        // 第一轮：create 建清单 → 面板显示（done 清）
        const auto r1 = todoWriteHandlerImpl(
            R"({"action":"create","todos":[{"content":"任务1"},{"content":"任务2"}]})",
            agent);
        expect(r1.find("\"success\":true") != std::string::npos);
        expect(!agent.isTodoPanelDone());   // create 清 done

        // 新回合开始：submit 清面板（§3.3）
        agent.setTodoPanelDone(true);
        expect(agent.isTodoPanelDone());

        // 本轮模型 update 第一项 → 面板必须重现（修复点）
        const auto r2 = todoWriteHandlerImpl(
            R"({"action":"update","id":"1","status":"in_progress"})", agent);
        expect(r2.find("\"success\":true") != std::string::npos);
        expect(!agent.isTodoPanelDone());   // update 也清 done

        const auto todos = agent.getTodos();
        expect(todos.size() == 2_ul);
        expect(todos[0].m_status == std::string("in_progress"));
        expect(todos[1].m_status == std::string("pending"));

        // clear 后清单空 → 面板不显示（empty 条件），done 状态无关紧要
        agent.setTodoPanelDone(false);
        const auto r3 = todoWriteHandlerImpl(R"({"action":"clear"})", agent);
        expect(r3.find("\"success\":true") != std::string::npos);
        expect(agent.getTodos().empty());
    };
};

int main() {}
