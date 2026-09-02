// CLFTipsBar.hpp — 输入框上方常驻 Tips 行（A5，设计-工具调用循环上限机制改造 §3.4）
// 纯显示层：只管显示位置 / 轮播频率 / 颜色，不感知数据来源细节
//   - 常态数据源：config/tips.txt（每行一条，CLFConfigLoader::resolvePath 查找链）
//   - 兜底数据源：代码内置条目（文件缺失/解析失败时，保证永远有内容）
//   - 异常态数据源：输出活动计数（ICLFOutput ⑨）静默判定
// 线程模型：ticker（CLFPeriodicTimer）写原子状态，渲染线程读——无锁
//
// example:
//   auto tips = std::make_unique<CLFTipsBar>(terminal);   // 默认 5s 轮播 / 300s 静默阈值
//   ftxui::Element el = tips->render(asyncSubmit.busy()); // 空闲自动空 Element
//   tips->tick();                                          // 测试直接驱动一个周期

#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "CLFTypes/ICLFOutput.hpp"
// ftxui::Element 是 shared_ptr<Node> 的 using 别名——前向声明 class Element
// 会与 ftxui 已有定义冲突（C2371），须 include 完整头
#include <ftxui/dom/node.hpp>
#include "CLFTypes/CLFPeriodicTimer.hpp"   // unique_ptr 成员需要完整类型

namespace CLF::CLFUI {

class CLFTipsBar {
public:
    // rotateIntervalSec / silenceThresholdSec 构造参数可注入（qa 用小值不等真实时间）
    // tipsPath 非空 = 指定数据文件（qa 注入临时文件/不存在路径测兜底）；空 = resolvePath 查找链
    // startTimer=false 供 qa 手动 tick()（避免后台定时器与断言竞态）
    explicit CLFTipsBar(CLF::CLFTypes::ICLFOutput* output,
                        int rotateIntervalSec   = 5,
                        int silenceThresholdSec = 300,
                        std::string tipsPath    = "",
                        bool startTimer         = true);
    ~CLFTipsBar();

    CLFTipsBar(const CLFTipsBar&)            = delete;
    CLFTipsBar& operator=(const CLFTipsBar&) = delete;

    // 当前应显示的行文本：busy=false 或条目空 → 空串（隐藏）；
    // 静默超阈值 → 浅红异常文案；否则灰色轮播条目
    std::string currentLine(bool busy) const;

    // 渲染为 Element（qa 不测渲染，人工验收；currentLine 覆盖逻辑测试）
    ftxui::Element render(bool busy) const;

    // 一个轮播周期（内部 CLFPeriodicTimer 调用；qa 直接调用）
    void tick();

    // 加载的条目数（qa 断言兜底/加载）
    size_t entryCount() const { return m_entries.size(); }

private:
    // 加载条目：tipsPath（指定）或 resolvePath("config/tips.txt")；
    // 缺失/读失败/空 → 内置兜底列表
    std::vector<std::string> loadEntries(const std::string& tipsPath) const;

    CLF::CLFTypes::ICLFOutput* m_output;
    std::vector<std::string>     m_entries;
    int                          m_rotateIntervalSec;
    int                          m_silenceThresholdSec;
    std::atomic<uint64_t>        m_silenceSec{0};   // 累计静默秒（ticker 写 / 渲染读）
    std::atomic<size_t>          m_rotateIndex{0};  // 轮播索引
    std::atomic<uint64_t>        m_lastActivity{0}; // 上次 tick 时的活动计数
    std::unique_ptr<CLF::CLFTypes::CLFPeriodicTimer> m_timer;
};

} // namespace CLF::CLFUI
