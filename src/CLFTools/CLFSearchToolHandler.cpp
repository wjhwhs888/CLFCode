// CLFSearchToolHandler.cpp — search_content handler 共享实现（2.3，2026-09-21）
// 自 CLFBuiltinTools.cpp 迁入（逻辑原样保真——纯文本匹配语义不变）。

#include "CLFTools/CLFSearchToolHandler.hpp"

#include <nlohmann/json.hpp>

#include "CLFCapabilities/CLFHandlerScaffold.hpp"   // withHandlerScaffold（2.2a 能力域共享）
#include "CLFTools/CLFSearchContent.hpp"

namespace CLF::CLFTools {

std::string searchContentToolHandler(const std::string& args) {
    return CLF::CLFCapabilities::withHandlerScaffold(
        args, [](const nlohmann::json& params, nlohmann::json& result) {
            std::string pattern   = params.value("pattern", "");
            std::string directory = params.value("directory", ".");
            std::string fileTypes = params.value("fileTypes", "");
            result["success"] = true;
            result["content"] = searchContent(pattern, directory, fileTypes);
        });
}

} // namespace CLF::CLFTools
