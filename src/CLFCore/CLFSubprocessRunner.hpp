// CLFSubprocessRunner.hpp — 子进程命令执行封装（C5：Builder 拆分，2026-09-07）
// argv 模式门面（命令执行层 §九，2026-09-23）：popen（必然过 shell）→
// CLFProcessRunner argv 模式（不经 shell）——`2>nul` 类重定向缺陷的整类
// 消除（shell 重定向语义随 shell 而走，argv 下 stderr 由执行器单独捕获）。
// 供系统信息捕获等宿主内部辅助命令使用（程序知道自己要跑什么 = argv）。
//
// example:
//   std::string ver = CLFSubprocessRunner::run({"git", "-C", root, "status", "--short"});

#pragma once

#include <string>
#include <vector>

namespace CLF::CLFCore {

class CLFSubprocessRunner {
public:
    // 执行命令并返回 stdout（去掉末尾单个换行）；打开失败/空输出 → 空串。
    // argv[0] = 程序名（PATH 搜索）；stderr 单独捕获后丢弃（与原 popen
    // 只读 stdout 语义一致——调用方仅消费 stdout）
    static std::string run(const std::vector<std::string>& argv);
};

} // namespace CLF::CLFCore
