// ICLFOutput.hpp — Agent → UI 输出抽象接口
// 定义 Agent Core 与 UI 之间的合同, CLFCore 仅依赖此接口, 不依赖 CLFUI
//
// 设计原则:
//   - 零项目依赖 (仅 stdlib)
//   - 纯虚接口 (MockOutput 可替换 CLFTerminal)
//   - 中断信号驱动 (非轮询)
//   - 瞬时状态统一槽 (覆盖式, 防冲突)
//   - 思考内容分离 (appendThinking/clearThinking, 与 emitContent 分通道)

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace CLF::CLFTypes {

// ============================================================================
// ICLFOutput — Agent → UI 输出抽象 (6 个方法, 5 组)
// ============================================================================
class ICLFOutput {
public:
    virtual ~ICLFOutput() = default;

    // ========== ① 内容输出 ==========

    // 永久内容 (进滚动区): AI 回复、代码块、最终答案
    virtual void emitContent(const std::string& text) = 0;

    // 子进程原始透传: 不加前缀, 不破坏 ANSI 进度条.
    // [保留钩子] 当前无调用方, 待子进程输出路径接入.
    virtual void emitRaw(const std::string& data) = 0;

    // ========== ② 瞬时状态 (统一状态槽, 覆盖式) ==========
    // cur=-1 total=-1 → 无步骤进度
    // cur>=0 total>0  → 带进度
    // title=""        → 清除状态
    virtual void setStatus(const std::string& title,
                           int current = -1, int total = -1) = 0;

    // ========== ③ 交互 ==========

    // 高风险确认 — 阻塞, 期间禁止状态刷新
    virtual bool confirm(const std::string& prompt) = 0;

    // ========== ④ 中断机制 (信号驱动, 非轮询) ==========

    // 注册中断回调 (后注册覆盖前注册). 传入 nullptr 清空回调.
    // 回调在 UI 线程触发 — 实现方必须保证线程安全 (atomic + mutex).
    // AgentLoop 应在 setOutput 时注册, 析构时传入 nullptr 清空.
    virtual void onInterrupt(std::function<void()> callback) = 0;

    // ========== ⑤ 异常通道 ==========

    // 错误输出 — UI 标红, 不影响正常输出流, Agent 继续运行
    virtual void emitError(const std::string& message) = 0;

    // ========== ⑥ 思考内容（与 emitContent 分通道，UI 层可折叠） ==========

    // 追加推理过程文本（不在主内容区显示，由 UI 层 Ctrl+O 展开）
    virtual void appendThinking(const std::string& text) = 0;

    // 清空当前 turn 的推理内容（ESC 中断 / 新 turn 开始时调用）
    virtual void clearThinking() = 0;
};

} // namespace CLF::CLFTypes
