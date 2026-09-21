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

    // ============ 渲染宽度（2026-09-20 拖选列偏移根因修复）============

    // 渲染层码点宽度：与 FTXUI g_full_width_characters 同表（wcwidth 区间
    // 表）——FTXUI 按此表布局列，选区列→字节换算必须同表，否则含
    // ⎿(U+23BF)/●/❯ 等"非宽多字节符号"的行点击列偏移 1（2026-09-20
    // 用户实机取证：'⎿ 配置: …' 行点击 k 命中左侧 e——charWidth 恒计 2
    // 而 FTXUI 渲染 1 宽）。displayWidth/substrByWidth 保持 charWidth 的
    // 项目规则口径（❯ 计 2 等 qa 钉子语义）不改——渲染视觉不受影响
    // （FTXUI 布局一直按本表），仅选区换算对齐渲染。
    static int renderCharWidth(const std::string& s, size_t pos);
    // pos 处 UTF-8 字符的字节长度（1-4；非法首字节兜底 1）
    static size_t utf8CharLen(const std::string& s, size_t pos);

    // ============ 消息内容截断 ============

    // tool result 消息内容截断（C2b 自 CLFContext 移出——容器不再含内容策略）：
    // 超 8000 字符 → utf8SafeHead + 截断标记；未超 → 原样返回
    static std::string truncateToolResult(const std::string& content);

    // ============ 通用字符串替换 ============

    // 全量替换所有 from → to（A2 自 CLFSystemPromptBuilder 归位）
    static std::string replaceAll(std::string s, const std::string& from,
                                  const std::string& to);

    // ============ 换行拆分 ============

    // 按 \n 拆分；尾空段不保留（"a\n" → ["a"]，与三处旧实现一致）；
    // keepEmpty=true 时空文本产出单个空段（旧 appendSplitLines 特判语义）
    static std::vector<std::string> splitLines(const std::string& text,
                                               bool keepEmpty);

    // 行范围切片（2.2a 自 CLFBuiltinTools 归位）：offset 为 0 基起始行，
    // limit<=0 取到末尾；offset<=0 且 limit<=0 → 原样返回
    static std::string sliceLines(const std::string& content, int offset, int limit);

    // ============ 路径边界判定 ============

    // 路径是否位于工作区（2.3 自 CLFCapabilities 归位——插件不可链 core，
    // 工作区根由调用方传入）：weakly_canonical 跟随 symlink/junction 防软链接
    // 逃逸；逐段比较而非字符串前缀（防 "proj-evil" 误判在 "proj" 内）。
    // workspaceRootUtf8 为空串 = 跳过校验（返回 true）
    static bool isWithinWorkspaceOf(const std::string& workspaceRootUtf8,
                                    const std::string& path, std::string& outError);

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
