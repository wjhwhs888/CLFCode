// ICLFOutput.hpp — Agent → UI 输出抽象接口
// 定义 Agent Core 与 UI 之间的合同, CLFCore 仅依赖此接口, 不依赖 CLFUI
//
// 设计原则:
//   - 零项目依赖 (仅 stdlib)
//   - 纯虚接口 (MockOutput 可替换 CLFTerminal)
//   - 中断信号驱动 (非轮询)
//   - 瞬时状态统一槽 (覆盖式, 防冲突)
//   - 思考内容分离 (appendThinking/clearThinking, 与 emitContent 分通道)
//
// C3 拆窄（2026-09-07，P1-2 深化，边界清单 C3-1 归位表）：
// 17 方法归四窄接口（内容/进度状态/交互/辅助），ICLFOutput 收窄为聚合接口
// （仅继承，不再新增方法）。消费者按最小集依赖窄接口（ISP）：
//   CLFPasserby → ICLFContentOutput（emitStyledLine）
//   CLFThinkingIndicator → ICLFProgressOutput（setStatus）
//   CLFTipsBar → ICLFProgressOutput（activityCount）
//   CLFToolExecutor → Content + Progress（5 方法）
//   CLFAgentLoop / CLFRepl 保持宽 ICLFOutput（全通道编排 / 装配点）
//
// 扩展纪律（B5 固化，窄接口层面继续生效）：新增方法一律带默认实现
// （空操作或基类安全行为）——保持既有 MockOutput / 测试最小实现零破坏；
// 纯虚仅用于新实现类必须提供的核心通道。

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace CLF::CLFTypes {

// ============================================================================
// ① ICLFContentOutput — 内容通道（滚动区/折叠块/原始透传/样式行）
// ============================================================================

class ICLFContentOutput {
public:
    virtual ~ICLFContentOutput() = default;

    // 永久内容 (进滚动区): AI 回复、代码块、最终答案
    virtual void emitContent(const std::string& text) = 0;

    // 子进程原始透传: 不加前缀, 不破坏 ANSI 进度条.
    // [保留钩子] 当前无调用方, 待子进程输出路径接入.
    virtual void emitRaw(const std::string& data) = 0;

    // 带样式的行输出（用于 diff 着色：+ 绿 / - 红 / 上下文 灰）
    enum class LineStyle { Normal, Add, Remove, Context };
    virtual void emitStyledLine(const std::string& line, LineStyle style) = 0;

    // 恢复回显折叠块（P2-1，带默认实现，Mock 零破坏）：
    // summary 常显折叠行，lines 为展开内容（UI 层快捷键切换展开态）
    virtual void showFoldedBlock(const std::string& summary,
                                 const std::vector<std::string>& lines) {
        (void)summary;
        (void)lines;
    }
};

// ============================================================================
// ② ICLFProgressOutput — 渐进状态通道（状态槽/进度块/状态点/刷新/活动计数）
// ============================================================================

class ICLFProgressOutput {
public:
    virtual ~ICLFProgressOutput() = default;

    // 瞬时状态 (统一状态槽, 覆盖式)
    // cur=-1 total=-1 → 无步骤进度
    // cur>=0 total>0  → 带进度
    // title=""        → 清除状态
    virtual void setStatus(const std::string& title,
                           int current = -1, int total = -1) = 0;

    // 仅更新 status 文本，不触发 refresh（高频更新用，由 emitContent 顺带刷新）
    virtual void setStatusTextOnly(const std::string& title) = 0;

    // 状态点种类（渲染层着色显示：None/Running/Done/Warn/Error）
    // 与 setStatus 文本通道独立，互不联动——调用方需显式清各自通道
    enum class StatusKind { None, Running, Done, Warn, Error };
    virtual void setStatusKind(StatusKind kind) { (void)kind; }

    // 展示可替换进度块（不进滚动区），每次调用替换上一次。空 vector 清除。
    virtual void showProgress(const std::vector<std::string>& lines) = 0;

    // 清除进度块，并将 summary 写入永久滚动区
    virtual void finishProgress(const std::string& summary) = 0;

    // 主动刷新请求（CLFTerminal 覆写为 PostEvent(Custom)）
    // turnTimer 1Hz 驱动用：修复工具执行期界面冻结 + 动画最低帧率
    virtual void requestRefresh() {}

    // 输出活动计数（A5 Tips 静默判定数据源，基类实现零破坏）：
    // 由具体实现的内容类输出入口调用（模型活动信号）。
    // 刻意不计状态行/刷新（setStatus/setStatusTextOnly/requestRefresh 等
    // UI 自身节奏）——turnTimer 每秒驱动状态行，计入会使静默计时永不触发
    void notifyActivity() { ++m_activityCount; }

    // 活动计数（TipsBar ticker 读取；原子，跨线程安全）
    uint64_t activityCount() const { return m_activityCount.load(); }

private:
    std::atomic<uint64_t> m_activityCount{0};
};

// ============================================================================
// ③ ICLFInteractionOutput — 交互通道（确认/中断注册）
// ============================================================================

class ICLFInteractionOutput {
public:
    virtual ~ICLFInteractionOutput() = default;

    // 高风险确认 — 阻塞, 期间禁止状态刷新
    virtual bool confirm(const std::string& prompt) = 0;

    // 注册中断回调 (后注册覆盖前注册). 传入 nullptr 清空回调.
    // 回调在 UI 线程触发 — 实现方必须保证线程安全 (atomic + mutex).
    // AgentLoop 应在 setOutput 时注册, 析构时传入 nullptr 清空.
    virtual void onInterrupt(std::function<void()> callback) = 0;
};

// ============================================================================
// ④ ICLFAuxOutput — 辅助通道（错误/思考）
// ============================================================================

class ICLFAuxOutput {
public:
    virtual ~ICLFAuxOutput() = default;

    // 错误输出 — UI 标红, 不影响正常输出流, Agent 继续运行
    virtual void emitError(const std::string& message) = 0;

    // 追加推理过程文本（不在主内容区显示，由 UI 层 Ctrl+O 展开）
    virtual void appendThinking(const std::string& text) = 0;

    // 清空当前 turn 的推理内容（ESC 中断 / 新 turn 开始时调用）
    virtual void clearThinking() = 0;
};

// ============================================================================
// ICLFOutput — 聚合接口（C3：仅继承四窄接口，不再新增方法）
// 17 方法 10 通道 = 4 内容 + 8 进度状态 + 2 交互 + 3 辅助（12 纯虚 +
// 3 默认 + 2 非虚，各归其窄接口）。全通道消费者（AgentLoop 编排、
// Repl 装配）与实现类（CLFTerminal）依赖此聚合；单通道消费者按
// ISP 依赖对应窄接口。
// ============================================================================

class ICLFOutput : public ICLFContentOutput,
                   public ICLFProgressOutput,
                   public ICLFInteractionOutput,
                   public ICLFAuxOutput {
public:
    ~ICLFOutput() override = default;
};

} // namespace CLF::CLFTypes
