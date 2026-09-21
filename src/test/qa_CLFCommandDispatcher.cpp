// qa_CLFCommandDispatcher.cpp — 命令注册表查询与 /help 同源测试（M 系列）
// 覆盖：matchingCommands 前缀语义 + /help 动态生成（唯一维护钉）
// 设计文档：设计-命令候选面板与唯一维护.md §五

#include <boost/ut.hpp>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFNetwork/CLFHttpClient.hpp"
#include "CLFTypes/ICLFOutput.hpp"
#include "CLFUI/CLFCommandDispatcher.hpp"

using namespace boost::ut;
using CLF::CLFCore::CLFAgentLoop;
using CLF::CLFCore::CLFAgentConfig;
using CLF::CLFNetwork::ICLFHttpClient;
using CLF::CLFNetwork::CLFHttpResponse;
using CLF::CLFTypes::ICLFOutput;
using CLF::CLFUI::CLFCommandDispatcher;

namespace {

// 最小 Mock HTTP：命令测试不发起网络，调用即失败（qa_CLFAgentLoop 版裁剪）
class MockHttpClient : public ICLFHttpClient {
public:
    void setTimeout(int) override {}
    void abort() override {}
    CLFHttpResponse postJson(const std::string&, const std::string&) override {
        throw std::runtime_error("MockHttpClient: network not expected in command tests");
    }
    CLFHttpResponse postJsonStream(
        const std::string&, const std::string&,
        std::function<void(const std::string&)>) override {
        throw std::runtime_error("MockHttpClient: network not expected in command tests");
    }
};

// 记录 emitContent 的 Output（其余通道空实现）
class MockOutput : public ICLFOutput {
public:
    std::vector<std::string> contents;

    void emitContent(const std::string& t) override { contents.push_back(t); }
    void emitRaw(const std::string&) override {}
    void emitStyledLine(const std::string&, LineStyle) override {}
    void setStatus(const std::string&, int, int) override {}
    void setStatusTextOnly(const std::string&) override {}
    bool confirm(const std::string&) override { return false; }
    void onInterrupt(std::function<void()>) override {}
    void showProgress(const std::vector<std::string>&) override {}
    void finishProgress(const std::string&) override {}
    void emitError(const std::string&) override {}
    void appendThinking(const std::string&) override {}
    void clearThinking() override {}
    void setStatusKind(StatusKind) override {}
    void showFoldedBlock(const std::string&,
                         const std::vector<std::string>&) override {}

    // 全部输出拼接（/help 单次 emitContent 的大段文本检索用）
    std::string joined() const {
        std::string all;
        for (const auto& c : contents) all += c;
        return all;
    }
};

// 组装 fixture。成员声明序 = 构造序，逆序析构——dispatcher 持 agent 引用
// 且先于 agent 析构，生命周期安全（dispatcher 构造时批量注册 13 内置命令）
struct CmdSetup {
    std::shared_ptr<MockHttpClient> mock;
    std::unique_ptr<CLFAgentLoop> agent;
    MockOutput output;
    std::unique_ptr<CLFCommandDispatcher> dispatcher;

    CmdSetup()
        : mock(std::make_shared<MockHttpClient>())
        , agent([&] {
            CLFAgentConfig c;
            c.m_apiKey    = "test-key";
            c.m_modelName = "test-model";
            return std::make_unique<CLFAgentLoop>(c, mock);
        }())
        , output()
        , dispatcher(std::make_unique<CLFCommandDispatcher>(
              *agent, "test-hist", &output, []{})) {}
};

// 注册序全集（registerBuiltinCommands 顺序钉——防误删命令，
// qa_CLFConfigLoader 26 字段先例）
const std::vector<std::string> kAllCommands = {
    "/exit", "/help", "/clear", "/plugin", "/model", "/mode",
    "/config", "/context", "/skill", "/history", "/resume", "/init", "/version",
};

} // namespace

suite<"CLFCommandDispatcher"> cmdSuite = [] {
    "M1 空前缀匹配全部（纯前缀语义；UI 层空串不调查询）"_test = [] {
        CmdSetup s;
        expect(s.dispatcher->matchingCommands("").size() == kAllCommands.size());
    };

    "M2 单斜杠返回全量（注册序名字全集钉）"_test = [] {
        CmdSetup s;
        const auto matches = s.dispatcher->matchingCommands("/");
        expect(matches.size() == kAllCommands.size());
        for (size_t i = 0; i < kAllCommands.size(); ++i) {
            expect(matches[i]->m_name == kAllCommands[i]);
        }
    };

    "M3 /c 前缀过滤（注册序）"_test = [] {
        CmdSetup s;
        const auto matches = s.dispatcher->matchingCommands("/c");
        expect(matches.size() == 3u);
        expect(matches[0]->m_name == "/clear");
        expect(matches[1]->m_name == "/config");
        expect(matches[2]->m_name == "/context");
    };

    "M4 /plug 长前缀唯一命中"_test = [] {
        CmdSetup s;
        const auto matches = s.dispatcher->matchingCommands("/plug");
        expect(matches.size() == 1u);
        expect(matches[0]->m_name == "/plugin");
    };

    "M5 /x 无匹配返回空"_test = [] {
        CmdSetup s;
        expect(s.dispatcher->matchingCommands("/x").empty());
    };

    "M6 /exit 精确名也走前缀路径"_test = [] {
        CmdSetup s;
        const auto matches = s.dispatcher->matchingCommands("/exit");
        expect(matches.size() == 1u);
        expect(matches[0]->m_name == "/exit");
    };

    "M7 含空格零匹配（纯前缀语义；空格收起在 ReplView 判定层）"_test = [] {
        CmdSetup s;
        expect(s.dispatcher->matchingCommands("/model x").empty());
    };

    "M8 /help 与注册表同源（唯一维护钉：描述漂移清零）"_test = [] {
        CmdSetup s;
        expect(s.dispatcher->handle("/help"));
        const std::string help = s.output.joined();
        // 13 个命令名各出现（命令段动态生成自注册表）
        for (const auto& name : kAllCommands) {
            expect(help.find(name) != std::string::npos)
                << "help 缺少命令: " << name;
        }
        // 描述 = 注册表版本（漂移处以注册表为准）
        expect(help.find("插件管理 /plugin [list|load|unload|reload <名>]")
               != std::string::npos);
        // 旧硬编码漂移文案不复存在
        expect(help.find("插件管理（list/load/unload/reload）")
               == std::string::npos);
    };
};

int main() {}
