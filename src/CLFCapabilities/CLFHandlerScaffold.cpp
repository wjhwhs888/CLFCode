// CLFHandlerScaffold.cpp — handler 脚手架实现（2.3 自 CLFFileOpsHandlers 独立）

#include "CLFCapabilities/CLFHandlerScaffold.hpp"

namespace CLF::CLFCapabilities {

std::string withHandlerScaffold(
    const std::string& args,
    const std::function<void(const nlohmann::json& params, nlohmann::json& result)>& body) {
    nlohmann::json result;
    try {
        nlohmann::json params = nlohmann::json::parse(args);
        body(params, result);
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"]   = std::string("Handler error: ") + e.what();
    }
    return result.dump();
}

} // namespace CLF::CLFCapabilities
