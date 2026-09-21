// CLFHandlerScaffold.hpp — handler 脚手架（2.3 自 CLFFileOpsHandlers 独立）
// 统一 parse / try-catch / dump 骨架（A4a 样板）：错误文案统一 "Handler error: "。
// 独立成文件的原因：各域插件只编入本文件（withHandlerScaffold），不连带
// FileOps handler 的能力符号（2.3 实抓：整文件编入 command 插件会导致
// CLFFileOps 未解析符号）。
// example:
//   return CLF::CLFCapabilities::withHandlerScaffold(
//       args, [](const nlohmann::json& params, nlohmann::json& result) {
//           result["success"] = true;
//       });

#pragma once

#include <functional>
#include <string>

#include <nlohmann/json.hpp>

namespace CLF::CLFCapabilities {

std::string withHandlerScaffold(
    const std::string& args,
    const std::function<void(const nlohmann::json& params, nlohmann::json& result)>& body);

} // namespace CLF::CLFCapabilities
