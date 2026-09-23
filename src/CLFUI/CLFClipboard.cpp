// CLFClipboard.cpp — 系统剪贴板（平台层收敛 2026-09-23：实现下沉
// CLFPlatform::readClipboard/writeClipboard——本类保留为转调门面，
// UI 调用面零变化；原 Windows 实现整体搬移平台层，非 Windows 空实现
// 补显式分支（清单 #1：原 write 整个函数被 #ifdef 包住连 #else 都没有）

#include "CLFUI/CLFClipboard.hpp"

#include "CLFTypes/CLFPlatform.hpp"

namespace CLF::CLFUI {

std::string CLFClipboard::read() {
    return CLF::CLFCore::CLFPlatform::readClipboard();
}

void CLFClipboard::write(const std::string& text) {
    (void)CLF::CLFCore::CLFPlatform::writeClipboard(text);
}

} // namespace CLF::CLFUI
