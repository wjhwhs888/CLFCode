// CLFToolScheduler.hpp — 并发工具调度器（事件化重构分册 3，2026-10-13）
//
// 定位（设计 §3.1）：只做调度（分组/并发/归序）——工具执行体复用
// CLFToolExecutor 的 handler 调用逻辑。混合调度定案（dsh §3）：
//   连续并行安全调用并发执行；独占调用串行且为屏障（屏障持续到提交完成）。
//
// 四阶段（dsh §4）：prepare（串行闸门：确认/守卫/中断检查）→ dispatch
// （唯一并发段，std::async，上限 maxParallelToolCalls）→ finalize（串行：
// 归一化/摘要/提交游标）。
//
// 结果顺序：提交游标沿模型序连续推进（队首未完成，后面已完成的必须等——
// dsh §4.3 head-of-line）。
//
// 分类（fail-closed）：tool.m_isConcurrencySafe 为空 / 异常 / 非严格 true
// → Exclusive（所有现工具初始全部独占——声明一个放开一个）。
//
// example:
//   CLFToolScheduler sched(executor, maxParallel);
//   auto r = sched.schedule(calls);

#pragma once

#include <vector>

#include "CLFTypes/CLFTypes.hpp"

namespace CLF::CLFCore {

// 调度分类（对齐 dsh executionMode）
enum class ToolExecMode { Parallel, Exclusive };

// 分类函数：m_isConcurrencySafe 为空 → Exclusive（fail-closed）；
// 调用返回非严格 true（含抛异常，catch 吞）→ Exclusive；严格 true → Parallel
ToolExecMode classifyMode(const CLFTool& tool, const std::string& argsJson);

} // namespace CLF::CLFCore
