// CLFAnsi.cpp — ANSI 终端控制原语实现
// 平台层收敛（2026-09-23）：VT 开启与终端尺寸取数已下沉 CLFPlatform
// （enableVirtualTerminal/consoleSize）——本类保留颜色包装与门控状态。

#include "CLFUI/CLFAnsi.hpp"

#include "CLFTypes/CLFPlatform.hpp"

namespace CLF::CLFUI {

bool CLFAnsi::s_enabled = false;

void CLFAnsi::enable() {
    s_enabled = CLF::CLFCore::CLFPlatform::enableVirtualTerminal();
}

std::string CLFAnsi::cyan(const std::string& s) { return s_enabled ? "\033[36m" + s + "\033[0m" : s; }
std::string CLFAnsi::cyanLight(const std::string& s) { return s_enabled ? "\033[96m" + s + "\033[0m" : s; }
std::string CLFAnsi::red(const std::string& s)  { return s_enabled ? "\033[31m" + s + "\033[0m" : s; }
std::string CLFAnsi::gray(const std::string& s) { return s_enabled ? "\033[90m" + s + "\033[0m" : s; }
std::string CLFAnsi::bold(const std::string& s) { return s_enabled ? "\033[1m" + s + "\033[0m" : s; }

int CLFAnsi::terminalHeight() {
    int w = 0, h = 0;
    return CLF::CLFCore::CLFPlatform::consoleSize(w, h) ? h : -1;
}

int CLFAnsi::terminalWidth() {
    int w = 0, h = 0;
    return CLF::CLFCore::CLFPlatform::consoleSize(w, h) ? w : -1;
}

} // namespace CLF::CLFUI
