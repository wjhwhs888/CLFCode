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

namespace {

// ============================================================================
// C6（2026-09-07）表驱动配置映射（P1-15）：{section, key, 类型, 目标槽} 静态表
// 替代 30+ if(contains) 样板——新配置项 = CLFAgentConfig 加字段 + 下表加一行。
// 成员指针为编译期常量（POD 平凡结构，无静态构造/析构问题）。
// 类型枚举的读取语义（行为保真，逐条对照原内联代码）：
//   String/Int/Float/Bool：is_<type> 过滤后赋值
//   StringListAppend：数组逐项追加（stop——原语义不清空）
//   StringListReplace：清空后逐项追加（command_allowlist——原语义 clear 先行）
//   IntMap：对象逐键 is_number 过滤（model_max_tokens）
// ============================================================================

enum class ConfigFieldType { String, Int, Float, Bool, StringListAppend, StringListReplace, IntMap };

struct ConfigField {
    const char* section;
    const char* key;
    ConfigFieldType type;
    // 目标槽（按 type 仅填一个；其余保持默认 nullptr）
    std::string CLFAgentConfig::* strField = nullptr;
    int    CLFAgentConfig::* intField = nullptr;
    float  CLFAgentConfig::* floatField = nullptr;
    bool   CLFAgentConfig::* boolField = nullptr;
    std::vector<std::string> CLFAgentConfig::* listField = nullptr;
    std::map<std::string, int> CLFAgentConfig::* mapField = nullptr;
};

constexpr ConfigField kConfigFields[] = {
    // —— connection（连接认证）——
    {"connection", "base_url", ConfigFieldType::String, &CLFAgentConfig::m_apiBaseUrl},
    {"connection", "api_key",  ConfigFieldType::String, &CLFAgentConfig::m_apiKey},
    // —— chat_completions（对齐 DeepSeek API 参数）——
    {"chat_completions", "model",             ConfigFieldType::String, &CLFAgentConfig::m_modelName},
    {"chat_completions", "sub_model",         ConfigFieldType::String, &CLFAgentConfig::m_subModel},
    {"chat_completions", "temperature",       ConfigFieldType::Float, nullptr, nullptr, &CLFAgentConfig::m_temperature},
    {"chat_completions", "max_tokens",        ConfigFieldType::Int, nullptr, &CLFAgentConfig::m_maxTokens},
    {"chat_completions", "top_p",             ConfigFieldType::Float, nullptr, nullptr, &CLFAgentConfig::m_topP},
    {"chat_completions", "stream",            ConfigFieldType::Bool, nullptr, nullptr, nullptr, &CLFAgentConfig::m_stream},
    {"chat_completions", "frequency_penalty", ConfigFieldType::Float, nullptr, nullptr, &CLFAgentConfig::m_frequencyPenalty},
    {"chat_completions", "presence_penalty",  ConfigFieldType::Float, nullptr, nullptr, &CLFAgentConfig::m_presencePenalty},
    {"chat_completions", "response_format",   ConfigFieldType::String, &CLFAgentConfig::m_responseFormat},
    {"chat_completions", "stop",              ConfigFieldType::StringListAppend, nullptr, nullptr, nullptr, nullptr, &CLFAgentConfig::m_stop},
    {"chat_completions", "thinking_level",    ConfigFieldType::String, &CLFAgentConfig::m_thinkingLevel},
    // —— agent（Agent 行为参数）——
    {"agent", "max_context_window",       ConfigFieldType::Int, nullptr, &CLFAgentConfig::m_maxContextWindow},
    {"agent", "max_tool_call_iterations", ConfigFieldType::Int, nullptr, &CLFAgentConfig::m_maxToolCallIterations},
    {"agent", "context_compression",      ConfigFieldType::Bool, nullptr, nullptr, nullptr, &CLFAgentConfig::m_contextCompression},
    {"agent", "auto_summary_threshold",   ConfigFieldType::Int, nullptr, &CLFAgentConfig::m_autoSummaryThreshold},
    {"agent", "model_max_tokens",         ConfigFieldType::IntMap, nullptr, nullptr, nullptr, nullptr, nullptr, &CLFAgentConfig::m_modelMaxTokens},
    {"agent", "max_response_delay_sec",   ConfigFieldType::Int, nullptr, &CLFAgentConfig::m_maxResponseDelaySec},
    {"agent", "interaction_language",     ConfigFieldType::String, &CLFAgentConfig::m_interactionLanguage},
    {"agent", "security_mode",            ConfigFieldType::String, &CLFAgentConfig::m_securityMode},
    {"agent", "allow_absolute_read",      ConfigFieldType::Bool, nullptr, nullptr, nullptr, &CLFAgentConfig::m_allowAbsoluteRead},
    {"agent", "command_allowlist",        ConfigFieldType::StringListReplace, nullptr, nullptr, nullptr, nullptr, &CLFAgentConfig::m_commandAllowlist},
    // —— logging（日志配置）——
    {"logging", "level",   ConfigFieldType::String, &CLFAgentConfig::m_logLevel},
    {"logging", "file",    ConfigFieldType::String, &CLFAgentConfig::m_logFile},
    {"logging", "console", ConfigFieldType::Bool, nullptr, nullptr, nullptr, &CLFAgentConfig::m_logConsole},
};

// 表驱动应用：单循环替代 30+ if(contains)；类型过滤语义与原内联逐条保真
void applyConfigFields(const json& cfg, CLFAgentConfig& outConfig) {
    for (const auto& f : kConfigFields) {
        auto secIt = cfg.find(f.section);
        if (secIt == cfg.end() || !secIt->is_object()) continue;
        auto valIt = secIt->find(f.key);
        if (valIt == secIt->end()) continue;
        const auto& v = *valIt;
        switch (f.type) {
        case ConfigFieldType::String:
            if (v.is_string()) outConfig.*(f.strField) = v.get<std::string>();
            break;
        case ConfigFieldType::Int:
            if (v.is_number()) outConfig.*(f.intField) = v.get<int>();
            break;
        case ConfigFieldType::Float:
            if (v.is_number()) outConfig.*(f.floatField) = v.get<float>();
            break;
        case ConfigFieldType::Bool:
            if (v.is_boolean()) outConfig.*(f.boolField) = v.get<bool>();
            break;
        case ConfigFieldType::StringListAppend:
            if (v.is_array())
                for (const auto& item : v)
                    if (item.is_string())
                        (outConfig.*(f.listField)).push_back(item.get<std::string>());
            break;
        case ConfigFieldType::StringListReplace: {
            if (!v.is_array()) break;
            auto& list = outConfig.*(f.listField);
            list.clear();
            for (const auto& item : v)
                if (item.is_string()) list.push_back(item.get<std::string>());
            break;
        }
        case ConfigFieldType::IntMap:
            if (v.is_object())
                for (auto it = v.begin(); it != v.end(); ++it)
                    if (it.value().is_number())
                        (outConfig.*(f.mapField))[it.key()] = it.value().get<int>();
            break;
        }
    }
}

} // anonymous namespace

std::string CLFConfigLoader::s_projectRoot;

std::string CLFConfigLoader::findProjectRoot() {
    if (!s_projectRoot.empty()) return s_projectRoot;

    // 1. 获取可执行文件所在目录
    std::string exeDir;
#ifdef _WIN32
    // W 版本 + u8string：A 版本按 ANSI 代码页读取路径，exe 位于中文目录时乱码
    wchar_t wbuf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, wbuf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        exeDir = fs::path(wbuf).parent_path().u8string();
    }
#else
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        exeDir = fs::path(buf).parent_path().string();
    }
#endif

    // 2. 从 exe 目录向上查找 CMakeLists.txt（开发环境：项目根目录）
    // u8path 构造：exeDir 为 UTF-8 窄字符，按 ANSI 代码页构造 path 会乱码
    fs::path dir = exeDir.empty() ? fs::current_path() : fs::u8path(exeDir);
    while (!dir.empty() && dir != dir.root_path()) {
        if (fs::exists(dir / "CMakeLists.txt")) {
            s_projectRoot = dir.u8string();
            return s_projectRoot;
        }
        dir = dir.parent_path();
    }

    // 3. 从 exe 目录向上查找 config/agent_settings.json（独立安装场景）
    dir = exeDir.empty() ? fs::current_path() : fs::u8path(exeDir);
    while (!dir.empty() && dir != dir.root_path()) {
        if (fs::exists(dir / "config" / "agent_settings.json")) {
            s_projectRoot = dir.u8string();
            return s_projectRoot;
        }
        dir = dir.parent_path();
    }

    // 4. 找不到则回退到 CWD
    s_projectRoot = fs::current_path().u8string();
    return s_projectRoot;
}

std::string CLFConfigLoader::resolvePath(const std::string& relativePath) {
    if (s_projectRoot.empty()) findProjectRoot();
    return s_projectRoot + "/" + relativePath;
}

std::string CLFConfigLoader::getWorkingDir() {
    return fs::current_path().u8string();
}

bool CLFConfigLoader::loadFromFile(const std::string& configPath, CLFAgentConfig& outConfig) {
    std::ifstream file(fs::u8path(configPath));
    if (!file.is_open()) {
        return false;
    }

    try {
        json cfg = json::parse(file);
        applyConfigFields(cfg, outConfig);   // C6：表驱动应用（26 字段）
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
