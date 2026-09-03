// CLFPasserby.cpp — 会话节奏观察器实现

#include "CLFCore/CLFPasserby.hpp"

#include "CLFTypes/ICLFOutput.hpp"
#include "CLFTypes/CLFTextUtil.hpp"   // A2：localNowTm（消平台 ifdef）

#include <ctime>

namespace CLF::CLFCore {

CLFPasserby::CLFPasserby(CLF::CLFTypes::ICLFOutput* output,
                         std::function<bool()> timeOk)
    : m_output(output)
    , m_timeOk(timeOk ? std::move(timeOk)
                      : std::function<bool()>(defaultTimeOk)) {
}

void CLFPasserby::onTurnFinished() {
    if (m_triggered) return;
    ++m_turnCount;
    if (m_turnCount < kTurnThreshold) return;
    if (!m_timeOk || !m_timeOk()) return;
    trigger();
}

bool CLFPasserby::defaultTimeOk() {
    // A2：字段读取场景（非格式化）→ CLFTextUtil::localNowTm（消平台 ifdef）
    const std::tm lt = CLFTextUtil::localNowTm();
    // 特殊日期全天：10 月 20 日（tm_mon 0 基）
    if (lt.tm_mon == 9 && lt.tm_mday == 20) return true;
    // 深夜窗：22:00–06:00（含跨日）
    return lt.tm_hour >= 22 || lt.tm_hour < 6;
}

std::string CLFPasserby::decodeName() {
    // UTF-8 字节序列（十进制）：E9 83 AD E4 BA AC E5 8D 8E
    static const unsigned char kEncoded[] = {233, 131, 173,
                                             228, 186, 172,
                                             229, 141, 142};
    return std::string(reinterpret_cast<const char*>(kEncoded),
                       sizeof(kEncoded));
}

void CLFPasserby::trigger() {
    m_triggered = true;
    if (!m_output) return;
    m_output->emitStyledLine(
        "  " + decodeName() + "路过，轻飘飘的看了一眼，嗯，看起来有点意思",
        CLF::CLFTypes::ICLFOutput::LineStyle::Context);
}

} // namespace CLF::CLFCore
