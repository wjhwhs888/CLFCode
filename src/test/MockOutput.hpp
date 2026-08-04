// MockOutput.hpp — ICLFOutput 测试替身, 记录所有调用序列
// 用于验证 AgentLoop/ToolExecutor 的输出行为

#pragma once

#include "CLFTypes/ICLFOutput.hpp"
#include <string>
#include <vector>

struct OutputCall {
    std::string method;
    std::string arg1;
    std::string arg2;
    int    argInt1 = 0;
    int    argInt2 = 0;
    bool   argBool = false;
};

class MockOutput : public CLF::CLFTypes::ICLFOutput {
public:
    std::vector<OutputCall> calls;

    void emitContent(const std::string& t) override {
        calls.push_back({"emitContent", t, "", 0, 0, false});
    }
    void emitRaw(const std::string& d) override {
        calls.push_back({"emitRaw", d, "", 0, 0, false});
    }
    void setStatus(const std::string& title, int cur, int total) override {
        calls.push_back({"setStatus", title, "", cur, total, false});
    }
    void onToolCall(const std::string& n, const std::string& p) override {
        calls.push_back({"onToolCall", n, p, 0, 0, false});
    }
    void onToolResult(const std::string& n, const std::string& r, bool ok) override {
        calls.push_back({"onToolResult", n, r, 0, 0, ok});
    }
    bool confirm(const std::string& prompt) override {
        calls.push_back({"confirm", prompt, "", 0, 0, false});
        return true;
    }
    int askSelect(const std::vector<std::string>& opts, const std::string& prompt) override {
        calls.push_back({"askSelect", prompt, "", (int)opts.size(), 0, false});
        return 0;
    }
    std::optional<std::string> askInput(const std::string& prompt, const std::string& def) override {
        calls.push_back({"askInput", prompt, def, 0, 0, false});
        return def.empty() ? std::nullopt : std::optional<std::string>(def);
    }
    void onInterrupt(std::function<void()> cb) override {
        calls.push_back({"onInterrupt", "", "", 0, 0, false});
        m_interruptCb = std::move(cb);
    }
    void emitError(const std::string& m) override {
        calls.push_back({"emitError", m, "", 0, 0, false});
    }
    void requestShutdown(const std::string& r) override {
        calls.push_back({"requestShutdown", r, "", 0, 0, false});
    }

    // 模拟中断触发
    void simulateInterrupt() { if (m_interruptCb) m_interruptCb(); }

    // 断言辅助
    bool called(const std::string& method) const {
        for (auto& c : calls) if (c.method == method) return true;
        return false;
    }
    int countOf(const std::string& method) const {
        int n = 0;
        for (auto& c : calls) if (c.method == method) ++n;
        return n;
    }

private:
    std::function<void()> m_interruptCb;
};
