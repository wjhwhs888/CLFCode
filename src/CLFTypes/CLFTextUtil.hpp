// CLFTextUtil.hpp — 公共文本工具（basic 层，clf_types 目标；批次 A2，2026-09-03）
// 全仓收敛：UTF-8 安全截断 / CJK 显示宽度 / 换行拆分 / 本地时间 / token 估算
//
// 收敛来源（设计-阶段1 §五 A2 + 边界清单 A2-1~A2-4 取证）：
//   utf8SafeHead/Tail ← 全仓 16+ 处截断（安全版 7 + 字节级 9；取证：无精确字节场景）
//   charWidth/displayWidth/substrByWidth ← SelectionModel/Terminal 两套等价实现
//   splitLines ← AgentLoop/Terminal 换行拆分
//   localNow/localNowTm ← 7 处时间戳 ifdef（含 Builder 唯一裸 localtime）
//   estimateTokenChars/estimateTokensForMessage ← Context/Builder 同公式双实现
//
// example:
//   std::string t = CLFTextUtil::utf8SafeHead(longText, 80);   // 不劈半多字节
//   int w = CLFTextUtil::displayWidth("中文abc");               // 2+2+1+1+1 = 7
//   std::string ts = CLFTextUtil::localNow("%Y-%m-%d %H:%M:%S");

#pragma once

#include <string>
#include <vector>

#include "CLFTypes/CLFTypes.hpp"

namespace CLF::CLFCore {

class CLFTextUtil {
public:
    // ============ UTF-8 截断 ============

    // 头部安全截断：maxBytes 处回退到多字节字符边界（不劈半），追加 ellipsis
    static std::string utf8SafeHead(const std::string& text, size_t maxBytes,
                                    const std::string& ellipsis = "…");
    // 尾部安全截断：保留末尾 maxBytes（先回退到边界），前缀 ellipsis
    // （extractKeyParam path 分支"..."+尾 52 字符的语义）
    static std::string utf8SafeTail(const std::string& text, size_t maxBytes,
                                    const std::string& ellipsis = "…");

    // ============ CJK 显示宽度（A2-2：两套等价实现收敛）============

    // 单字节显示宽度：ASCII=1；UTF-8 多字节首字节=2（CJK/全角）；
    // 续字节=0。共同局限（与旧实现一致）：emoji/组合字符计 2
    static int  charWidth(unsigned char c);
    static int  displayWidth(const std::string& s);
    // 按显示宽度切分（不劈半多字节字符）；maxW<=0 返回原串
    static std::string substrByWidth(const std::string& s, int maxW);

    // ============ 通用字符串替换 ============

    // 全量替换所有 from → to（A2 自 CLFSystemPromptBuilder 归位）
    static std::string replaceAll(std::string s, const std::string& from,
                                  const std::string& to);

    // ============ 换行拆分 ============

    // 按 \n 拆分；尾空段不保留（"a\n" → ["a"]，与三处旧实现一致）；
    // keepEmpty=true 时空文本产出单个空段（旧 appendSplitLines 特判语义）
    static std::vector<std::string> splitLines(const std::string& text,
                                               bool keepEmpty);

    // ============ 本地时间（线程安全）============

    // strftime 格式化当前本地时间（内部 localtime_s/_r；替代 7 处平台 ifdef）
    static std::string localNow(const char* fmt);
    // 当前本地时间 tm 结构（字段读取场景，如时段判断；替代裸 localtime）
    static std::tm localNowTm();

    // ============ token 估算（P1-13：双实现统一）============

    // 简单估算：ascii/4 + 非 ASCII 字符×3/2（每多字节序列首字节计 1 个非 ASCII）
    static int estimateTokenChars(const std::string& text);
    // 单条消息 token 估算（content + tool_calls 的 arguments/id/name）
    static int estimateTokensForMessage(const CLFMessage& msg);
};

} // namespace CLF::CLFCore
