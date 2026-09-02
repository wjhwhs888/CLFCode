// CLFTipsBar.cpp — Tips 行实现（A5，设计-工具调用循环上限机制改造 §3.4）

#include "CLFUI/CLFTipsBar.hpp"

#include <filesystem>
#include <fstream>

#include <ftxui/dom/elements.hpp>

#include "CLFCore/CLFConfigLoader.hpp"
#include "CLFCore/CLFLogger.hpp"
#include "CLFTypes/CLFPeriodicTimer.hpp"

namespace CLF::CLFUI {
using namespace CLF::CLFCore;  // CLFConfigLoader / CLFLogger（CLFRepl.cpp 同模式）

namespace {

// 兜底条目（文件缺失/解析失败时）——constexpr const char* 数组：
// 文件级非平凡静态对象（vector/set）在 boost::ut 静态析构期跨 TU 析构
// 顺序未定义（本项目两次段错误教训），仅此形式安全
constexpr const char* const kDefaultTips[] = {
    "/help 帮助 · /context 上下文 · Shift+Tab 切换安全模式",
    "Ctrl+T 展开思考 · Ctrl+N 换行 · Ctrl+R 折叠 · Esc 中断",
    "复杂任务可分阶段完成，触顶后输入「继续」接着做",
    "长时间无响应可 Esc 中断，或输入「停止过度思考」",
    "高危操作会弹出确认，仔细核对后选择",
};

} // anonymous namespace

CLFTipsBar::CLFTipsBar(CLF::CLFTypes::ICLFOutput* output,
                       int rotateIntervalSec,
                       int silenceThresholdSec,
                       std::string tipsPath,
                       bool startTimer)
    : m_output(output)
    , m_entries(loadEntries(tipsPath))
    , m_rotateIntervalSec(rotateIntervalSec)
    , m_silenceThresholdSec(silenceThresholdSec) {
    if (m_output) m_lastActivity = m_output->activityCount();
    if (startTimer) {
        // 构造即启动（项目惯例）；析构自动 stop（CLFPeriodicTimer 析构即 stop）
        m_timer = std::make_unique<CLF::CLFTypes::CLFPeriodicTimer>(
            std::chrono::seconds(m_rotateIntervalSec),
            [this]() { tick(); });
    }
}

CLFTipsBar::~CLFTipsBar() = default;  // m_timer 析构即 stop

std::vector<std::string> CLFTipsBar::loadEntries(const std::string& tipsPath) const {
    const std::string path = tipsPath.empty()
        ? CLFConfigLoader::resolvePath("config/tips.txt") : tipsPath;
    // u8path：中文路径按 ANSI 代码页窄构造会乱码（编码陷阱族）
    std::ifstream f(std::filesystem::u8path(path));
    std::vector<std::string> entries;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();  // CRLF
        if (line.empty() || line[0] == '#') continue;               // 空行/注释
        entries.push_back(line);
    }
    if (!entries.empty()) return entries;

    // 兜底：内置默认条目（保证 Tips 永远有内容）
    std::vector<std::string> fallback;
    for (const char* s : kDefaultTips) fallback.emplace_back(s);
    CLFLogger::instance().warn("[TipsBar] tips file missing/invalid, fallback defaults: " + path);
    return fallback;
}

void CLFTipsBar::tick() {
    const uint64_t now = m_output ? m_output->activityCount() : 0;
    if (now != m_lastActivity.load()) {
        // 有输出活动 → 静默清零（异常消除回灰轮播）
        m_silenceSec = 0;
        m_lastActivity = now;
    } else {
        m_silenceSec += static_cast<uint64_t>(m_rotateIntervalSec);
    }
    ++m_rotateIndex;
}

std::string CLFTipsBar::currentLine(bool busy) const {
    if (!busy || m_entries.empty()) return std::string();
    const uint64_t silence = m_silenceSec.load();
    if (silence >= static_cast<uint64_t>(m_silenceThresholdSec)) {
        return "⚠ 已 " + std::to_string(silence) + "s 无响应，可 Esc 中断后输入「停止过度思考」继续";
    }
    return m_entries[m_rotateIndex.load() % m_entries.size()];
}

ftxui::Element CLFTipsBar::render(bool busy) const {
    const std::string line = currentLine(busy);
    if (line.empty()) return ftxui::emptyElement();
    const bool isWarning = line.rfind("⚠", 0) == 0;
    return ftxui::text(line)
        | ftxui::color(isWarning ? ftxui::Color::RedLight : ftxui::Color::GrayDark);
}

} // namespace CLF::CLFUI
