// CLFCommandToolHandler.hpp — execute_command 工具 handler 共享实现（2.3，2026-09-21）
// 双消费者：CLFBuiltinTools（宿主静态注册，2.3 切换前）与 tools.command 插件 DLL。
// 只依赖能力（CLFCommandExec）+ clf_types + nlohmann json——不依赖 core/UI。
// example:
//   std::string out = CLF::CLFTools::executeCommandToolHandler(args, workspaceRoot);
//   std::string out2 = CLF::CLFTools::executeCommandToolHandler(
//       args, workspaceRoot, [] { return interrupted.load(); });

#pragma once

#include <functional>
#include <string>

namespace CLF::CLFTools {

// 判定命令的退出码是否应视为成功（S2-3 退出码白名单——自 CLFBuiltinTools
// 迁入共享；grep/diff 一类退出码 1 = "无匹配/有差异"属正常结果）
bool exitCodeMeansSuccess(const std::string& command, int exitCode);

// execute_command：workspaceRootUtf8 为空串 = cwd 校验跳过
// （2.3 参数化——插件经 host->config 取根；CLFBuiltinTools 传 ConfigLoader 值）
// isCancelled 为空 = 不可取消（向后兼容）；命中 → 结果含 interrupted=true
std::string executeCommandToolHandler(const std::string& args,
                                      const std::string& workspaceRootUtf8,
                                      const std::function<bool()>& isCancelled = {});

} // namespace CLF::CLFTools
