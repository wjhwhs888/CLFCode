// CLFFileOpsHandlers.hpp — 文件工具 handler 共享实现（2.2a，2026-09-21）
// 双消费者：CLFBuiltinTools（宿主静态注册，2.2b 前）与 tools.fileops 插件 DLL。
// 只依赖能力层（CLFFileOps）+ clf_types（CLFTextUtil）+ nlohmann json（第三方）
// ——不依赖 core/UI（插件不可链 core，§二 分层约束）。
// 2.2b 宿主装配切换插件后，CLFBuiltinTools 的注册删除、本文件仅剩插件消费者。
//
// example:
//   std::string out = CLF::CLFCapabilities::writeFileToolHandler(args);

#pragma once

#include <functional>
#include <string>

#include <nlohmann/json.hpp>

namespace CLF::CLFCapabilities {

// handler 脚手架（A4a 样板，自 CLFBuiltinTools 迁入供双消费者共用）：
// 统一 parse / try-catch / dump 骨架；错误文案统一 "Handler error: "
// example:
//   return withHandlerScaffold(args, [](const nlohmann::json& params, nlohmann::json& result) {
//       result["success"] = true;
//   });
std::string withHandlerScaffold(
    const std::string& args,
    const std::function<void(const nlohmann::json& params, nlohmann::json& result)>& body);

// 路径是否位于工作区（参数化版——插件不可链 core 的 CLFConfigLoader，
// 工作区根由调用方传入）：weakly_canonical 跟随 symlink/junction 防软链接逃逸；
// 逐段比较而非字符串前缀（防 "proj-evil" 误判在 "proj" 内）。
// workspaceRootUtf8 为空串 = 跳过校验（返回 true）。
// example:
//   std::string err;
//   if (!isWithinWorkspaceOf(root, path, err)) reject(err);
bool isWithinWorkspaceOf(const std::string& workspaceRootUtf8,
                         const std::string& path, std::string& outError);

// read_file：allowAbsolute=true 跳过边界校验；workspaceRootUtf8 为空串 = 跳过
// （2.2a 阶段插件侧传空串——2.2b 切换前校验归属定案，见 2.2a 设计 §二）
std::string readFileToolHandler(const std::string& args,
                                bool allowAbsolute,
                                const std::string& workspaceRootUtf8);

// write_file / edit_file / list_directory（零 core 依赖，直接共享）
std::string writeFileToolHandler(const std::string& args);
std::string editFileToolHandler(const std::string& args);
std::string listDirectoryToolHandler(const std::string& args);

} // namespace CLF::CLFCapabilities
