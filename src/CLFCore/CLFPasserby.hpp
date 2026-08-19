// CLFPasserby.hpp — 会话节奏观察器
// 在特定时间窗与会话轮数条件满足时，向输出追加一行附加文本（每个进程一次）。
// 名称与参数以字节编码存储，不出现明文。

#pragma once

#include <ctime>
#include <functional>
#include <string>

namespace CLF::CLFTypes { class ICLFOutput; }

namespace CLF::CLFCore {

class CLFPasserby {
public:
    // timeOk：时间条件检查（可注入以便测试；默认：深夜窗 22:00-06:00 或 10/20 全天）
    explicit CLFPasserby(CLF::CLFTypes::ICLFOutput* output,
                         std::function<bool()> timeOk = {});
    // 每轮 AI 对话结束后调用（含工具回合）；命中条件时输出一次并永久标记
    void onTurnFinished();

private:
    static constexpr int kTurnThreshold = 8;   // 会话内需累计的对话轮数
    static std::string decodeName();           // 字节码 → UTF-8 名字
    static bool defaultTimeOk();               // 默认时间条件
    void trigger();                            // 输出附加行并置标记

    CLF::CLFTypes::ICLFOutput* m_output;
    std::function<bool()> m_timeOk;
    int m_turnCount = 0;
    bool m_triggered = false;
};

} // namespace CLF::CLFCore
