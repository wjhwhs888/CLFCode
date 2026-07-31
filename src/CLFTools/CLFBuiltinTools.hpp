// CLFBuiltinTools.hpp — 内置工具注册
// 将所有内置工具（文件操作、命令执行、系统信息等）一次性注册到 Agent
//
// example:
//   CLF::CLFCore::CLFAgentLoop agent(config);
//   CLF::CLFTools::registerBuiltinTools(agent);

#pragma once

namespace CLF::CLFCore {
class CLFAgentLoop;
}

namespace CLF::CLFTools {

// 向 Agent 注册全部内置工具
// 包括：read_file, write_file, list_directory, execute_command,
//       get_current_time, echo
void registerBuiltinTools(CLF::CLFCore::CLFAgentLoop& agent);

} // namespace CLF::CLFTools
