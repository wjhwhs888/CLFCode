// CLFPlatform.hpp — 平台能力层（设计-平台层收敛 §四，2026-09-23）
// Windows 平台耦合的唯一接缝：路径 / 动态库 / 控制台 / 剪贴板 / 临时文件
// 原语——"只搬取数，不搬算式"（坐标算式、补偿值、策略判断留在调用方；
// §4.3 不下沉清单为硬约束）。
// 第一期 = Windows 实现；非 Windows 分支显式保留 + [未验证] 标记（第二期
// 真编译——CentOS 7 = glibc 2.17 为兼容底线，设计-平台层收敛 §十五）。
// 裁剪注记（总排期冲突 A）：nullDevice() 不设立——argv 化后 `2>nul` 整类
// 消除、`ver` 已改 RtlGetVersion 原语，该接口无消费方。
// 命名空间/风格随 CLFEncoding 先例（clf_types 底层组件）。
//
// example:
//   std::string exeDir = CLFPlatform::executableDir();
//   int w = 0, h = 0; CLFPlatform::consoleSize(w, h);

#pragma once

#include <string>

namespace CLF::CLFCore {

class CLFPlatform {
public:
    // —— 路径 ——
    // 可执行文件所在目录（UTF-8 窄串——u8string 中文路径安全）；失败返回
    // 空串（调用方回落 fs::current_path）
    static std::string executableDir();

    // —— 动态库 ——
    // 平台动态库文件扩展名（".dll" / ".so"）；返回静态字面量
    static const char* dynamicLibraryExtension();
    // 抑制系统错误弹窗（坏 PE 加载失败时 Windows 弹"损坏的映像"框——CLI
    // 程序被弹窗卡住不可接受，2026-09-21 用户实测实抓）；返回旧模式供
    // restore 往返恢复；POSIX 无系统弹窗概念 → no-op 返回 0
    static unsigned suppressErrorDialogs();
    static void restoreErrorDialogs(unsigned oldMode);

    // —— 控制台 ——
    // 开启虚拟终端处理（VT 转义支持）；成功返回 true
    static bool enableVirtualTerminal();
    // 控制台窗口尺寸（列 × 行）；失败返回 false（无控制台环境允许失败，
    // 调用方按需回落——不 flaky）
    static bool consoleSize(int& columns, int& rows);
    // 原始输入模式：清 ENABLE_PROCESSED_INPUT——否则系统把 Ctrl+C 转信号，
    // 事件到不了应用层（验收实证的选区态 Ctrl+C 失效根因）
    static bool setRawInputMode();
    // 控制台编码初始化：stdin/stdout 代码页设为 UTF-8
    static bool initConsoleEncoding();
    // 控制台光标位置（**1 基**坐标——基准口径与 SGR 鼠标 raw 一致）；
    // 失败返回 false
    static bool consoleCursorPosition(int& x, int& y);

    // —— 剪贴板 ——
    // 读取系统剪贴板文本（UTF-8 净化后）；失败/空 → 空串
    static std::string readClipboard();
    // 写入系统剪贴板；成功返回 true
    static bool writeClipboard(const std::string& text);

    // —— 临时文件 ——
    // 生成临时文件**绝对路径**（Windows 读 TEMP / POSIX 读 TMPDIR），
    // **不创建文件**（是否创建由调用方决定）；失败返回空串
    static std::string makeTempFilePath(const std::string& prefix);
};

} // namespace CLF::CLFCore
