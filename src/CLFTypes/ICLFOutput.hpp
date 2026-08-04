// ICLFOutput.hpp — Agent → UI 输出抽象接口
// 定义 Agent Core 与 UI 之间的合同, CLFCore 仅依赖此接口, 不依赖 CLFUI
//
// 设计原则:
//   - 零项目依赖 (仅 stdlib)
//   - 纯虚接口 (MockOutput 可替换 CLFTerminal)
//   - 中断信号驱动 (非轮询)
//   - 瞬时状态统一槽 (覆盖式, 防冲突)

#pragma once

#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace CLF::CLFTypes {

// ============================================================================
// 中断异常 — 用户按 ESC 时抛出, 冒泡到 Agent 主循环捕获
// ============================================================================
class InterruptError : public std::runtime_error {
public:
    InterruptError() : std::runtime_error("User interrupted") {}
};

// ============================================================================
// ICLFOutput — Agent → UI 输出抽象 (13 个方法, 6 组)
// ============================================================================
class ICLFOutput {
public:
    virtual ~ICLFOutput() = default;

    // ========== ① 内容输出 ==========

    // 永久内容 (进滚动区): AI 回复、代码块、最终答案
    virtual void emitContent(const std::string& text) = 0;

    // 子进程原始透传: 不加前缀, 不破坏 ANSI 进度条.
    // 实现方应自动清状态进入原始模式 (调用方无需关心互斥细节).
    // 第一期: 暂透传至 scrollPrint, 不处理 ANSI 进度条互斥.
    virtual void emitRaw(const std::string& data) = 0;

    // ========== ② 瞬时状态 (统一状态槽, 覆盖式) ==========
    // cur=-1 total=-1 → 无步骤进度 (等效旧 showWorking)
    // cur>=0 total>0  → 带进度 (等效旧 beginStep)
    // title=""        → 清除状态 (等效旧 clearStatus)
    virtual void setStatus(const std::string& title,
                           int current = -1, int total = -1) = 0;

    // ========== ③ 工具反馈 ==========

    virtual void onToolCall(const std::string& name, const std::string& params) = 0;
    virtual void onToolResult(const std::string& name, const std::string& result, bool ok) = 0;

    // ========== ④ 交互 (调用前必须 setStatus("") 清状态!) ==========

    // 高风险确认 — 阻塞, 期间禁止状态刷新
    virtual bool confirm(const std::string& prompt) = 0;
    // 多选 — CLI 用数字索引 [1]A [2]B; 返回 -1 表示取消
    virtual int askSelect(const std::vector<std::string>& options,
                          const std::string& prompt) = 0;
    // 自由文本输入 — nullopt 表示取消, "" 表示用户真输入了空串
    virtual std::optional<std::string> askInput(
        const std::string& prompt,
        const std::string& placeholder = "") = 0;

    // ========== ⑤ 中断机制 (信号驱动, 非轮询) ==========

    // 注册中断回调 (后注册覆盖前注册). 传入 nullptr 清空回调.
    // 回调在 UI 线程触发 — 实现方必须保证线程安全 (atomic + mutex).
    // AgentLoop 应在 setOutput 时注册, 析构时传入 nullptr 清空.
    virtual void onInterrupt(std::function<void()> callback) = 0;

    // ========== ⑥ 异常通道 ==========

    // 错误输出 — UI 标红, 不影响正常输出流, Agent 继续运行
    virtual void emitError(const std::string& message) = 0;
    // 请求关闭 — 通知 UI 层准备退出, 由 AgentLoop 决定是否真退出
    virtual void requestShutdown(const std::string& reason) = 0;
};

} // namespace CLF::CLFTypes
