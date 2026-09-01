// qa_CLFAgentLoop.cpp — Agent 主循环集成测试（L2，Mock HTTP）
// 覆盖：tool-calling 循环、安全策略阻断、流式累积

#include <boost/ut.hpp>
#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <stdexcept>
#include <thread>

#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFCore/CLFMessageCodec.hpp"
#include "CLFCore/CLFSessionManager.hpp"
#include "CLFNetwork/CLFHttpClient.hpp"

using namespace boost::ut;
using CLF::CLFCore::CLFAgentLoop;
using CLF::CLFCore::CLFAgentConfig;
using CLF::CLFCore::CLFMessage;
using CLF::CLFCore::CLFMessageCodec;
using CLF::CLFCore::CLFSessionManager;
using CLF::CLFCore::CLFTodoItem;
using CLF::CLFCore::CLFTool;
using CLF::CLFCore::CLFSecurityMode;
using CLF::CLFNetwork::ICLFHttpClient;
using CLF::CLFNetwork::CLFHttpResponse;

namespace {

// Mock HTTP 客户端：按序消费预设响应
class MockHttpClient : public ICLFHttpClient {
public:
    void setTimeout(int) override {}
    void abort() override {}

    // 预设同步响应（顺序消费）
    void pushResponse(const std::string& body, const std::string& error = "") {
        m_syncResponses.push_back({200, body, error});
    }

    // 预设流式响应（SSE 行序列，含 data: 前缀）
    void pushStream(const std::vector<std::string>& sseLines) {
        m_streamResponses.push_back(sseLines);
    }

    int syncCallCount() const { return m_syncCalls; }
    int streamCallCount() const { return m_streamCalls; }

    CLFHttpResponse postJson(const std::string&, const std::string&) override {
        ++m_syncCalls;
        // 预设不足必须抛异常，不能只 expect 后继续：boost::ut 的 expect 只记录
        // 失败、不终止执行，继续对空 deque 调 front()/pop_front() 是 UB——
        // MSVC Debug 的 _STL_VERIFY 会弹断言对话框，无人值守下进程永久挂起
        // （表现为 ctest 超时且无任何输出）。抛异常则由 runTurn 的 catch 兜住。
        if (m_syncResponses.empty()) {
            expect(false) << "MockHttpClient: sync response queue exhausted";
            throw std::runtime_error("MockHttpClient: sync response queue exhausted");
        }
        auto resp = m_syncResponses.front();
        m_syncResponses.pop_front();
        return resp;
    }

    CLFHttpResponse postJsonStream(
        const std::string&, const std::string&,
        std::function<void(const std::string&)> onLine) override {
        ++m_streamCalls;
        if (m_streamResponses.empty()) {  // 同 postJson：空队列 front() 是 UB
            expect(false) << "MockHttpClient: stream response queue exhausted";
            throw std::runtime_error("MockHttpClient: stream response queue exhausted");
        }
        auto lines = m_streamResponses.front();
        m_streamResponses.pop_front();
        int idx = 0;
        for (const auto& line : lines) {
            onLine(line);
            // T6a: 喂到 hookAfterLine 行后触发注入的中断回调
            if (idx == hookAfterLine && onLineHook) onLineHook();
            ++idx;
        }
        return {200, "", ""};
    }

    // T6a 中断注入钩子
    int hookAfterLine = -1;
    std::function<void()> onLineHook;

private:
    std::deque<CLFHttpResponse> m_syncResponses;
    std::deque<std::vector<std::string>> m_streamResponses;
    int m_syncCalls = 0;
    int m_streamCalls = 0;
};

// 构造带 Mock 的 Agent + 注册 echo 工具
std::unique_ptr<CLFAgentLoop> makeAgent(std::shared_ptr<MockHttpClient> mock, const std::string& mode = "edit") {
    CLFAgentConfig config;
    config.m_apiKey    = "test-key";
    config.m_modelName = "test-model";
    config.m_stream    = false;
    config.m_securityMode = mode;
    config.m_maxToolCallIterations = 8;

    auto agent = std::make_unique<CLFAgentLoop>(config, mock);

    CLFTool echoTool;
    echoTool.m_name = "echo";
    echoTool.m_description = "echo test";
    echoTool.m_parametersSchema = R"({"type":"object","properties":{"msg":{"type":"string"}},"required":["msg"]})";
    echoTool.m_risk = CLF::CLFCore::CLFToolRisk::Read;
    echoTool.m_handler = [](const std::string& args) { return "Echo:" + args; };
    agent->registerTool(echoTool);

    return agent;
}

// T6: 记录输出调用的 Mock（中断消息计数 + StatusKind 序列）
class MockOutput : public CLF::CLFTypes::ICLFOutput {
public:
    std::vector<std::string> contents;
    std::vector<ICLFOutput::StatusKind> kinds;

    void emitContent(const std::string& t) override { contents.push_back(t); }
    void emitRaw(const std::string&) override {}
    void emitStyledLine(const std::string&, LineStyle) override {}
    void setStatus(const std::string&, int, int) override {}
    void setStatusTextOnly(const std::string&) override {}
    bool confirm(const std::string&) override { return false; }
    void onInterrupt(std::function<void()> cb) override { m_cb = std::move(cb); }
    void showProgress(const std::vector<std::string>&) override {}
    void finishProgress(const std::string&) override {}
    void emitError(const std::string&) override {}
    void appendThinking(const std::string&) override {}
    void clearThinking() override {}
    void setStatusKind(StatusKind k) override { kinds.push_back(k); }
    void showFoldedBlock(const std::string& summary,
                         const std::vector<std::string>& lines) override {
        foldedSummary = summary;
        foldedLines = lines;
    }
    std::string foldedSummary;
    std::vector<std::string> foldedLines;

    void fireInterrupt() { if (m_cb) m_cb(); }
    int interruptEmissions() const {
        int n = 0;
        for (const auto& c : contents)
            if (c.find("已中断") != std::string::npos) ++n;
        return n;
    }

private:
    std::function<void()> m_cb;
};

// V 系列（J3，jsonl 会话文件上下文，2026-09-02）：临时目录 + mock + agent 组合
struct VSetup {
    std::filesystem::path dir;
    std::shared_ptr<MockHttpClient> mock;
    std::unique_ptr<CLFAgentLoop> agent;

    VSetup() {
        dir = std::filesystem::temp_directory_path()
            / ("clf_agent_v_" + std::to_string(
                   std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(dir);
        mock = std::make_shared<MockHttpClient>();
        agent = makeAgent(mock);
        agent->setHistoryDir(dir.string());   // temp 路径无中文，string() 安全
    }
    ~VSetup() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    void pushStop(const std::string& content = "回答") {
        const std::string body =
            "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\""
            + content + "\"},\"finish_reason\":\"stop\"}]}";
        mock->pushResponse(body);
    }
    // 读 jsonl 全部行（nlohmann 对象列表）
    // ⚠️ 窄字符 ifstream 按 CP936 解释路径——中文文件名必须 u8path（编码陷阱族）
    static std::vector<nlohmann::json> readLines(const std::string& path) {
        std::vector<nlohmann::json> lines;
        std::ifstream f(std::filesystem::u8path(path));
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            try { lines.push_back(nlohmann::json::parse(line)); } catch (...) {}
        }
        return lines;
    }
};

} // anonymous namespace

const boost::ut::suite<"CLFAgentLoop"> tests = [] {
    "tool-calling 循环：先 tool_calls 后 stop，两轮完成"_test = [] {
        auto mock = std::make_shared<MockHttpClient>();
        mock->pushResponse(R"({
            "choices": [{
                "message": {
                    "role": "assistant",
                    "content": "让我查一下",
                    "tool_calls": [{
                        "id": "call_1",
                        "type": "function",
                        "function": {"name": "echo", "arguments": "{\"msg\":\"hi\"}"}
                    }]
                },
                "finish_reason": "tool_calls"
            }]
        })");
        mock->pushResponse(R"({
            "choices": [{
                "message": {"role": "assistant", "content": "查询完成：Echo:{\"msg\":\"hi\"}"},
                "finish_reason": "stop"
            }]
        })");

        auto agent = makeAgent(mock);
        std::string result = agent->runTurn("echo hi");
        expect(mock->syncCallCount() == 2);
        expect(result.find("查询完成") != std::string::npos);
    };

    "Analyze 模式：命令类工具被阻断（不执行 handler）"_test = [] {
        auto mock = std::make_shared<MockHttpClient>();
        // 只有一轮响应：模型请求写工具 → 被阻断 → 结果回传 → 模型回复
        mock->pushResponse(R"({
            "choices": [{
                "message": {
                    "role": "assistant",
                    "content": null,
                    "tool_calls": [{
                        "id": "call_2",
                        "type": "function",
                        "function": {"name": "write_file", "arguments": "{\"path\":\"a.txt\"}"}
                    }]
                },
                "finish_reason": "tool_calls"
            }]
        })");
        mock->pushResponse(R"({
            "choices": [{
                "message": {"role": "assistant", "content": "工具被安全策略阻断"},
                "finish_reason": "stop"
            }]
        })");

        CLFAgentConfig config;
        config.m_apiKey = "k";
        config.m_securityMode = "analyze";
        auto agent = std::make_unique<CLFAgentLoop>(config, mock);

        // 注册写工具（handler 不应被调用）
        bool handlerCalled = false;
        CLFTool writeTool;
        writeTool.m_name = "write_file";
        writeTool.m_risk = CLF::CLFCore::CLFToolRisk::Write;
        writeTool.m_handler = [&](const std::string&) { handlerCalled = true; return "wrote"; };
        agent->registerTool(writeTool);

        std::string result = agent->runTurn("写文件");
        expect(!handlerCalled); // 阻断，handler 未执行
        expect(result.find("安全策略") != std::string::npos ||
               result.find("Blocked") != std::string::npos);
    };

    "流式路径：SSE 行累积出完整内容 + tool_calls"_test = [] {
        auto mock = std::make_shared<MockHttpClient>();
        CLFAgentConfig config;
        config.m_apiKey = "k";
        config.m_stream = true;
        config.m_maxToolCallIterations = 4;
        auto agent = std::make_unique<CLFAgentLoop>(config, mock);

        CLFTool timeTool;
        timeTool.m_name = "get_time";
        timeTool.m_risk = CLF::CLFCore::CLFToolRisk::Read;
        timeTool.m_handler = [](const std::string&) { return "12:00"; };
        agent->registerTool(timeTool);

        // 流式：文本 → tool_calls delta → finish_reason → [DONE]
        mock->pushStream({
            "data: {\"choices\":[{\"delta\":{\"content\":\"时间\"}}]}",
            "data: {\"choices\":[{\"delta\":{\"content\":\"查询中\"}}]}",
            "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call_s\",\"function\":{\"name\":\"get_time\",\"arguments\":\"\"}}]}}]}",
            "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"arguments\":\"{}\"}}]}}]}",
            "data: {\"choices\":[{\"finish_reason\":\"tool_calls\"}]}",
            "data: [DONE]"
        });
        // 第二轮：最终回答
        mock->pushStream({
            "data: {\"choices\":[{\"delta\":{\"content\":\"现在是12:00\"}}]}",
            "data: {\"choices\":[{\"finish_reason\":\"stop\"}]}",
            "data: [DONE]"
        });

        std::string result = agent->runTurn("几点了");
        expect(mock->streamCallCount() == 2);
        expect(result.empty()); // 流式返回空串（已实时输出）
    };

    // ========== T6: emitInterrupted 三时点中断（F17 裁决：不加 once 语义，测试兜底） ==========

    "T6a 中断于流式中：恰好一条中断消息 + Warn"_test = [] {
        auto mock = std::make_shared<MockHttpClient>();
        CLFAgentConfig config;
        config.m_apiKey = "k";
        config.m_stream = true;
        auto agent = std::make_unique<CLFAgentLoop>(config, mock);
        MockOutput out;
        agent->setOutput(&out);

        mock->hookAfterLine = 1;  // 第 2 行之后触发中断
        mock->onLineHook = [&] { out.fireInterrupt(); };
        mock->pushStream({
            "data: {\"choices\":[{\"delta\":{\"content\":\"a\"}}]}",
            "data: {\"choices\":[{\"delta\":{\"content\":\"b\"}}]}",
            "data: [DONE]"
        });

        std::string result = agent->runTurn("hi");
        expect(result == "[Interrupted]");
        expect(out.interruptEmissions() == 1);
        expect(out.kinds.back() == CLF::CLFTypes::ICLFOutput::StatusKind::Warn);
    };

    "T6b 中断于重试等待中：恰好一条中断消息 + Warn"_test = [] {
        auto mock = std::make_shared<MockHttpClient>();
        CLFAgentConfig config;
        config.m_apiKey = "k";
        config.m_stream = false;
        config.m_maxToolCallIterations = 4;
        auto agent = std::make_unique<CLFAgentLoop>(config, mock);
        MockOutput out;
        agent->setOutput(&out);

        mock->pushResponse("", "timeout");  // 非致命 → 进入可中断重试等待

        std::string result;
        std::thread runner([&] { result = agent->runTurn("hi"); });
        std::this_thread::sleep_for(std::chrono::milliseconds(150));  // 已进入等待循环
        out.fireInterrupt();
        runner.join();

        expect(result == "[Interrupted]");
        expect(out.interruptEmissions() == 1);
        expect(out.kinds.back() == CLF::CLFTypes::ICLFOutput::StatusKind::Warn);
    };

    // ========== T4: 状态点状态机（P1-1 接线全表） ==========

    "T4a 正常完成：Running → Done 序列，Done 常亮不自动清"_test = [] {
        auto mock = std::make_shared<MockHttpClient>();
        CLFAgentConfig config;
        config.m_apiKey = "k";
        config.m_stream = false;
        auto agent = std::make_unique<CLFAgentLoop>(config, mock);
        MockOutput out;
        agent->setOutput(&out);

        mock->pushResponse(R"({
            "choices": [{
                "message": {"role": "assistant", "content": "hi"},
                "finish_reason": "stop"
            }]
        })");

        std::string result = agent->runTurn("hello");
        expect(!result.empty());
        expect(out.kinds.size() >= 2);
        expect(out.kinds.front() == CLF::CLFTypes::ICLFOutput::StatusKind::Running);
        expect(out.kinds.back() == CLF::CLFTypes::ICLFOutput::StatusKind::Done);
        // F14: turn 结束后 Done 常亮（无自动清除）
    };

    "T4b 致命错误 return：Error 不被 TurnGuard 覆盖（F20）"_test = [] {
        auto mock = std::make_shared<MockHttpClient>();
        CLFAgentConfig config;
        config.m_apiKey = "k";
        config.m_stream = false;
        auto agent = std::make_unique<CLFAgentLoop>(config, mock);
        MockOutput out;
        agent->setOutput(&out);

        mock->pushResponse("", "HTTP 401 unauthorized");

        std::string result = agent->runTurn("hi");
        expect(result.find("[Error]") != std::string::npos);
        expect(out.kinds.back() == CLF::CLFTypes::ICLFOutput::StatusKind::Error);
    };

    "T10a 同步响应 usage → 会话累计"_test = [] {
        auto mock = std::make_shared<MockHttpClient>();
        CLFAgentConfig config;
        config.m_apiKey = "k";
        config.m_stream = false;
        auto agent = std::make_unique<CLFAgentLoop>(config, mock);

        mock->pushResponse(R"({
            "choices": [{
                "message": {"role": "assistant", "content": "hi"},
                "finish_reason": "stop"
            }],
            "usage": {"prompt_tokens": 90, "completion_tokens": 60, "total_tokens": 150}
        })");

        agent->runTurn("hello");
        expect(agent->getTotalTokensUsed() == 150);
    };

    "T10c 流式 usage chunk（choices 空数组）穿透 lambda → 会话累计"_test = [] {
        auto mock = std::make_shared<MockHttpClient>();
        CLFAgentConfig config;
        config.m_apiKey = "k";
        config.m_stream = true;
        auto agent = std::make_unique<CLFAgentLoop>(config, mock);

        mock->pushStream({
            "data: {\"choices\":[{\"delta\":{\"content\":\"hi\"}}]}",
            "data: {\"choices\":[{\"finish_reason\":\"stop\"}]}",
            // 真实形态：usage chunk 的 choices 为空数组——必须穿透 lambda 的 choices 过滤
            "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":70,\"completion_tokens\":30,\"total_tokens\":100}}",
            "data: [DONE]"
        });

        agent->runTurn("hello");
        expect(agent->getTotalTokensUsed() == 100);
    };

    "T10b 中断于流式中：usage 未到达不累计（R3）"_test = [] {
        auto mock = std::make_shared<MockHttpClient>();
        CLFAgentConfig config;
        config.m_apiKey = "k";
        config.m_stream = true;
        auto agent = std::make_unique<CLFAgentLoop>(config, mock);
        MockOutput out;
        agent->setOutput(&out);

        mock->hookAfterLine = 0;  // 第 1 行后中断，usage chunk 不会到达
        mock->onLineHook = [&] { out.fireInterrupt(); };
        mock->pushStream({
            "data: {\"choices\":[{\"delta\":{\"content\":\"a\"}}]}",
            "data: [DONE]"
        });

        std::string result = agent->runTurn("hi");
        expect(result == "[Interrupted]");
        expect(agent->getTotalTokensUsed() == 0);  // 落定规则：中断不累计
    };

    "T7 /resume 回显走折叠块：历史不进滚动区（P2-1）"_test = [] {
        auto dir = std::filesystem::temp_directory_path()
                 / ("clf_restore_test_" + std::to_string(
                        std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(dir);

        std::vector<CLFMessage> messages;
        messages.push_back({"user", "hello"});
        messages.push_back({"assistant", "world"});
        std::string path = CLFSessionManager::save(messages, dir.string(), false);

        auto mock = std::make_shared<MockHttpClient>();
        CLFAgentConfig config;
        config.m_apiKey = "k";
        auto agent = std::make_unique<CLFAgentLoop>(config, mock);
        MockOutput out;
        agent->setOutput(&out);

        bool ok = agent->restoreSession(path);
        expect(ok);

        // 历史内容不进滚动区（emitContent 直灌路径已废除）
        bool leaked = false;
        for (const auto& c : out.contents)
            if (c.find("hello") != std::string::npos
                || c.find("world") != std::string::npos)
                leaked = true;
        expect(!leaked);

        // 折叠块：摘要含消息计数、展开行含内容
        expect(out.foldedSummary.find("2 条消息") != std::string::npos);
        bool hasUser = false, hasAssistant = false;
        for (const auto& l : out.foldedLines) {
            if (l.find("> hello") != std::string::npos) hasUser = true;
            if (l.find("world") != std::string::npos) hasAssistant = true;
        }
        expect(hasUser);
        expect(hasAssistant);

        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    };

    "T6c 中断于工具执行中：恰好一条中断消息 + Warn"_test = [] {
        auto mock = std::make_shared<MockHttpClient>();
        CLFAgentConfig config;
        config.m_apiKey = "k";
        config.m_stream = false;
        auto agent = std::make_unique<CLFAgentLoop>(config, mock);
        MockOutput out;
        agent->setOutput(&out);

        CLFTool slowTool;
        slowTool.m_name = "slow_read";
        slowTool.m_risk = CLF::CLFCore::CLFToolRisk::Read;
        slowTool.m_handler = [](const std::string&) {
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            return "done";
        };
        agent->registerTool(slowTool);

        mock->pushResponse(R"({
            "choices": [{
                "message": {
                    "role": "assistant",
                    "content": null,
                    "tool_calls": [{
                        "id": "c1",
                        "type": "function",
                        "function": {"name": "slow_read", "arguments": "{}"}
                    }]
                },
                "finish_reason": "tool_calls"
            }]
        })");

        std::string result;
        std::thread runner([&] { result = agent->runTurn("go"); });
        std::this_thread::sleep_for(std::chrono::milliseconds(150));  // handler 执行中
        out.fireInterrupt();
        runner.join();

        expect(result == "[Interrupted]");
        expect(out.interruptEmissions() == 1);
        expect(out.kinds.back() == CLF::CLFTypes::ICLFOutput::StatusKind::Warn);
    };

    // ========== U 系列：m_todos 线程安全（2026-09-02，设计-任务清单UI显示 §3.9） ==========

    // 加锁前该测试在 MSVC 下为数据竞争（UB）：handler 工作线程写 ↔ 渲染线程读。
    // 加锁后 getTodos 锁内拷贝、setTodos 锁内替换，并发读写必须稳定不崩、快照完整
    "U1 getTodos/setTodos 并发读写不崩且快照完整"_test = [] {
        auto mock = std::make_shared<MockHttpClient>();
        auto agent = makeAgent(mock);
        agent->setTodos({{"1", "任务1", "pending"}, {"2", "任务2", "in_progress"}});

        std::atomic<bool> stop{false};
        std::thread writer([&] {
            int i = 0;
            while (!stop.load()) {
                // 模拟 handler 的"取副本 → 改 → 整体写回"模式（CLFBuiltinTools update 路径）
                auto todos = agent->getTodos();
                if (!todos.empty()) todos[0].m_status = (++i % 2) ? "completed" : "pending";
                agent->setTodos(std::move(todos));
            }
        });

        // 模拟 UI 渲染线程读：每帧一次快照拷贝，必须始终完整（2 项，无半截状态）
        for (int i = 0; i < 20000; ++i) {
            const auto snapshot = agent->getTodos();
            expect(snapshot.size() == 2_ul);
            expect(snapshot[0].m_content == std::string("任务1"));
        }

        stop.store(true);
        writer.join();
    };

    // ========== V 系列：jsonl 会话文件上下文（J3，设计-会话追加式保存.jsonl §3.9，2026-09-02） ==========

    "V1 beginSessionFile 建文件 + appendTurnLine 差集与快照字段"_test = [] {
        VSetup s;
        std::string path = s.agent->beginSessionFile("帮我查看项目");
        expect(!path.empty());
        expect(std::filesystem::exists(std::filesystem::u8path(path)));

        // 第一轮：无 todo 操作 → turn 行无 todos 字段
        s.pushStop("第一答");
        s.agent->runTurn("第一问");
        expect(s.agent->appendTurnLine() == path);

        auto lines = VSetup::readLines(path);
        expect(lines.size() == 2_ul);                 // header + turn
        expect(lines[0]["type"] == std::string("header"));
        expect(lines[0]["title"] == std::string("帮我查看项目"));
        expect(lines[1]["type"] == std::string("turn"));
        expect(lines[1]["messages"].size() == 2_ul);  // user + assistant
        expect(!lines[1].contains("todos"));          // 未操作 todo → 无快照字段

        // 第二轮：markTodosDirty + setTodos → turn 行带 todos 快照；轮末 m_todoDirty 清除。
        // ⚠️ 清单须非全完成（1✓1○）——否则会意外触发 T6 收尾（complete 行 + 面板置位），
        // 干扰"turn 行快照字段"断言（T6 行为由 V2 专门覆盖）
        s.agent->setTodos({{"1", "任务1", "completed"}, {"2", "任务2", "pending"}});
        s.agent->markTodosDirty();
        s.pushStop("第二答");
        s.agent->runTurn("第二问");
        s.agent->appendTurnLine();

        lines = VSetup::readLines(path);
        expect(lines.size() == 3_ul);
        expect(lines[2]["todos"].size() == 2_ul);
        expect(lines[2]["todos"][0]["status"] == std::string("completed"));
        expect(lines[2]["todos"][1]["status"] == std::string("pending"));

        // m_todoDirty 已清除 → 第三轮（不碰 todo）无 todos 字段
        s.pushStop("第三答");
        s.agent->runTurn("第三问");
        s.agent->appendTurnLine();
        lines = VSetup::readLines(path);
        expect(!lines[3].contains("todos"));
    };

    "V2 T6 全完成收尾：complete 行 + emit 收尾行 + 面板置位"_test = [] {
        VSetup s;
        MockOutput out;
        s.agent->setOutput(&out);
        s.agent->beginSessionFile("任务会话");
        s.agent->setTodos({{"1", "任务A", "completed"}, {"2", "任务B", "completed"}});

        s.pushStop("总结");
        s.agent->runTurn("完成吧");
        s.agent->appendTurnLine();

        expect(s.agent->isTodoPanelDone());   // 面板置位

        // 收尾行已 emit 到对话流
        bool found = false;
        for (const auto& c : out.contents)
            if (c.find("任务清单（全部完成）") != std::string::npos) found = true;
        expect(found);

        // 文件含 complete 行（在 turn 行之前——T6 在 runTurn 完成分支，appendTurnLine 在轮末）
        auto lines = VSetup::readLines(s.agent->getActiveSessionFile());
        bool hasComplete = false;
        for (const auto& l : lines)
            if (l.value("type", "") == "complete") hasComplete = true;
        expect(hasComplete);
    };

    "V2b 非全完成不触发收尾 + 中断残留补收尾"_test = [] {
        VSetup s;
        MockOutput out;
        s.agent->setOutput(&out);
        s.agent->beginSessionFile("任务会话");
        s.agent->setTodos({{"1", "任务A", "completed"}, {"2", "任务B", "pending"}});

        s.pushStop("未完成");
        s.agent->runTurn("继续");
        s.agent->appendTurnLine();

        expect(!s.agent->isTodoPanelDone());   // 面板保留
        bool found = false;
        for (const auto& c : out.contents)
            if (c.find("任务清单（全部完成）") != std::string::npos) found = true;
        expect(!found);

        // 中断残留场景（§八 补丁 5）：清单变全✓但上一轮未收尾 → 本轮完成分支补收尾
        s.agent->setTodos({{"1", "任务A", "completed"}, {"2", "任务B", "completed"}});
        s.pushStop("再来");
        s.agent->runTurn("再聊一轮");
        s.agent->appendTurnLine();
        expect(s.agent->isTodoPanelDone());
    };

    "V3 appendTodoSnapshotNow 即时写快照行（不等轮末）"_test = [] {
        VSetup s;
        s.agent->beginSessionFile("快照会话");
        s.agent->setTodos({{"1", "任务", "in_progress"}});
        s.agent->markTodosDirty();
        s.agent->appendTodoSnapshotNow();

        auto lines = VSetup::readLines(s.agent->getActiveSessionFile());
        expect(lines.size() == 2_ul);   // header + todo_snapshot（无 turn 行——未轮末）
        expect(lines[1]["type"] == std::string("todo_snapshot"));
        expect(lines[1]["todos"].size() == 1_ul);
        expect(lines[1]["todos"][0]["status"] == std::string("in_progress"));
    };

    "V4 restoreSession .jsonl 分流：m_resumedFrom 置位 + 面板按快照"_test = [] {
        // 造一个 jsonl 会话：最后快照非全完成（1 completed + 1 in_progress）
        auto dir = std::filesystem::temp_directory_path()
                 / ("clf_agent_v4_" + std::to_string(
                        std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(dir);
        std::string srcPath = dir.string() + "/2026-08-25_10-00-00_旧会话.jsonl";  // 纯字符串拼接（UTF-8 字节），避免 path 窄构造按 CP936 解码
        {
            std::ofstream f(std::filesystem::u8path(srcPath), std::ios::binary);
            f << CLFMessageCodec::serializeHeaderLine("旧会话", "t", "sid", "model") << "\n";
            std::vector<CLFMessage> turn{{"user", "问"}, {"assistant", "答"}};
            f << CLFMessageCodec::serializeTurnLine(turn, "ts-1") << "\n";
            std::vector<CLFTodoItem> todos{{"1", "任务1", "completed"},
                                           {"2", "任务2", "in_progress"}};
            f << CLFMessageCodec::serializeTodoSnapshot(todos, "ts-2") << "\n";
        }

        VSetup s;
        expect(s.agent->restoreSession(srcPath));
        expect(s.agent->getResumedFrom() == srcPath);   // 恢复即续写态
        expect(!s.agent->isTodoPanelDone());            // 非全完成 → 面板重现
        const auto todos = s.agent->getTodos();
        expect(todos.size() == 2_ul);
        expect(todos[0].m_status == std::string("completed"));
        expect(todos[1].m_status == std::string("in_progress"));

        // 全完成快照 → 面板不显示（置位）
        std::string donePath = dir.string() + "/2026-08-25_11-00-00_全完成.jsonl";
        {
            std::ofstream f(std::filesystem::u8path(donePath), std::ios::binary);
            f << CLFMessageCodec::serializeHeaderLine("全完成", "t", "sid", "model") << "\n";
            std::vector<CLFMessage> turn{{"user", "问"}, {"assistant", "答"}};
            f << CLFMessageCodec::serializeTurnLine(turn, "ts-1") << "\n";
            std::vector<CLFTodoItem> todos{{"1", "任务1", "completed"}};
            f << CLFMessageCodec::serializeTodoSnapshot(todos, "ts-2") << "\n";
        }
        VSetup s2;
        expect(s2.agent->restoreSession(donePath));
        expect(s2.agent->isTodoPanelDone());   // 全完成 → 不显示

        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    };

    "V5 beginSessionFile 续写复制：源文件冻结 + resumedFrom 清除"_test = [] {
        // 造源文件（header + turn 共 2 行）
        auto dir = std::filesystem::temp_directory_path()
                 / ("clf_agent_v5_" + std::to_string(
                        std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(dir);
        std::string srcPath = dir.string() + "/2026-08-25_09-00-00_源会话.jsonl";
        {
            std::ofstream f(std::filesystem::u8path(srcPath), std::ios::binary);
            f << CLFMessageCodec::serializeHeaderLine("源会话", "t", "sid", "model") << "\n";
            std::vector<CLFMessage> turn{{"user", "第一问"}, {"assistant", "第一答"}};
            f << CLFMessageCodec::serializeTurnLine(turn, "ts-1") << "\n";
        }

        VSetup s;
        s.agent->setResumedFrom(srcPath);   // 模拟 restoreSession 已置位
        std::string contPath = s.agent->beginSessionFile("继续做");

        expect(!contPath.empty());
        expect(contPath != srcPath);
        expect(contPath.find("续.jsonl") != std::string::npos);   // 续命名
        expect(s.agent->getResumedFrom().empty());                // 生命周期：创建后清除
        expect(s.agent->getActiveSessionFile() == contPath);

        // 续文件 = 源文件全部行原样复制（header 原样——session_id 延续）
        auto srcLines  = VSetup::readLines(srcPath);
        auto contLines = VSetup::readLines(contPath);
        expect(contLines.size() == srcLines.size());
        expect(contLines[0]["session_id"] == std::string("sid"));
        expect(contLines[1]["messages"].size() == 2_ul);

        // 源文件冻结不变（续写后的轮次只写续文件）
        s.pushStop("续答");
        s.agent->runTurn("续问");
        s.agent->appendTurnLine();
        expect(VSetup::readLines(srcPath).size() == 2_ul);       // 源文件仍 2 行
        expect(VSetup::readLines(contPath).size() == 3_ul);      // 续文件 +1 行

        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    };
};

// Boost.UT：测试在静态初始化时注册，cfg 析构时自动运行并输出报告
int main() {
    return 0;
}
