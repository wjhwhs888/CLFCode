// CLFTextUtil.cpp — 公共文本工具实现（basic 层，clf_types；批次 A2）
// ⚠ 收敛纪律：各函数语义与替换点旧实现逐处对照（A2-1~A2-4 取证），
// 输出格式零变化（尤其 localNow 的 fmt 由调用点传原格式串）

#include "CLFTypes/CLFTextUtil.hpp"

#include <chrono>
#include <ctime>

namespace CLF::CLFCore {

namespace {

// 回退到 UTF-8 字符边界（跳过续字节 0x80-0xBF）
size_t backToCharBoundary(const std::string& s, size_t pos) {
    while (pos > 0 && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80) --pos;
    return pos;
}

// 前进到 UTF-8 字符边界（跳过续字节）
size_t forwardToCharBoundary(const std::string& s, size_t pos) {
    while (pos < s.size() && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80) ++pos;
    return pos;
}

// ANSI 转义序列跳过（宽度计算用）：\033[ 起、参数/中间字节 0x20-0x3F、终止字节
// 0x40-0x7E（SGR 序列 "\033[36m" 即 [ 后参数 36、终止 m）。转义不占显示宽——
// 否则含颜色包装的行（"● CLFCode:"/"❯ 输入"）硬换行点提前、选区坐标错位。
// 完整序列才跳过；不完整/伪造序列按普通字符处理。
// 返回是否跳过；跳过时 i 已推进到终止字节之后（两个调用点均为 while 无自增循环）
bool skipAnsiEscape(const std::string& s, size_t& i) {
    if (s[i] != '\033' || i + 1 >= s.size() || s[i + 1] != '[') return false;
    size_t j = i + 2;
    while (j < s.size()
        && static_cast<unsigned char>(s[j]) >= 0x20
        && static_cast<unsigned char>(s[j]) <= 0x3F) ++j;
    if (j >= s.size()
        || static_cast<unsigned char>(s[j]) < 0x40
        || static_cast<unsigned char>(s[j]) > 0x7E) return false;
    i = j + 1;   // 推进到终止字节之后（含终止字节整体跳过）
    return true;
}

} // anonymous namespace

std::string CLFTextUtil::utf8SafeHead(const std::string& text, size_t maxBytes,
                                      const std::string& ellipsis) {
    if (text.size() <= maxBytes) return text;
    size_t cut = backToCharBoundary(text, maxBytes);
    return text.substr(0, cut) + ellipsis;
}

std::string CLFTextUtil::utf8SafeTail(const std::string& text, size_t maxBytes,
                                      const std::string& ellipsis) {
    if (text.size() <= maxBytes) return text;
    size_t start = text.size() - maxBytes;
    start = forwardToCharBoundary(text, start);
    return ellipsis + text.substr(start);
}

int CLFTextUtil::charWidth(unsigned char c) {
    if (c < 0x80) return 1;   // ASCII
    if (c >= 0xC0) return 2;  // UTF-8 多字节首字节（CJK/全角计 2）
    return 0;                 // UTF-8 续字节
}

int CLFTextUtil::displayWidth(const std::string& s) {
    int w = 0;
    for (size_t i = 0; i < s.size();) {
        if (skipAnsiEscape(s, i)) continue;   // ANSI 转义序列不占显示宽（i 已推进）
        w += charWidth(static_cast<unsigned char>(s[i]));
        ++i;
    }
    return w;
}

std::string CLFTextUtil::substrByWidth(const std::string& s, int maxW) {
    int w = 0;
    for (size_t i = 0; i < s.size();) {
        if (skipAnsiEscape(s, i)) continue;   // ANSI 转义序列不占显示宽
        int cw = charWidth(static_cast<unsigned char>(s[i]));
        if (cw == 0) { ++i; continue; }           // UTF-8 续字节，不单独算
        if (w + cw > maxW) return s.substr(0, i);
        w += cw;
        if (cw == 2) { ++i; while (i < s.size()
            && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i; }
        else { ++i; }
    }
    return s;
}

std::string CLFTextUtil::replaceAll(std::string s, const std::string& from,
                                    const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string CLFTextUtil::truncateToolResult(const std::string& content) {
    // C2b：自 CLFContext::truncateContent 原样搬移（8000 阈值 + 标记语义保真；
    // A2 已将字节级 substr 换为 utf8SafeHead——不劈半多字节）
    constexpr size_t kMaxMessageChars = 8000;
    if (content.size() <= kMaxMessageChars) return content;
    return utf8SafeHead(content, kMaxMessageChars)
           + "\n\n[truncated, original: " + std::to_string(content.size()) + " chars]";
}

std::vector<std::string> CLFTextUtil::splitLines(const std::string& text,
                                                 bool keepEmpty) {
    std::vector<std::string> out;
    if (text.empty()) {
        if (keepEmpty) out.emplace_back();
        return out;
    }
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) {
            out.push_back(text.substr(pos));
            break;
        }
        out.push_back(text.substr(pos, nl - pos));
        pos = nl + 1;
    }
    return out;
}

std::string CLFTextUtil::localNow(const char* fmt) {
    std::tm lt = localNowTm();
    char buf[64];
    std::strftime(buf, sizeof(buf), fmt, &lt);
    return std::string(buf);
}

std::tm CLFTextUtil::localNowTm() {
    std::time_t now = std::time(nullptr);
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &now);
#else
    localtime_r(&now, &lt);
#endif
    return lt;
}

int CLFTextUtil::estimateTokenChars(const std::string& text) {
    int ascii = 0;
    int nonAscii = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) ++ascii;
        else if ((c & 0xC0) == 0xC0) ++nonAscii;
    }
    return (ascii / 4) + (nonAscii * 3 / 2);
}

int CLFTextUtil::estimateTokensForMessage(const CLFMessage& msg) {
    // 与旧 CLFContext::estimateTokensForMessage 逐字段等价：
    // id/name 字节数并入 ascii 总数后统一 /4（逐项 /4 会因整数除产生差异）
    int ascii = 0;
    int nonAscii = 0;
    auto countText = [&](const std::string& text) {
        for (size_t i = 0; i < text.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(text[i]);
            if (c < 0x80) ++ascii;
            else if ((c & 0xC0) == 0xC0) ++nonAscii;
        }
    };
    countText(msg.m_content);
    for (const auto& tc : msg.m_toolCalls) {
        countText(tc.m_arguments);
        ascii += static_cast<int>(tc.m_id.size());
        ascii += static_cast<int>(tc.m_name.size());
    }
    return (ascii / 4) + (nonAscii * 3 / 2);
}

} // namespace CLF::CLFCore
