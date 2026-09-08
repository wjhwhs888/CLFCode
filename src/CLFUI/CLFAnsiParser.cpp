// CLFAnsiParser.cpp — SGR 转义序列解析实现
// 状态机：\033[...m 更新样式状态（bold/fg），普通字符按当前状态累积分段。
// 支持的 SGR 码（CLFAnsi 实际使用集合 + 常见重置）：
//   0 重置全部 / 1 bold / 22 bold 关 / 30-37 前景 / 39 前景默认 / 90-97 亮前景
// 嵌套形态（cyan(bold(x)) → \033[36m\033[1m…\033[0m\033[0m）按顺序状态叠加，
// 重置后回到"无样式"（CLFAnsi 包装均为成对 reset，不做栈恢复）

#include "CLFUI/CLFAnsiParser.hpp"

namespace CLF::CLFUI {

namespace {

// 解析 "\033[<params>m"；返回终止位置（指向 'm' 之后的索引）。
// 非 SGR 序列（终止字节非 m）返回 npos
size_t matchSgr(const std::string& s, size_t i, std::string& params) {
    // 前置条件：s[i]=='\033' && s[i+1]=='['
    size_t j = i + 2;
    while (j < s.size() && s[j] != 'm'
        && !(static_cast<unsigned char>(s[j]) >= 0x40
             && static_cast<unsigned char>(s[j]) <= 0x7E)) ++j;
    if (j >= s.size() || s[j] != 'm') return std::string::npos;
    params = s.substr(i + 2, j - (i + 2));
    return j + 1;
}

// 手动解析单个码（stoi 容错：非数字 → -1 忽略）
int parseCode(const std::string& token) {
    if (token.empty()) return 0;   // "\033[m" 等价 "\033[0m"
    int code = 0;
    for (char c : token) {
        if (c < '0' || c > '9') return -1;
        code = code * 10 + (c - '0');
    }
    return code;
}

void applyCode(int code, bool& bold, int& fg) {
    if (code == 0) { bold = false; fg = -1; return; }
    if (code == 1) { bold = true; return; }
    if (code == 22) { bold = false; return; }
    if (code >= 30 && code <= 37) { fg = code; return; }
    if (code == 39) { fg = -1; return; }
    if (code >= 90 && code <= 97) { fg = code; return; }
    // 其余码（38/48 扩展色等）忽略
}

} // anonymous namespace

std::vector<CLFAnsiSegment> CLFAnsiParser::parse(const std::string& line) {
    std::vector<CLFAnsiSegment> segs;
    std::string buf;
    bool bold = false;
    int fg = -1;

    auto flush = [&]() {
        if (buf.empty()) return;
        segs.push_back(CLFAnsiSegment{std::move(buf), bold, fg});
        buf.clear();
    };

    for (size_t i = 0; i < line.size();) {
        if (line[i] == '\033' && i + 1 < line.size() && line[i + 1] == '[') {
            std::string params;
            const size_t next = matchSgr(line, i, params);
            if (next == std::string::npos) {
                // 不完整/非 SGR 序列：ESC 按普通字符保留（不吞内容）
                buf += line[i++];
                continue;
            }
            flush();
            size_t p = 0;
            while (true) {
                const size_t semi = params.find(';', p);
                const std::string token = params.substr(
                    p, semi == std::string::npos ? std::string::npos : semi - p);
                applyCode(parseCode(token), bold, fg);
                if (semi == std::string::npos) break;
                p = semi + 1;
            }
            i = next;
        } else {
            buf += line[i++];
        }
    }
    flush();
    return segs;
}

std::string CLFAnsiParser::strip(const std::string& line) {
    std::string out;
    out.reserve(line.size());
    for (size_t i = 0; i < line.size();) {
        if (line[i] == '\033' && i + 1 < line.size() && line[i + 1] == '[') {
            std::string params;
            const size_t next = matchSgr(line, i, params);
            if (next != std::string::npos) {
                i = next;
                continue;
            }
        }
        out += line[i++];
    }
    return out;
}

} // namespace CLF::CLFUI
