// CLFTerminal.cpp — 6 区终端 UI 实现
// 颜色/尺寸 → CLFAnsi; 缓冲 → CLFScrollBuffer

#include "CLFCore/CLFTerminal.hpp"
#include "CLFCore/CLFAnsi.hpp"
#include "CLFCore/CLFScrollBuffer.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#undef min
#undef max
#endif

namespace CLF::CLFCore {

CLFScrollBuffer CLFTerminal::s_buffer;
std::string CLFTerminal::s_inputText;
std::string CLFTerminal::s_modeLabel;
std::string CLFTerminal::s_statusLine;
std::vector<std::string> CLFTerminal::s_statusTree;
std::vector<std::string> CLFTerminal::s_confirmOpts;
int  CLFTerminal::s_confirmSel = 0;
int  CLFTerminal::s_inputCursor = 0;
bool CLFTerminal::s_layoutValid = false;

namespace {

// ============ 工具函数 ============

std::string truncToW(const std::string& text, int maxW) {
    if (CLFAnsi::textWidth(text) <= maxW) return text;
    std::string r = text;
    while (CLFAnsi::textWidth(r) > maxW) {
        size_t len = 1;
        while (len < r.size() && (static_cast<unsigned char>(r[r.size()-len]) & 0xC0) == 0x80) ++len;
        r.erase(r.size() - len);
    }
    return r;
}

std::string stripAnsi(const std::string& text) {
    std::string r;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\033' && i+1 < text.size() && text[i+1] == '[') {
            i += 2;
            while (i < text.size() && !((text[i]>='a'&&text[i]<='z')||(text[i]>='A'&&text[i]<='Z'))) ++i;
        } else r += text[i];
    }
    return r;
}

int countLines(const std::string& t) { int n=1; for(char c:t) if(c=='\n')++n; return n; }

std::vector<std::string> splitLines(const std::string& t) {
    std::vector<std::string> ls; std::string cur;
    for(char c:t){ if(c=='\n'){ls.push_back(cur);cur.clear();}else cur+=c; }
    ls.push_back(cur); return ls;
}

void cursorVisPos(const std::string& text, int bytePos, int& outLine, int& outColW) {
    outLine=0; outColW=0;
    for(int i=0;i<bytePos&&i<(int)text.size();){
        if(text[i]=='\n'){++outLine;outColW=0;++i;}
        else{unsigned char c=text[i];int cl=1;
            if((c&0xE0)==0xC0)cl=2;else if((c&0xF0)==0xE0)cl=3;else if((c&0xF8)==0xF0)cl=4;
            outColW+=CLFAnsi::textWidth(text.substr(i,cl));i+=cl;}
    }
}

void setSR(int top, int bottom) { std::cout << "\033[" << top << ";" << bottom << "r" << std::flush; }
void resetSR() { std::cout << "\033[r" << std::flush; }

} // anonymous namespace

// ============ 布局计算 ============

int CLFTerminal::inputLineCount() { int n=countLines(s_inputText); return n<1?1:n; }
int CLFTerminal::statusLineCount() { return 1 + (int)s_statusTree.size(); }
int CLFTerminal::contentBottom() {
    int H=getTerminalHeight(); if(H<=0)H=30;
    int cb=H-4-inputLineCount()-statusLineCount();
    return cb<3?3:cb;
}
int CLFTerminal::upperSepRow() { return contentBottom() + statusLineCount() + 1; }
int CLFTerminal::inputTopRow() { return upperSepRow() + 1; }

void CLFTerminal::recomputeLayout() {
    int H=getTerminalHeight(); if(H<=0)H=30;
    int cb=contentBottom();
    resetSR(); setSR(1,cb);
    s_layoutValid=true;
}

// ============ 分隔线 ============

void CLFTerminal::drawSeparators() {
    int H=getTerminalHeight(),W=getTerminalWidth();
    if(H<=0)H=30;if(W<=0)W=80;
    // 上分隔线
    int us=upperSepRow();
    std::string info="clfcode";
    std::string left="---";
    int pad=W-CLFAnsi::textWidth(left)-CLFAnsi::textWidth(info)-2;
    if(pad<1)pad=1;
    std::cout<<"\033["<<us<<";1H"<<CLFAnsi::gray(left+std::string(pad,'-')+" "+info)<<"\033[K"<<std::flush;
    // 下分隔线
    int ls=lowerSepRow();
    std::cout<<"\033["<<ls<<";1H"<<CLFAnsi::gray(std::string(W,'-'))<<"\033[K"<<std::flush;
}

// ============ 固定区全量渲染 ============

void CLFTerminal::renderFixedArea() {
    int H=getTerminalHeight(),W=getTerminalWidth();
    if(H<=0)H=30;if(W<=0)W=80;
    if(!CLFAnsi::isEnabled()||H<10)return;

    drawSeparators();

    // ⑥ StatusRegion
    int sr=contentBottom()+1;
    int sl=statusLineCount();
    for(int i=0;i<sl;++i){
        std::cout<<"\033["<<(sr+i)<<";1H\033[K"<<std::flush;
        if(i==0&&!s_statusLine.empty())
            std::cout<<"\033["<<(sr+i)<<";1H  "<<s_statusLine<<"\033[K"<<std::flush;
        else if(i>0&&i-1<(int)s_statusTree.size())
            std::cout<<"\033["<<(sr+i)<<";1H    "<<s_statusTree[i-1]<<"\033[K"<<std::flush;
    }

    // ⑤ InputRegion
    int it=inputTopRow();
    int N=inputLineCount();
    auto lines=splitLines(s_inputText);
    for(int i=0;i<N&&i<(int)lines.size();++i){
        int row=it+i;
        std::string prefix=(i==0)?"> ":"  ";
        std::cout<<"\033["<<row<<";1H"<<prefix<<lines[i]<<"\033[K"<<std::flush;
    }
    // 清除多余旧行（仅限输入区内部, 不触碰下分隔线和模式行）
    for(int i=(int)lines.size();i<N;++i)
        std::cout<<"\033["<<(it+i)<<";1H\033[K"<<std::flush;

    // ④ ModeLine
    int mr=modeRow();
    std::string left="  "+s_modeLabel+" mode on";
    std::string hints="shift+tab to cycle · esc to interrupt · /help for help";
    int pad=W-CLFAnsi::textWidth(left)-CLFAnsi::textWidth(hints)-2;
    if(pad<1)pad=1;
    std::cout<<"\033["<<mr<<";1H"<<CLFAnsi::gray(left+std::string(pad,' ')+hints)<<"\033[K"<<std::flush;

    // ③ ConfirmRegion
    int cr=confirmRow();
    if(s_confirmOpts.empty()){
        std::cout<<"\033["<<cr<<";1H\033[K"<<std::flush;
    } else {
        std::string d;
        for(size_t i=0;i<s_confirmOpts.size();++i){
            if(i>0)d+="    ";
            if((int)i==s_confirmSel)d+="["+green("●")+"] "+bold(s_confirmOpts[i]);
            else d+="[ ] "+gray(s_confirmOpts[i]);
        }
        std::cout<<"\033["<<cr<<";1H"<<d<<"\033[K"<<std::flush;
    }

    // 光标定位（输入区末行末尾）
    int cursorLine,cursorColW;
    cursorVisPos(s_inputText,s_inputCursor,cursorLine,cursorColW);
    cursorLine=std::min(cursorLine,(int)lines.size()-1);
    int prefixW=(cursorLine==0)?CLFAnsi::textWidth(">"):CLFAnsi::textWidth("  ");
    int col=prefixW+cursorColW+2;
    std::cout<<"\033["<<(it+cursorLine)<<";"<<col<<"H"<<std::flush;
}

// ============ ANSI — 委托 CLFAnsi ============

void CLFTerminal::enableAnsi(){CLFAnsi::enable();}
std::string CLFTerminal::green(const std::string& s){return CLFAnsi::green(s);}
std::string CLFTerminal::cyan(const std::string& s){return CLFAnsi::cyan(s);}
std::string CLFTerminal::lightBlue(const std::string& s){return CLFAnsi::lightBlue(s);}
std::string CLFTerminal::yellow(const std::string& s){return CLFAnsi::yellow(s);}
std::string CLFTerminal::red(const std::string& s){return CLFAnsi::red(s);}
std::string CLFTerminal::gray(const std::string& s){return CLFAnsi::gray(s);}
std::string CLFTerminal::bold(const std::string& s){return CLFAnsi::bold(s);}
int CLFTerminal::getTerminalHeight(){return CLFAnsi::terminalHeight();}
int CLFTerminal::getTerminalWidth(){return CLFAnsi::terminalWidth();}
int CLFTerminal::textWidth(const std::string& t){return CLFAnsi::textWidth(t);}
int CLFTerminal::wrappedLines(const std::string& t){return CLFAnsi::wrappedLines(t);}

// ============ ⑦ ContentRegion ============

void CLFTerminal::scrollPrint(const std::string& text){
    s_buffer.append(text);
    std::cout<<text<<std::flush;
}
void CLFTerminal::thoughtMark(int seconds, int searchCount, int readCount){
    if(seconds<=0)return;
    std::string msg="  Thought for "+std::to_string(seconds)+"s";
    if(searchCount>0)msg+=", searched for "+std::to_string(searchCount)+" pattern(s)";
    if(readCount>0)msg+=", read "+std::to_string(readCount)+" file(s)";
    scrollPrint("\n"+gray(msg)+"\n");
}

// ============ ⑥ StatusRegion ============

void CLFTerminal::showThinking(int seconds){
    s_statusLine="· Thinking… ("+std::to_string(seconds)+"s)";
    s_statusTree.clear();
    // 仅更新状态区行（不重绘整个固定区，避免后台线程干扰用户输入显示）
    int H=getTerminalHeight(); if(H<=0)H=30;
    if(!CLFAnsi::isEnabled()||H<10)return;
    int cb=contentBottom();
    std::cout<<"\033["<<(cb+1)<<";1H  "<<s_statusLine<<"\033[K"<<std::flush;
}
void CLFTerminal::showWorking(const std::string& title){
    s_statusLine=title;
    int H=getTerminalHeight(); if(H<=0)H=30;
    if(!CLFAnsi::isEnabled()||H<10)return;
    int cb=contentBottom();
    std::cout<<"\033["<<(cb+1)<<";1H  "<<s_statusLine<<"\033[K"<<std::flush;
}
void CLFTerminal::showTaskTree(const std::vector<std::string>& phases){
    s_statusTree=phases;
    if(s_statusTree.size()>10)s_statusTree.resize(10);
    recomputeLayout(); // 行数变化, 需重设滚动区
    renderFixedArea();
}
void CLFTerminal::clearStatus(){
    s_statusLine.clear();
    s_statusTree.clear();
    // 仅清除状态区显示, 不重置 DECSTBM(避免打断流式输出)
    int H=getTerminalHeight(); if(H<=0)H=30;
    if(!CLFAnsi::isEnabled()||H<10)return;
    int cb=contentBottom();
    int sl=statusLineCount()+1; // 旧状态可能占多行
    for(int i=0;i<sl;++i)
        std::cout<<"\033["<<(cb+1+i)<<";1H\033[K"<<std::flush;
    std::cout<<std::flush;
}

// ============ ⑤ InputRegion ============

void CLFTerminal::drawInput(const std::string& text, int cursorPos){
    int oldIL=inputLineCount();
    s_inputText=text;
    s_inputCursor=(cursorPos<0)?(int)text.size():cursorPos;
    int newIL=inputLineCount();
    if(oldIL!=newIL) recomputeLayout();
    renderFixedArea();
}

// ============ ④ ModeLine ============

void CLFTerminal::drawMode(const std::string& mode){
    if(s_modeLabel==mode)return;
    s_modeLabel=mode;
    // 仅更新模式行(不重绘全部固定区)
    int H=getTerminalHeight(),W=getTerminalWidth();
    if(H<=0)H=30;if(W<=0)W=80;
    if(!CLFAnsi::isEnabled()||H<10)return;
    std::cout<<"\0337";
    int mr=modeRow();
    std::string left="  "+s_modeLabel+" mode on";
    std::string hints="shift+tab to cycle · esc to interrupt · /help for help";
    int pad=W-CLFAnsi::textWidth(left)-CLFAnsi::textWidth(hints)-2;
    if(pad<1)pad=1;
    std::cout<<"\033["<<mr<<";1H"<<gray(left+std::string(pad,' ')+hints)<<"\033[K"<<std::flush;
    std::cout<<"\0338"<<std::flush;
}

// ============ ③ ConfirmRegion ============

void CLFTerminal::showConfirm(const std::vector<std::string>& options, int selected){
    s_confirmOpts=options; s_confirmSel=selected;
    int H=getTerminalHeight(),cr=confirmRow();
    if(!CLFAnsi::isEnabled()||H<10)return;
    std::cout<<"\0337";
    std::string d;
    for(size_t i=0;i<options.size();++i){
        if(i>0)d+="    ";
        if((int)i==selected)d+="["+green("●")+"] "+bold(options[i]);
        else d+="[ ] "+gray(options[i]);
    }
    std::cout<<"\033["<<cr<<";1H"<<d<<"\033[K"<<std::flush;
    std::cout<<"\0338"<<std::flush;
}
void CLFTerminal::hideConfirm(){
    s_confirmOpts.clear();
    int H=getTerminalHeight(),cr=confirmRow();
    if(!CLFAnsi::isEnabled()||H<10)return;
    std::cout<<"\0337";
    std::cout<<"\033["<<cr<<";1H\033[K"<<std::flush;
    std::cout<<"\0338"<<std::flush;
}

// ============ 布局初始化 / 重绘 ============

void CLFTerminal::initLayout(const std::string& modeLabel){
    s_modeLabel=modeLabel;
    s_inputText.clear(); s_inputCursor=0;
    s_statusLine.clear(); s_statusTree.clear();
    s_confirmOpts.clear();
    s_buffer.clear();

    int H=getTerminalHeight(),W=getTerminalWidth();
    if(H<=0)H=30;if(W<=0)W=80;
    if(!CLFAnsi::isEnabled()||H<10){std::cout<<"\033[2J\033[H"<<std::flush;return;}

    std::cout<<"\033[2J\033[H"<<std::flush;
    recomputeLayout();
    renderFixedArea();
    std::cout<<"\033[H"<<std::flush;
}

void CLFTerminal::redrawAll(){
    int H=getTerminalHeight(),W=getTerminalWidth();
    if(H<=0)H=30;if(W<=0)W=80;
    if(!CLFAnsi::isEnabled()||H<10){
        std::cout<<"\033[2J\033[H"<<std::flush;
        for(auto& l:s_buffer.lines())std::cout<<l<<"\n"<<std::flush;
        s_layoutValid=false; drawInput(s_inputText,s_inputCursor);
        return;
    }
    std::cout<<"\033[2J\033[H"<<std::flush;
    recomputeLayout();
    // 重放缓冲(拼接,让终端根据当前宽度自行换行)
    int cb=contentBottom();
    const auto& lines=s_buffer.lines();
    size_t vis=(size_t)(cb-1),start=(lines.size()>vis)?lines.size()-vis:0;
    for(size_t i=start;i<lines.size();++i){
        if(i>start)std::cout<<"\n";
        std::cout<<stripAnsi(lines[i]);
    }
    std::cout<<std::flush;
    renderFixedArea();
    std::cout<<"\033[H"<<std::flush;
}

void CLFTerminal::restoreScrollRegion(){
    resetSR();
    if(CLFAnsi::isEnabled())std::cout<<"\033[2J\033[H"<<std::flush;
}
std::string CLFTerminal::diagnosticInfo(){
    return "终端: 高"+std::to_string(getTerminalHeight())+" x 宽"+std::to_string(getTerminalWidth())
           +", ANSI: "+(CLFAnsi::isEnabled()?"开":"关");
}

// ============ 兼容旧 API ============

void CLFTerminal::drawStatusArea(const std::string& title, const std::string& content){
    if(!title.empty()) s_statusLine="▍ "+title;
    if(!content.empty()){
        s_statusTree.clear();
        s_statusTree.push_back(content);
    }
    recomputeLayout();
    if(!title.empty()||!content.empty()) renderFixedArea();
}

void CLFTerminal::toContentArea(){
    s_inputText.clear(); s_inputCursor=0;
    s_statusLine.clear(); s_statusTree.clear();
    s_confirmOpts.clear();
    recomputeLayout();
    int H=getTerminalHeight(),W=getTerminalWidth();
    if(H<=0)H=30;if(W<=0)W=80;
    if(!CLFAnsi::isEnabled()||H<10){std::cout<<"\r\033[K\n\033[K\n\033[K\n"<<std::flush;return;}
    // 清屏 + 重绘固定区 + 光标回滚动区顶部(让后续输出从第1行开始自然填充)
    std::cout<<"\033[2J\033[H"<<std::flush;
    renderFixedArea();
    std::cout<<"\033[H"<<std::flush;  // row 1, col 1 — 滚动区顶部
}

} // namespace CLF::CLFCore
