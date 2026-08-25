// CLFBuiltinTools.hpp — 内置工具注册
// 将所有内置工具（文件操作、命令执行、系统信息等）一次性注册到 Agent
//
// example:
//   CLF::CLFCore::CLFAgentLoop agent(config);
//   CLF::CLFTools::registerBuiltinTools(agent);

#pragma once

#include <string>

namespace CLF::CLFCore {
class CLFAgentLoop;
}

namespace CLF::CLFTools {

// 向 Agent 注册全部内置工具
// 包括：read_file, write_file, edit_file, list_directory, search_content,
//       execute_command, get_current_time, echo
void registerBuiltinTools(CLF::CLFCore::CLFAgentLoop& agent);

// ============================================================================
// 内部辅助函数——暴露仅为单测可达，正常使用无需直接调用
// ============================================================================
namespace detail {

//判定路径是否位于工作区（cwd）之内（S2-1）
// 用 weakly_canonical 跟随 symlink/junction，防软链接逃逸；
// 逐段比较而非字符串前缀，避免 "proj-evil" 被误判在 "proj" 内
// example:
//   std::string err;
//   if (!detail::isWithinWorkspace(path, err)) reject(err);
bool isWithinWorkspace(const std::string& path, std::string& outError);

//判定命令退出码是否应视为成功（S2-3 退出码白名单）
// grep/rg/findstr/diff/fc 的退出码 1 表示"无匹配/有差异"，属正常结果
// example:
//   detail::exitCodeMeansSuccess("grep foo a.txt", 1);  // true
bool exitCodeMeansSuccess(const std::string& command, int exitCode);

//按行切片（offset 为 0 基起始行，limit<=0 表示取到末尾）
// example:
//   detail::sliceLines(content, 10, 20);  // 第 10-29 行
std::string sliceLines(const std::string& content, int offset, int limit);

} // namespace detail

} // namespace CLF::CLFTools
