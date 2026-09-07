// CLFSubprocessRunner.hpp — 子进程命令执行封装（C5：Builder 拆分，2026-09-07）
// popen/_popen 跨平台封装（P1-14 后半）：CLFCommandExec 迁插件后 core 不可
// 依赖之——本封装留在 core，供系统信息捕获等轻量子进程读取。
//
// example:
//   std::string ver = CLFSubprocessRunner::run("ver 2>nul");

#pragma once

#include <string>

namespace CLF::CLFCore {

class CLFSubprocessRunner {
public:
    // 执行命令并返回 stdout（去掉末尾单个换行）；打开失败/空输出 → 空串
    static std::string run(const std::string& cmd);
};

} // namespace CLF::CLFCore
