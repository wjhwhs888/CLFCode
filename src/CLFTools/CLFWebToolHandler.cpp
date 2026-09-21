// CLFWebToolHandler.cpp — web_fetch handler 共享实现（2.3，2026-09-21）
// 自 CLFBuiltinTools.cpp 迁入（逻辑原样保真——url 必填容错语义不变）。

#include "CLFTools/CLFWebToolHandler.hpp"

#include <nlohmann/json.hpp>

#include "CLFCapabilities/CLFHandlerScaffold.hpp"   // withHandlerScaffold（2.2a 能力域共享）
#include "CLFTools/CLFWebFetch.hpp"

namespace CLF::CLFTools {

std::string webFetchToolHandler(const std::string& args) {
    return CLF::CLFCapabilities::withHandlerScaffold(
        args, [](const nlohmann::json& params, nlohmann::json& result) {
            CLFWebRequest req;
            req.m_url        = params.value("url", "");
            req.m_method     = params.value("method", "GET");
            req.m_body       = params.value("body", "");
            req.m_timeoutSec = params.value("timeout", 15);
            if (params.contains("headers") && params["headers"].is_object()) {
                for (auto it = params["headers"].begin(); it != params["headers"].end(); ++it) {
                    if (it.value().is_string()) {
                        req.m_headers[it.key()] = it.value().get<std::string>();
                    }
                }
            }
            if (req.m_url.empty()) {
                result["success"] = false;
                result["error"]   = "url is required";
                return;
            }

            const auto resp = CLF::CLFTools::webFetch(req);
            result["success"] = resp.m_success;
            if (!resp.m_success) {
                result["error"] = resp.m_error;
                return;
            }
            result["status"]  = resp.m_status;
            result["headers"] = resp.m_headers;
            result["body"]    = resp.m_body;
            if (resp.m_truncated) result["truncated"] = true;
            if (resp.m_binary)    result["binary"]    = true;
        });
}

} // namespace CLF::CLFTools
