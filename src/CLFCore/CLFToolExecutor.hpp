// CLFToolExecutor.hpp — 工具调用执行器
// 负责工具查表、安全策略检查、用户确认、结果格式化显示
//
// 事件化重构分册 3（§3.1）：瘦身为单工具三段——prepare（串行闸门：
// 查找/确认/守卫/中断检查）→ run（唯一并发段：handler 调用）→
// finalize（提交游标内：归一化/显示/统计/事件）。调度循环拆至
// CLFToolScheduler（持有本类引用）。execute 保留为串行兼容壳
// （maxParallel=1 调度——既有调用点/串行等价回归零行为变化）。
//
// 批次接口 beginBatch/finishBatch：进度 guard + summary + 统计写回。
// 单线程批次假设：同一时刻至多一个批次在跑（AgentLoop 单线程驱动
// schedule）——批次状态用成员承载，注释钉死不变量。
//
// example:
//   CLFToolExecutor executor(tools, securityPolicy, confirmCb);
//   auto results = executor.execute(toolCalls);           // 兼容壳（串行）
//   // 或由调度器驱动（分册 3 主体）：
//   CLFToolScheduler scheduler(executor, maxParallel);
//   auto r = scheduler.schedule(toolCalls);

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "CLFTypes/CLFTypes.hpp"
#include "CLFTypes/ICLFOutput.hpp"
#include "CLFCore/CLFSecurityPolicy.hpp"

namespace CLF::CLFPluginApi { class ICLFFileService; }
namespace CLF::CLFTypes {
class ICLFContentOutput;
class ICLFProgressOutput;
class CLFEventBus;
}
namespace CLF::CLFCore {

struct ToolStats;
class CLFToolScheduler;

class CLFToolExecutor {
public:
    // C3：依赖窄化——5 方法跨内容+进度两通道，收两窄指针替代宽 ICLFOutput
    CLFToolExecutor(std::vector<CLFTool>& tools,
                    CLFSecurityPolicy& policy,
                    std::function<bool(const std::string&)> confirmCallback,
                    ToolStats& stats,
                    CLF::CLFPluginApi::ICLFFileService* fileService,
                    CLF::CLFTypes::ICLFContentOutput* contentOutput = nullptr,
                    CLF::CLFTypes::ICLFProgressOutput* progressOutput = nullptr,
                    std::atomic<bool>* interruptFlag = nullptr,
                    const CLFTimerLabels* labels = nullptr,
                    std::atomic<int>* thinkingSec = nullptr,
                    // 事件化重构步骤 2：非空时发射 ToolStarted/ToolFinished
                    // （双轨——直调保留；nullptr = 不发射，既有调用点零改动）
                    CLF::CLFTypes::CLFEventBus* eventBus = nullptr);
    ~CLFToolExecutor();   // cpp 定义（unique_ptr<BatchProgressGuard> 需完整类型）
    CLFToolExecutor(CLFToolExecutor&&) = default;   // 引用成员可移（= 引用拷贝）；
                                                    // 拷贝构造保持删除（不可拷贝）

    // ==================== 单工具三段（调度器驱动，分册 3 §3.1） ====================

    // prepare 产物——run/finalize 共享的执行上下文。
    // toolIndex = m_tools 下标（prepare 定位后固定；dispatch 期间注册表
    // 结构稳定——插件热卸载语义为 handler 兜底，不缩容）。
    // Write 预览摘要：TOCTOU/渲染在 prepare 完成，finalize 仅需 diff 行
    // 显示所需标量（WritePreview 结构保持 cpp 内部）。
    struct PreparedCall {
        size_t toolIndex = 0;
        std::string keyParam;
        bool useProgressive = false;
        bool isWriteTool = false;
        std::string previewFilePath;
        bool previewHasDiffLines = false;
        bool previewDiffTruncated = false;
        int  previewAdded = 0;
        int  previewRemoved = 0;
        int  previewHunks = 0;
    };

    // prepare 结果三态：
    //   Ready    —— 闸门通过，可 dispatch（run）
    //   Fallback —— 合成失败结果（not_found/blocked/denied/预览失败等），
    //               不 dispatch，照常走提交（finalize 发 ToolFinished）
    //   Aborted  —— 中断，调度器停止补充并为剩余补"未执行"结果
    struct PrepareOutcome {
        enum class Kind { Ready, Fallback, Aborted };
        Kind kind = Kind::Ready;
        CLFToolResult fallback;   // Fallback 时的合成结果
        std::string errorKind;    // Fallback 时 ToolFinished 的 errorKind
    };

    // run 产物：handler 原始结果 + 异常标志（ok 判定在 finalize 的
    // formatToolResult——run 并发段零 UI/事件/统计）。
    struct RunOutcome {
        CLFToolResult result;
        bool exceptionCaught = false;
        std::string displayError;   // 异常时的截断展示文本（100 字符内）
    };

    // finalize 产物：提交结果 + ok（false = handler 失败/异常——调度器
    // 失败静默信号 §3.6；Fallback 恒 true 不触发静默）
    struct FinalizeOutcome {
        CLFToolResult result;
        bool ok = true;
    };

    PrepareOutcome prepare(const CLFToolCall& call, PreparedCall& pc);
    RunOutcome run(const CLFToolCall& call, const PreparedCall& pc);
    FinalizeOutcome finalize(const CLFToolCall& call, const PreparedCall& pc,
                             RunOutcome&& raw, bool isFallback,
                             const std::string& errorKind);

    // ==================== 批次接口（schedule 驱动） ====================
    // beginBatch：进度 guard 激活 + 统计快照（m_stats 会话累计为基底）
    void beginBatch();
    // finishBatch：summary 提交 + m_stats 写回（中断/失败路径同样调用——
    // 与现状"break 后仍提交 summary"语义一致）
    void finishBatch(int totalCalls);

    // ==================== 调度器支撑 ====================
    bool isInterrupted() const { return m_interruptFlag && m_interruptFlag->load(); }
    // 工具查找（调度器分类用）；返回指针仅限调用方即时消费（不存）
    const CLFTool* findTool(const std::string& name) const;

    // 兼容壳：串行调度（maxParallel=1——零行为变化承诺；第三批 AgentLoop
    // 切轨 CLFToolScheduler 后，此壳仅剩串行等价回归用途）
    std::vector<CLFToolResult> execute(const std::vector<CLFToolCall>& calls);

private:
    // 批次进度 guard：commit(summary) 正常提交；析构兜底 finishProgress("")
    // （schedule 异常逃逸路径——与现状 ProgressGuard 同语义）
    struct BatchProgressGuard {
        CLF::CLFTypes::ICLFProgressOutput* out;
        bool committed = false;
        explicit BatchProgressGuard(CLF::CLFTypes::ICLFProgressOutput* o)
            : out(o) {}
        ~BatchProgressGuard() { if (!committed && out) out->finishProgress(""); }
        void commit(const std::string& s) { committed = true; if (out) out->finishProgress(s); }
    };

    void emitToolFinished(const CLFToolResult& r, bool ok,
                          const std::string& errorKind);

    std::vector<CLFTool>& m_tools;
    CLFSecurityPolicy& m_securityPolicy;
    std::function<bool(const std::string&)> m_confirmCallback;
    ToolStats& m_stats;
    CLF::CLFTypes::ICLFContentOutput*  m_contentOutput = nullptr;   // C3 窄指针
    CLF::CLFTypes::ICLFProgressOutput* m_progressOutput = nullptr;  // C3 窄指针
    std::atomic<bool>* m_interruptFlag;
    const CLFTimerLabels* m_labels = nullptr;
    std::atomic<int>* m_thinkingSec = nullptr;
    CLF::CLFPluginApi::ICLFFileService* m_fileService = nullptr;
    CLF::CLFTypes::CLFEventBus* m_eventBus = nullptr;   // 步骤 2 双轨发射

    // ---- 批次状态（beginBatch 重置；单线程批次假设——§3.4 提交游标/
    // 串行闸门内执行，天然串行安全）----
    std::unique_ptr<BatchProgressGuard> m_batchGuard;
    int m_batchSearchCount = 0;      // 会话累计快照基底
    int m_batchReadCount = 0;
    int m_batchProgressReads = 0;    // 批次内读类计数（summary）
    int m_batchProgressEdits = 0;    // 批次内写类计数
    std::vector<std::string> m_batchDetailLines;   // 详情行（summary 附加）
};

} // namespace CLF::CLFCore
