// CLFSessionReplayer.hpp — 会话恢复事件重放器（事件化重构分册 2 §3.2，2026-10-13）
//
// 定位：resume 回显从"UI 读文件独立解析"改为"后端重放事件流、UI 走与
// 正常对话同一渲染路径"（用户定调："理论上应该是与正式请求后端接收信息
// 是一个流程"）——消灭两套显示逻辑。
//
// 流程：/resume 选择 → restoreSession 重建 context（数据层）→ replay 读
// jsonl 逐行解析 → History* 事件重放（文件行序 = seq 序）→ 适配器按
// History* 渲染（历史样式折叠块）→ ResumeDone 结束回显。
//
// 老会话兼容（设计 2.4，硬约束）：
//   - 装饰剥离：worked 行（✻ 前缀精确格式）与 📋 收尾段行级精确匹配剥除
//     （只剥程序已知格式，模型内容合法 ✻/📋 不动；只影响回显不改原文件）
//   - 老文件无 event_log：从 turn 行内 tool 消息提取兜底（差异收敛在此）
//   - 老文件无 turnId：回显时生成临时 id（仅显示用）
//
// 副作用防护（天然成立）：History* 不在 runTurn 消费路径；writer 只订阅
// live 语义帧不回写；重放不可能触发 API/工具。
//
// example:
//   CLFSessionReplayer::replay(jsonlPath, bus);

#pragma once

#include <cstddef>
#include <string>

namespace CLF::CLFTypes { class CLFEventBus; }

namespace CLF::CLFCore {

class CLFSessionReplayer {
public:
    // 重放 jsonl → History* 事件（publish 到 bus；bus 空 = no-op）。
    // 返回重放事件数；解析失败行跳过（warn 不抛——未知类型静默跳过安全网）
    static size_t replay(const std::string& jsonlPath,
                         CLF::CLFTypes::CLFEventBus* bus);
};

} // namespace CLF::CLFCore
