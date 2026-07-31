// CLFConfigLoader.cpp — Agent 配置加载器实现

#include "CLFCore/CLFConfigLoader.hpp"
#include "CLFCore/CLFLogger.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace CLF::CLFCore {

std::string CLFConfigLoader::s_projectRoot;

std::string CLFConfigLoader::findProjectRoot() {
    if (!s_projectRoot.empty()) return s_projectRoot;

    // 1. 获取可执行文件所在目录
    std::string exeDir;
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, buf, sizeof(buf));
    if (len > 0 && len < sizeof(buf)) {
        exeDir = fs::path(buf).parent_path().string();
    }
#else
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        exeDir = fs::path(buf).parent_path().string();
    }
#endif

    // 2. 从 exe 目录向上查找 CMakeLists.txt
    fs::path dir = exeDir.empty() ? fs::current_path() : fs::path(exeDir);
    while (!dir.empty() && dir != dir.root_path()) {
        if (fs::exists(dir / "CMakeLists.txt")) {
            s_projectRoot = dir.string();
            return s_projectRoot;
        }
        dir = dir.parent_path();
    }

    // 3. 找不到则回退到 CWD
    s_projectRoot = fs::current_path().string();
    return s_projectRoot;
}

const std::string& CLFConfigLoader::getProjectRoot() {
    if (s_projectRoot.empty()) findProjectRoot();
    return s_projectRoot;
}

std::string CLFConfigLoader::resolvePath(const std::string& relativePath) {
    if (s_projectRoot.empty()) findProjectRoot();
    return s_projectRoot + "/" + relativePath;
}

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

        // —— logging（日志配置）——
        if (cfg.contains("logging")) {
            const auto& logging = cfg["logging"];
            if (logging.contains("level") && logging["level"].is_string()) {
                outConfig.m_logLevel = logging["level"].get<std::string>();
            }
            if (logging.contains("file") && logging["file"].is_string()) {
                outConfig.m_logFile = logging["file"].get<std::string>();
            }
            if (logging.contains("console") && logging["console"].is_boolean()) {
                outConfig.m_logConsole = logging["console"].get<bool>();
            }
        }

        return true;
    } catch (const json::exception& e) {
        CLFLogger::instance().error(std::string("ConfigLoader JSON parse error: ") + e.what());
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

} // namespace CLF::CLFCore
