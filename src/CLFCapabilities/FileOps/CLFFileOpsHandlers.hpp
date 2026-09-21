// CLFFileOpsHandlers.hpp — 文件工具 handler 共享实现（2.2a，2026-09-21）
// 双消费者：CLFBuiltinTools（宿主静态注册，2.2b 前）与 tools.fileops 插件 DLL。
// 只依赖能力层（CLFFileOps）+ clf_types（CLFTextUtil）+ nlohmann json（第三方）
// ——不依赖 core/UI（插件不可链 core，§二 分层约束）。
// 2.2b 宿主装配切换插件后，CLFBuiltinTools 的注册删除、本文件仅剩插件消费者。
//
// example:
//   std::string out = CLF::CLFCapabilities::writeFileToolHandler(args);

#pragma once

#include "CLFCapabilities/CLFHandlerScaffold.hpp"
#include <string>

namespace CLF::CLFCapabilities {

// （2.3：withHandlerScaffold 已独立为 CLFHandlerScaffold——各域插件只编入
// 脚手架，不连带本文件的能力符号）

// read_file：allowAbsolute=true 跳过边界校验；workspaceRootUtf8 为空串 = 跳过
// （2.2b 定案：保持 handler 层校验——插件经 host->config 取根）。
// 边界判定用 CLFTextUtil::isWithinWorkspaceOf（2.3 归位——插件可链 clf_types）
std::string readFileToolHandler(const std::string& args,
                                bool allowAbsolute,
                                const std::string& workspaceRootUtf8);

// write_file / edit_file / list_directory（零 core 依赖，直接共享）
std::string writeFileToolHandler(const std::string& args);
std::string editFileToolHandler(const std::string& args);
std::string listDirectoryToolHandler(const std::string& args);

} // namespace CLF::CLFCapabilities
