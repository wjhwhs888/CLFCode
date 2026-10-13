// qa_CLFSessionFileCtx.cpp — 会话文件上下文测试（C2，2026-09-07）
// S1-S6: 文件创建/续写 / turn 差集追加 / summary 行 / 回显行收集

#include <boost/ut.hpp>
#include "CLFTestTempDir.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "CLFCore/CLFMessageCodec.hpp"
#include "CLFCore/CLFSessionFileCtx.hpp"

using namespace boost::ut;
namespace fs = std::filesystem;

using CLF::CLFCore::CLFMessage;
using CLF::CLFCore::CLFMessageCodec;
using CLF::CLFCore::CLFSessionFileCtx;
using CLF::CLFCore::CLFSessionSummary;
using CLF::CLFCore::CLFSessionEchoLine;

namespace {

// 唯一临时工作目录（RAII 统一设施：离开作用域自动清理，失败重试 + 哨兵兜底）
CLFTest::CLFTestTempDir makeTempDir(const std::string& name) {
    return CLFTest::CLFTestTempDir(name);
}

CLFMessage makeMsg(const std::string& role, const std::string& content) {
    CLFMessage m;
    m.m_role = role;
    m.m_content = content;
    return m;
}

} // anonymous namespace

const boost::ut::suite<"CLFSessionFileCtx"> tests = [] {
    "S1 beginSessionFile 全新创建：文件存在 + 活动文件置位 + header 可解析"_test = [] {
        auto dir = makeTempDir("clf_qa_filectx_s1");
        CLFSessionFileCtx ctx;
        ctx.setHistoryDir(dir);
        auto path = ctx.beginSessionFile("first input", "test-model", {"skill-a"});
        expect(!path.empty());
        expect(ctx.getActiveSessionFile() == path);
        expect(fs::exists(fs::u8path(path)));
        // header 行可解析且 title/model 正确（读文件作用域化——Windows 文件锁）
        {
            std::ifstream f(fs::u8path(path));
            std::string line;
            expect(bool(std::getline(f, line)));
            auto obj = nlohmann::json::parse(line);
            expect(obj.value("type", "") == std::string("header"));
            std::string title;
            CLFMessageCodec::parseHeaderLine(obj, &title);
            expect(title == std::string("first input"));
        }
    };

    "S2 beginSessionFile 续写：复制源文件 + 清 resumedFrom + 续后缀"_test = [] {
        auto dir = makeTempDir("clf_qa_filectx_s2");
        // 源文件：header + turn 行（简化合法 jsonl）
        fs::path src = dir.path() / "src.jsonl";
        {
            std::ofstream f(src, std::ios::binary);
            f << R"({"type":"header","title":"源会话","session_id":"sid-1","timestamp":"2026-09-07 10:00:00","model":"m","skills":[]})" << "\n";
        }
        CLFSessionFileCtx ctx;
        ctx.setHistoryDir(dir);
        ctx.setResumedFrom(src.string());
        auto path = ctx.beginSessionFile("", "m", {});
        expect(!path.empty());
        expect(ctx.getResumedFrom().empty());          // 续写态清除
        expect(path.find("续") != std::string::npos);   // 续后缀
        // 内容 = 源文件复制（header 原样；读文件作用域化——Windows 文件锁）
        {
            std::ifstream a(fs::u8path(src.string()));
            std::ifstream b(fs::u8path(path));
            std::string lineA, lineB;
            expect(bool(std::getline(a, lineA)) && bool(std::getline(b, lineB)));
            expect(lineA == lineB);
        }
    };

    "S3 appendTurn 差集：轮初计数后追加新消息"_test = [] {
        auto dir = makeTempDir("clf_qa_filectx_s3");
        CLFSessionFileCtx ctx;
        ctx.setHistoryDir(dir);
        ctx.beginSessionFile("t", "m", {});
        // 轮初 2 条（user+assistant），轮末 4 条 → 差集 2 条入 turn 行
        std::vector<CLFMessage> msgs = {
            makeMsg("user", "hi"), makeMsg("assistant", "hello")};
        ctx.setTurnStartMsgCount(msgs.size());
        msgs.push_back(makeMsg("tool", "result-1"));
        msgs.push_back(makeMsg("assistant", "done"));
        auto path = ctx.appendTurn(msgs, nullptr);
        expect(!path.empty());
        // turn 行存在且解析出 2 条消息（读文件作用域化——Windows 文件锁）
        bool foundTurn = false;
        {
            std::ifstream f(fs::u8path(path));
            std::string line;
            while (std::getline(f, line)) {
                auto obj = nlohmann::json::parse(line);
                if (obj.value("type", "") == "turn") {
                    foundTurn = true;
                    std::vector<CLFMessage> turnMsgs;
                    expect(CLFMessageCodec::parseTurnLine(obj, turnMsgs, nullptr));
                    expect(turnMsgs.size() == 2u);
                    expect(turnMsgs[0].m_content == std::string("result-1"));
                    break;
                }
            }
        }
        expect(foundTurn);
    };

    "S4 appendTurn 无新消息 → 跳过（空串）"_test = [] {
        auto dir = makeTempDir("clf_qa_filectx_s4");
        CLFSessionFileCtx ctx;
        ctx.setHistoryDir(dir);
        ctx.beginSessionFile("t", "m", {});
        std::vector<CLFMessage> msgs = {makeMsg("user", "hi")};
        ctx.setTurnStartMsgCount(msgs.size());
        expect(ctx.appendTurn(msgs, nullptr).empty());   // 无新增
        // 无活动文件也跳过
        CLFSessionFileCtx ctx2;
        expect(ctx2.appendTurn(msgs, nullptr).empty());
    };

    "S5 appendSummaryLine：无效跳过 / 有效追加"_test = [] {
        auto dir = makeTempDir("clf_qa_filectx_s5");
        CLFSessionFileCtx ctx;
        ctx.setHistoryDir(dir);
        ctx.beginSessionFile("t", "m", {});
        CLFSessionSummary invalid;   // m_valid=false
        ctx.appendSummaryLine(invalid);   // 不崩、不追加
        CLFSessionSummary valid;
        valid.m_valid = true;
        valid.m_method = "api";
        valid.m_summary = "compressed";
        ctx.appendSummaryLine(valid);
        // summary 行存在（读文件作用域化——Windows 文件锁）
        bool found = false;
        {
            std::ifstream f(fs::u8path(ctx.getActiveSessionFile()));
            std::string line;
            while (std::getline(f, line)) {
                if (nlohmann::json::parse(line).value("type", "") == "summary") found = true;
            }
        }
        expect(found);
    };

};

// （分册 2 块 3：S6 collectEchoLines 用例迁移 qa_CLFSessionReplayer）

int main() {}
