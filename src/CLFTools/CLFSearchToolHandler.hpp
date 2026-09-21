// CLFSearchToolHandler.hpp — search_content 工具 handler 共享实现（2.3，2026-09-21）
// 双消费者：CLFBuiltinTools（宿主静态注册）与 tools.search 插件 DLL。
// 只依赖能力（CLFSearchContent）+ clf_types + nlohmann json——不依赖 core/UI。
// example:
//   std::string out = CLF::CLFTools::searchContentToolHandler(args);

#pragma once

#include <string>

namespace CLF::CLFTools {

std::string searchContentToolHandler(const std::string& args);

} // namespace CLF::CLFTools
