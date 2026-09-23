// CLFTextUtil.cpp — 公共文本工具实现（basic 层，clf_types；批次 A2）
// ⚠ 收敛纪律：各函数语义与替换点旧实现逐处对照（A2-1~A2-4 取证），
// 输出格式零变化（尤其 localNow 的 fmt 由调用点传原格式串）

#include "CLFTypes/CLFTextUtil.hpp"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <sstream>

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

// ============ 渲染宽字符区间表（与 FTXUI g_full_width_characters 同源）============
// 来源：Markus Kuhn wcwidth 数据（FTXUI screen/string.cpp 同款 116 区间）。
// 渲染层（FTXUI text()）按此表布局列——选区列→字节换算必须同表（2026-09-20
// 拖选列偏移根因：charWidth 对多字节恒计 2，⎿(U+23BF) 实为渲染 1 宽）。
struct WcInterval { std::uint32_t first; std::uint32_t last; };
constexpr WcInterval kFullWidthChars[] = {
    {0x01100, 0x0115f}, {0x0231a, 0x0231b}, {0x02329, 0x0232a},
    {0x023e9, 0x023ec}, {0x023f0, 0x023f0}, {0x023f3, 0x023f3},
    {0x025fd, 0x025fe}, {0x02614, 0x02615}, {0x02648, 0x02653},
    {0x0267f, 0x0267f}, {0x02693, 0x02693}, {0x026a1, 0x026a1},
    {0x026aa, 0x026ab}, {0x026bd, 0x026be}, {0x026c4, 0x026c5},
    {0x026ce, 0x026ce}, {0x026d4, 0x026d4}, {0x026ea, 0x026ea},
    {0x026f2, 0x026f3}, {0x026f5, 0x026f5}, {0x026fa, 0x026fa},
    {0x026fd, 0x026fd}, {0x02705, 0x02705}, {0x0270a, 0x0270b},
    {0x02728, 0x02728}, {0x0274c, 0x0274c}, {0x0274e, 0x0274e},
    {0x02753, 0x02755}, {0x02757, 0x02757}, {0x02795, 0x02797},
    {0x027b0, 0x027b0}, {0x027bf, 0x027bf}, {0x02b1b, 0x02b1c},
    {0x02b50, 0x02b50}, {0x02b55, 0x02b55}, {0x02e80, 0x02e99},
    {0x02e9b, 0x02ef3}, {0x02f00, 0x02fd5}, {0x02ff0, 0x02ffb},
    {0x03000, 0x0303e}, {0x03041, 0x03096}, {0x03099, 0x030ff},
    {0x03105, 0x0312f}, {0x03131, 0x0318e}, {0x03190, 0x031e3},
    {0x031f0, 0x0321e}, {0x03220, 0x03247}, {0x03250, 0x04dbf},
    {0x04e00, 0x0a48c}, {0x0a490, 0x0a4c6}, {0x0a960, 0x0a97c},
    {0x0ac00, 0x0d7a3}, {0x0f900, 0x0faff}, {0x0fe10, 0x0fe19},
    {0x0fe30, 0x0fe52}, {0x0fe54, 0x0fe66}, {0x0fe68, 0x0fe6b},
    {0x0ff01, 0x0ff60}, {0x0ffe0, 0x0ffe6}, {0x16fe0, 0x16fe4},
    {0x16ff0, 0x16ff1}, {0x17000, 0x187f7}, {0x18800, 0x18cd5},
    {0x18d00, 0x18d08}, {0x1b000, 0x1b11e}, {0x1b150, 0x1b152},
    {0x1b164, 0x1b167}, {0x1b170, 0x1b2fb}, {0x1f004, 0x1f004},
    {0x1f0cf, 0x1f0cf}, {0x1f18e, 0x1f18e}, {0x1f191, 0x1f19a},
    {0x1f200, 0x1f202}, {0x1f210, 0x1f23b}, {0x1f240, 0x1f248},
    {0x1f250, 0x1f251}, {0x1f260, 0x1f265}, {0x1f300, 0x1f320},
    {0x1f32d, 0x1f335}, {0x1f337, 0x1f37c}, {0x1f37e, 0x1f393},
    {0x1f3a0, 0x1f3ca}, {0x1f3cf, 0x1f3d3}, {0x1f3e0, 0x1f3f0},
    {0x1f3f4, 0x1f3f4}, {0x1f3f8, 0x1f43e}, {0x1f440, 0x1f440},
    {0x1f442, 0x1f4fc}, {0x1f4ff, 0x1f53d}, {0x1f54b, 0x1f54e},
    {0x1f550, 0x1f567}, {0x1f57a, 0x1f57a}, {0x1f595, 0x1f596},
    {0x1f5a4, 0x1f5a4}, {0x1f5fb, 0x1f64f}, {0x1f680, 0x1f6c5},
    {0x1f6cc, 0x1f6cc}, {0x1f6d0, 0x1f6d2}, {0x1f6d5, 0x1f6d7},
    {0x1f6eb, 0x1f6ec}, {0x1f6f4, 0x1f6fc}, {0x1f7e0, 0x1f7eb},
    {0x1f90c, 0x1f93a}, {0x1f93c, 0x1f945}, {0x1f947, 0x1f978},
    {0x1f97a, 0x1f9cb}, {0x1f9cd, 0x1f9ff}, {0x1fa70, 0x1fa74},
    {0x1fa78, 0x1fa7a}, {0x1fa80, 0x1fa86}, {0x1fa90, 0x1faa8},
    {0x1fab0, 0x1fab6}, {0x1fac0, 0x1fac2}, {0x1fad0, 0x1fad6},
    {0x20000, 0x2fffd}, {0x30000, 0x3fffd},
};

// 码点是否渲染 2 宽（区间有序，二分查找）
bool isFullWidth(std::uint32_t cp) {
    size_t lo = 0, hi = sizeof(kFullWidthChars) / sizeof(kFullWidthChars[0]);
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (cp < kFullWidthChars[mid].first) { hi = mid; }
        else if (cp > kFullWidthChars[mid].last) { lo = mid + 1; }
        else { return true; }
    }
    return false;
}

// pos 处 UTF-8 序列解码码点；非法/截断返回 0xFFFD（宽 1）
std::uint32_t decodeUtf8(const std::string& s, size_t pos) {
    unsigned char c0 = static_cast<unsigned char>(s[pos]);
    size_t len = 1;
    if ((c0 & 0xE0) == 0xC0) len = 2;
    else if ((c0 & 0xF0) == 0xE0) len = 3;
    else if ((c0 & 0xF8) == 0xF0) len = 4;
    else if (c0 >= 0x80) return 0xFFFD;   // 续字节开头的坏字节
    if (pos + len > s.size()) return 0xFFFD;
    std::uint32_t cp = c0 & (0xFF >> (len + 1));
    for (size_t k = 1; k < len; ++k) {
        unsigned char ck = static_cast<unsigned char>(s[pos + k]);
        if ((ck & 0xC0) != 0x80) return 0xFFFD;   // 续字节形态非法
        cp = (cp << 6) | (ck & 0x3F);
    }
    return cp;
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

int CLFTextUtil::renderCharWidth(const std::string& s, size_t pos) {
    if (pos >= s.size()) return 0;
    unsigned char c0 = static_cast<unsigned char>(s[pos]);
    if (c0 < 0x80) return 1;
    return isFullWidth(decodeUtf8(s, pos)) ? 2 : 1;
}

size_t CLFTextUtil::utf8CharLen(const std::string& s, size_t pos) {
    unsigned char c0 = static_cast<unsigned char>(s[pos]);
    if ((c0 & 0xE0) == 0xC0) return 2;
    if ((c0 & 0xF0) == 0xE0) return 3;
    if ((c0 & 0xF8) == 0xF0) return 4;
    return 1;   // ASCII 或坏字节兜底
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

// ============ 渲染口径折行（2026-09-22 启动横幅批次）============
// 与 renderCharWidth 同表（FTXUI wcwidth）：⎿/●/❯/块字等非宽多字节符号计
// 1 宽——charWidth 口径恒计 2 致折行点提前（含 ⎿ 长行折行位置错误的既有
// 缺陷）。中文仍在宽表（计 2）与 charWidth 口径一致，仅符号行折行点右移。
// 折行与选区 colToByte（已用 renderCharWidth）口径自此统一。

int CLFTextUtil::renderDisplayWidth(const std::string& s) {
    int w = 0;
    for (size_t i = 0; i < s.size();) {
        if (skipAnsiEscape(s, i)) continue;   // ANSI 转义不占显示宽（i 已推进）
        w += renderCharWidth(s, i);
        i += utf8CharLen(s, i);               // 坏字节兜底 1，循环不卡续字节
    }
    return w;
}

std::string CLFTextUtil::renderSubstrByWidth(const std::string& s, int maxW) {
    int w = 0;
    for (size_t i = 0; i < s.size();) {
        if (skipAnsiEscape(s, i)) continue;   // 转义整体跳过（切分点不落转义中间）
        int cw = renderCharWidth(s, i);
        if (w + cw > maxW) return s.substr(0, i);
        w += cw;
        i += utf8CharLen(s, i);
    }
    return s;
}

std::vector<std::string> CLFTextUtil::wrapLines(const std::string& s, int wrapW) {
    std::vector<std::string> parts;
    if (wrapW <= 0) { parts.push_back(s); return parts; }   // 免折行（外层守卫同语义）
    std::string remaining = s;
    while (!remaining.empty()) {
        std::string part = renderSubstrByWidth(remaining, wrapW);
        if (part.empty()) part = remaining.substr(0, 1);    // fallback（与 CLFReplView 折行同语义）
        remaining = remaining.substr(part.size());
        parts.push_back(std::move(part));
    }
    return parts;
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
    // A2 已将字节级 substr 换为 utf8SafeHead——不劈半多字节）。
    // G2（2026-09-23 命令执行层 §6.4）：仅保头 → 头+尾各 8000——命令结论
    // 常在尾部，长输出时模型必须能看到尾部结论（W-6 缺陷修根）。
    // 标记保留原格式（qa T2 钉子："[truncated, original: N chars]"）
    constexpr size_t kHeadChars = 8000;
    constexpr size_t kTailChars = 8000;
    if (content.size() <= kHeadChars + kTailChars) return content;
    return utf8SafeHead(content, kHeadChars)
           + "\n\n...[中间省略 "
           + std::to_string(content.size() - kHeadChars - kTailChars) + " chars]...\n\n"
           + utf8SafeTail(content, kTailChars)
           + "\n[truncated, original: " + std::to_string(content.size()) + " chars]";
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

// 行范围切片（2.2a 自 CLFBuiltinTools 归位，语义原样保真）：
// offset 为 0 基起始行，limit<=0 取到末尾
std::string CLFTextUtil::sliceLines(const std::string& content, int offset, int limit) {
    if (offset <= 0 && limit <= 0) return content;
    std::istringstream iss(content);
    std::string line, out;
    int idx = 0, taken = 0;
    while (std::getline(iss, line)) {
        if (idx++ < offset) continue;
        if (limit > 0 && taken >= limit) break;
        out += line;
        out += '\n';
        ++taken;
    }
    return out;
}

// 路径边界判定（2.3 自 CLFCapabilities 归位——插件不可链 core，工作区根由
// 调用方传入；语义原样保真：weakly_canonical 防软链接逃逸、逐段比较防前缀误判）
bool CLFTextUtil::isWithinWorkspaceOf(const std::string& workspaceRootUtf8,
                                      const std::string& path, std::string& outError) {
    namespace fs = std::filesystem;
    if (workspaceRootUtf8.empty()) return true;   // 空根 = 跳过校验（2.2b 定案）
    std::error_code ec;

    fs::path root = fs::weakly_canonical(fs::u8path(workspaceRootUtf8), ec);
    if (ec) { outError = "无法解析工作区根目录"; return false; }

    fs::path target = fs::u8path(path);
    if (!target.is_absolute()) target = root / target;
    target = fs::weakly_canonical(target, ec);
    if (ec) { outError = "无法解析路径: " + path; return false; }

    auto rootIt = root.begin();
    auto tgtIt  = target.begin();
    for (; rootIt != root.end(); ++rootIt, ++tgtIt) {
        if (tgtIt == target.end() || *tgtIt != *rootIt) {
            outError = "路径超出工作区边界: " + path;
            return false;
        }
    }
    return true;
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
