// CLFTerminal.cpp — 终端 UI (FTXUI 组件树 + 静态兼容层)
#include "CLFUI/CLFTerminal.hpp"
#include "CLFUI/CLFAnsi.hpp"
#include "CLFUI/CLFRepl.hpp"
#include "CLFUI/CLFScrollBuffer.hpp"

#include <algorithm>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#undef min
#undef max
#endif

namespace CLF::CLFUI {

using namespace ftxui;

// ========== 静态工具 (委托 CLFAnsi) ==========
void CLFTerminal::enableAnsi(){ CLFAnsi::enable(); }
std::string CLFTerminal::green(const std::string& s){ return CLFAnsi::green(s); }
std::string CLFTerminal::cyan(const std::string& s){ return CLFAnsi::cyan(s); }
std::string CLFTerminal::lightBlue(const std::string& s){ return CLFAnsi::lightBlue(s); }
std::string CLFTerminal::yellow(const std::string& s){ return CLFAnsi::yellow(s); }
std::string CLFTerminal::red(const std::string& s){ return CLFAnsi::red(s); }
std::string CLFTerminal::gray(const std::string& s){ return CLFAnsi::gray(s); }
std::string CLFTerminal::bold(const std::string& s){ return CLFAnsi::bold(s); }
int CLFTerminal::getTerminalHeight(){ return CLFAnsi::terminalHeight(); }
int CLFTerminal::getTerminalWidth(){ return CLFAnsi::terminalWidth(); }
int CLFTerminal::textWidth(const std::string& t){ return CLFAnsi::textWidth(t); }
int CLFTerminal::wrappedLines(const std::string& t){ return CLFAnsi::wrappedLines(t); }
std::string CLFTerminal::diagnosticInfo(){
    return "终端: 高"+std::to_string(getTerminalHeight())+" x 宽"+std::to_string(getTerminalWidth())
           +", ANSI: "+(CLFAnsi::isEnabled()?"开":"关");
}

// ========== 静态兼容层 (委托到 FTXUI 状态, 或保留原始逻辑) ==========

static CLFScrollBuffer s_buffer;
static std::string s_modeLabel;

void CLFTerminal::scrollPrint(const std::string& text){
    s_buffer.append(text);
    std::cout<<text<<std::flush;
}
void CLFTerminal::thoughtMark(int seconds, int searchCount, int readCount){
    if(seconds<=0)return;
    std::string msg="  Thought for "+std::to_string(seconds)+"s";
    if(searchCount>0)msg+=", searched for "+std::to_string(searchCount)+" pattern(s)";
    if(readCount>0)msg+=", read "+std::to_string(readCount)+" file(s)";
    // 仅设 msg 文本, 不走 ANSI 直写
}
void CLFTerminal::showThinking(int seconds){
    // 静态兼容: 不输出 ANSI, 仅设状态文本 (FTXUI Renderer 会显示)
}
void CLFTerminal::showWorking(const std::string& title){
    scrollPrint("\r"+title);
}
void CLFTerminal::clearStatus(){}
void CLFTerminal::showTaskTree(const std::vector<std::string>& phases){}
void CLFTerminal::drawInput(const std::string& text, int){}
void CLFTerminal::drawMode(const std::string& mode){ s_modeLabel=mode; }
void CLFTerminal::showConfirm(const std::vector<std::string>&, int){}
void CLFTerminal::hideConfirm(){}
void CLFTerminal::initLayout(const std::string& modeLabel){ s_modeLabel=modeLabel; s_buffer.clear(); }
void CLFTerminal::redrawAll(){}
void CLFTerminal::restoreScrollRegion(){}
void CLFTerminal::drawStatusArea(const std::string&, const std::string&){}
void CLFTerminal::toContentArea(){ s_buffer.clear(); }  // 注: FTXUI 路径通过 emitContent→m_contentBuffer, 不受 s_buffer 影响

// ========== FTXUI 实现 ==========

CLFTerminal::~CLFTerminal() {
    if(m_screen) m_screen->ExitLoopClosure()();
}

void CLFTerminal::requestRefresh() {
    if (m_screen) m_screen->Post(Event::Custom);
}

// ---- ICLFOutput 映射 ----

// 去掉 ANSI 转义码 (\033[...m)
static std::string stripAnsi(const std::string& s) {
    std::string r;
    for(size_t i=0;i<s.size();++i){
        if(s[i]=='\033'&&i+1<s.size()&&s[i+1]=='['){
            i+=2;
            while(i<s.size()&&!( (s[i]>='a'&&s[i]<='z')||(s[i]>='A'&&s[i]<='Z')))++i;
        }else r+=s[i];
    }
    return r;
}

void CLFTerminal::emitContent(const std::string& text) {
    {
        std::lock_guard lock(m_mutex);
        // 跨调用累积: 按 \n 拆行, 未完成行保持在 m_pendingLine
        for(char c : text){
            if(c=='\n'){
                m_contentBuffer.push_back(stripAnsi(m_pendingLine));
                m_pendingLine.clear();
            }else{
                m_pendingLine+=c;
            }
        }
    }
    requestRefresh();
}

void CLFTerminal::emitRaw(const std::string& data) {
    emitContent(data);  // 第一期: 不处理 ANSI 透传
}

void CLFTerminal::setStatus(const std::string& title, int cur, int total) {
    if (title.empty()) { m_statusText.clear(); }
    else if (cur >= 0 && total > 0) {
        m_statusText = title + " (" + std::to_string(cur) + "/" + std::to_string(total) + ")";
    } else {
        m_statusText = title;
    }
    requestRefresh();
}

void CLFTerminal::onToolCall(const std::string& name, const std::string& params) {
    emitContent("● " + name + "(" + params + ")");
}

void CLFTerminal::onToolResult(const std::string&, const std::string& result, bool ok) {
    emitContent("  ⎿ " + std::string(ok ? "✓ " : "✗ ") + result);
}

bool CLFTerminal::confirm(const std::string& prompt) {
    if (!m_screen) return false;
    m_confirmOpts = {"确认", "取消"};
    m_confirmSel = 0;
    m_confirmActive = true;
    m_confirmResult = false;
    requestRefresh();
    // nested Loop 等待选择
    auto inner = Renderer([&]{
        Elements btns;
        for(size_t i=0;i<m_confirmOpts.size();++i){
            auto label = (int)i==m_confirmSel ? "["+green("●")+"] "+bold(m_confirmOpts[i])
                                              : "[ ] "+gray(m_confirmOpts[i]);
            btns.push_back(text("  "+label));
        }
        return vbox({
            separator(),
            text("  ⚠ "+prompt) | color(Color::Yellow),
            hbox(btns),
            text("  Enter=确认 Esc=取消"),
        }) | border | center;
    });
    // keyboard handler
    auto c = CatchEvent(inner, [&](Event e){
        if (e == Event::Return) { m_confirmResult = true; m_screen->ExitLoopClosure()(); return true; }
        if (e == Event::Escape) { m_confirmResult = false; m_screen->ExitLoopClosure()(); return true; }
        if (e == Event::ArrowLeft || e == Event::ArrowRight) { m_confirmSel = 1-m_confirmSel; return true; }
        return false;
    });
    m_screen->Loop(c);
    m_confirmActive = false;
    return m_confirmResult;
}

int CLFTerminal::askSelect(const std::vector<std::string>&, const std::string&) {
    return -1;  // 第一期未激活
}

std::optional<std::string> CLFTerminal::askInput(const std::string&, const std::string&) {
    return std::nullopt;  // 第一期未激活
}

void CLFTerminal::onInterrupt(std::function<void()> cb) {
    m_interruptCb = std::move(cb);
}

void CLFTerminal::emitError(const std::string& msg) {
    emitContent(red("✗ ") + msg);
}

void CLFTerminal::requestShutdown(const std::string& reason) {
    std::cerr << "\n[FATAL] " << reason << std::endl;
    m_shutdownRequested = true;
}

// ---- FTXUI 组件树 ----

Component CLFTerminal::buildUI(CLFRepl* repl) {
    // 输入区
    InputOption inputOpt;
    inputOpt.multiline = false;
    auto input = Input(&m_inputText, "> ", inputOpt);

    // 组件树
    auto root = Container::Vertical({input});

    auto renderer = Renderer(root, [&, repl] {
        // 内容区
        Elements contentLines;
        {
            std::lock_guard lock(m_mutex);
            for (auto& line : m_contentBuffer)
                contentLines.push_back(text(line));
        }
        auto scroll = vbox(contentLines) | vscroll_indicator | frame | flex;

        // 状态行
        Element status = emptyElement();
        if (!m_statusText.empty())
            status = text("  " + m_statusText) | dim;

        // 模式行
        auto modeStr = s_modeLabel.empty() ? "edit" : s_modeLabel;
        auto modeLine = hbox({
            text("  " + modeStr + " mode on"),
            filler(),
            text("shift+tab to cycle · esc to interrupt · /help for help"),
        }) | dim;

        // 确认区
        Element confirmBar = emptyElement();
        if (m_confirmActive) {
            Elements btns;
            for (size_t i = 0; i < m_confirmOpts.size(); ++i) {
                auto label = (int)i == m_confirmSel
                    ? "[" + green("●") + "] " + bold(m_confirmOpts[i])
                    : "[ ] " + gray(m_confirmOpts[i]);
                btns.push_back(text("  " + label));
            }
            confirmBar = hbox(btns);
        }

        return vbox({
            scroll,
            separator(),
            status,
            separator(),
            input->Render() | border,
            separator(),
            modeLine,
            confirmBar,
        });
    });

    // Enter 提交
    auto submitHandler = CatchEvent(renderer, [&, repl](Event e) {
        if (e == Event::Return && !m_inputText.empty() && repl) {
            auto text = m_inputText;
            m_inputText.clear();
            repl->submit(text);        // 调用现有 submit 逻辑
            return true;
        }
        // ESC 中断
        if (e == Event::Escape && m_interruptCb) {
            m_interruptCb();
            return true;
        }
        return false;
    });

    return submitHandler;
}

} // namespace CLF::CLFUI
