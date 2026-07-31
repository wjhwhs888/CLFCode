// CLFConfigLoader.cpp — Agent 配置加载器实现

#include "CLFCore/CLFConfigLoader.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <cstdlib>

using json = nlohmann::json;

namespace CLF::CLFCore {

bool CLFConfigLoader::loadFromFile(const std::string& configPath, CLFAgentConfig& outConfig) {
    std::ifstream file(configPath);
    if (!file.is_open()) {
        return false;
    }

    try {
        json cfg = json::parse(file);

        // —— connection（连接认证）——
        if (cfg.contains("connection")) {
            const auto& conn = cfg["connection"];
            if (conn.contains("base_url") && conn["base_url"].is_string()) {
                outConfig.m_apiBaseUrl = conn["base_url"].get<std::string>();
            }
            if (conn.contains("api_key") && conn["api_key"].is_string()) {
                outConfig.m_apiKey = conn["api_key"].get<std::string>();
            }
        }

        // —— chat_completions（对齐 DeepSeek API 参数）——
        if (cfg.contains("chat_completions")) {
            const auto& cc = cfg["chat_completions"];
            if (cc.contains("model") && cc["model"].is_string()) {
                outConfig.m_modelName = cc["model"].get<std::string>();
            }
            if (cc.contains("sub_model") && cc["sub_model"].is_string()) {
                outConfig.m_subModel = cc["sub_model"].get<std::string>();
            }
            if (cc.contains("temperature") && cc["temperature"].is_number()) {
                outConfig.m_temperature = cc["temperature"].get<float>();
            }
            if (cc.contains("max_tokens") && cc["max_tokens"].is_number()) {
                outConfig.m_maxTokens = cc["max_tokens"].get<int>();
            }
            if (cc.contains("top_p") && cc["top_p"].is_number()) {
                outConfig.m_topP = cc["top_p"].get<float>();
            }
            if (cc.contains("stream") && cc["stream"].is_boolean()) {
                outConfig.m_stream = cc["stream"].get<bool>();
            }
            if (cc.contains("frequency_penalty") && cc["frequency_penalty"].is_number()) {
                outConfig.m_frequencyPenalty = cc["frequency_penalty"].get<float>();
            }
            if (cc.contains("presence_penalty") && cc["presence_penalty"].is_number()) {
                outConfig.m_presencePenalty = cc["presence_penalty"].get<float>();
            }
            if (cc.contains("response_format") && cc["response_format"].is_string()) {
                outConfig.m_responseFormat = cc["response_format"].get<std::string>();
            }
            if (cc.contains("stop") && cc["stop"].is_array()) {
                for (const auto& s : cc["stop"]) {
                    if (s.is_string()) {
                        outConfig.m_stop.push_back(s.get<std::string>());
                    }
                }
            }
            if (cc.contains("thinking_level") && cc["thinking_level"].is_string()) {
                outConfig.m_thinkingLevel = cc["thinking_level"].get<std::string>();
            }
        }

        // —— agent（Agent 行为参数）——
        if (cfg.contains("agent")) {
            const auto& agent = cfg["agent"];
            if (agent.contains("max_context_window") && agent["max_context_window"].is_number()) {
                outConfig.m_maxContextWindow = agent["max_context_window"].get<int>();
            }
            if (agent.contains("max_tool_call_iterations") && agent["max_tool_call_iterations"].is_number()) {
                outConfig.m_maxToolCallIterations = agent["max_tool_call_iterations"].get<int>();
            }
            if (agent.contains("context_compression") && agent["context_compression"].is_boolean()) {
                outConfig.m_contextCompression = agent["context_compression"].get<bool>();
            }
            if (agent.contains("max_response_delay_sec") && agent["max_response_delay_sec"].is_number()) {
                outConfig.m_maxResponseDelaySec = agent["max_response_delay_sec"].get<int>();
            }
            if (agent.contains("interaction_language") && agent["interaction_language"].is_string()) {
                outConfig.m_interactionLanguage = agent["interaction_language"].get<std::string>();
            }
        }

        return true;
    } catch (const json::exception& e) {
        std::cerr << "[ConfigLoader] JSON parse error: " << e.what() << std::endl;
        return false;
    }
}

bool CLFConfigLoader::loadFromFileWithEnv(const std::string& configPath, CLFAgentConfig& outConfig) {
    bool fileOk = loadFromFile(configPath, outConfig);

    // 环境变量覆盖（优先级高于配置文件）
    const char* envApiKey = std::getenv("CLF_API_KEY");
    if (envApiKey && envApiKey[0] != '\0') {
        outConfig.m_apiKey = envApiKey;
    }

    const char* envBaseUrl = std::getenv("CLF_API_BASE_URL");
    if (envBaseUrl && envBaseUrl[0] != '\0') {
        outConfig.m_apiBaseUrl = envBaseUrl;
    }

    const char* envModel = std::getenv("CLF_MODEL");
    if (envModel && envModel[0] != '\0') {
        outConfig.m_modelName = envModel;
    }

    return fileOk;
}

std::string CLFConfigLoader::resolveConfigPath(const std::string& relativePath) {
    namespace fs = std::filesystem;
    std::vector<std::string> candidates = {
        relativePath,
        "../../" + relativePath,
        "../" + relativePath,
    };
    for (const auto& p : candidates) {
        if (fs::exists(p)) {
            return p;
        }
    }
    return relativePath; // fallback
}

} // namespace CLF::CLFCore
