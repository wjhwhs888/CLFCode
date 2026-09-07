// CLFSubprocessRunner.cpp — 子进程命令执行封装实现
// C5 拆分：逻辑自 CLFSystemPromptBuilder::execCommand 原样搬移

#include "CLFCore/CLFSubprocessRunner.hpp"

#include <cstdio>

#ifdef _WIN32
#define popen  _popen
#define pclose _pclose
#endif

namespace CLF::CLFCore {

std::string CLFSubprocessRunner::run(const std::string& cmd) {
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe)) {
        result += buf;
    }
    pclose(pipe);
    if (!result.empty() && result.back() == '\n') result.pop_back();
    return result;
}

} // namespace CLF::CLFCore
