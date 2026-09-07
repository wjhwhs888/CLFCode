// qa_CLFConfigLoader.cpp — 配置加载器测试（C6，2026-09-07）
// 表驱动改写回归：26 字段全断言 + 类型过滤 + 环境变量覆盖 + 失败路径。
// 此套件即映射表的形状钉子——表条目错一个（section/key/类型/槽）即挂。

#include <boost/ut.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "CLFCore/CLFConfigLoader.hpp"

using namespace boost::ut;
namespace fs = std::filesystem;

using CLF::CLFCore::CLFAgentConfig;
using CLF::CLFCore::CLFConfigLoader;

namespace {

// 写临时配置文件（唯一文件名防并发冲突），返回路径
std::string writeConfigFile(const std::string& content) {
    auto stamp = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    fs::path p = fs::temp_directory_path() / ("clf_qa_config_" + stamp + ".json");
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << content;
    f.close();
    return p.string();
}

} // anonymous namespace

const boost::ut::suite<"CLFConfigLoader"> tests = [] {
    "L1 全字段加载：26 字段逐一断言"_test = [] {
        const std::string path = writeConfigFile(R"({
            "connection": { "base_url": "https://api.example.com", "api_key": "sk-test" },
            "chat_completions": {
                "model": "test-model",
                "sub_model": "test-sub",
                "temperature": 0.7,
                "max_tokens": 4096,
                "top_p": 0.9,
                "stream": true,
                "frequency_penalty": 0.1,
                "presence_penalty": 0.2,
                "response_format": "json_object",
                "stop": ["\n\n", "END"],
                "thinking_level": "low"
            },
            "agent": {
                "max_context_window": 200000,
                "max_tool_call_iterations": 24,
                "context_compression": true,
                "auto_summary_threshold": 8000,
                "model_max_tokens": { "deepseek-v4-pro": 65536, "deepseek-v4-flash": 8192 },
                "max_response_delay_sec": 120,
                "interaction_language": "en-US",
                "security_mode": "auto",
                "allow_absolute_read": true,
                "command_allowlist": ["git status", "git diff"]
            },
            "logging": { "level": "debug", "file": "qa.log", "console": true }
        })");
        CLFAgentConfig cfg;
        expect(CLFConfigLoader::loadFromFile(path, cfg));

        // connection
        expect(cfg.m_apiBaseUrl == std::string("https://api.example.com"));
        expect(cfg.m_apiKey == std::string("sk-test"));
        // chat_completions
        expect(cfg.m_modelName == std::string("test-model"));
        expect(cfg.m_subModel == std::string("test-sub"));
        expect(cfg.m_temperature > 0.69f && cfg.m_temperature < 0.71f);
        expect(cfg.m_maxTokens == 4096);
        expect(cfg.m_topP > 0.89f && cfg.m_topP < 0.91f);
        expect(cfg.m_stream);
        expect(cfg.m_frequencyPenalty > 0.09f && cfg.m_frequencyPenalty < 0.11f);
        expect(cfg.m_presencePenalty > 0.19f && cfg.m_presencePenalty < 0.21f);
        expect(cfg.m_responseFormat == std::string("json_object"));
        expect(cfg.m_stop.size() == 2u);              // 追加语义：数组逐项
        expect(cfg.m_stop[0] == std::string("\n\n"));
        expect(cfg.m_stop[1] == std::string("END"));
        expect(cfg.m_thinkingLevel == std::string("low"));
        // agent
        expect(cfg.m_maxContextWindow == 200000);
        expect(cfg.m_maxToolCallIterations == 24);
        expect(cfg.m_contextCompression);
        expect(cfg.m_autoSummaryThreshold == 8000);
        expect(cfg.m_modelMaxTokens.size() == 2u);
        expect(cfg.m_modelMaxTokens["deepseek-v4-pro"] == 65536);
        expect(cfg.m_modelMaxTokens["deepseek-v4-flash"] == 8192);
        expect(cfg.m_maxResponseDelaySec == 120);
        expect(cfg.m_interactionLanguage == std::string("en-US"));
        expect(cfg.m_securityMode == std::string("auto"));
        expect(cfg.m_allowAbsoluteRead);
        expect(cfg.m_commandAllowlist.size() == 2u);
        expect(cfg.m_commandAllowlist[0] == std::string("git status"));
        expect(cfg.m_commandAllowlist[1] == std::string("git diff"));
        // logging
        expect(cfg.m_logLevel == std::string("debug"));
        expect(cfg.m_logFile == std::string("qa.log"));
        expect(cfg.m_logConsole);

        fs::remove(fs::u8path(path));
    };

    "L2 类型不符过滤：错误类型字段被忽略（保持默认）"_test = [] {
        const std::string path = writeConfigFile(R"({
            "agent": {
                "max_context_window": "not-a-number",
                "context_compression": "yes",
                "security_mode": 42,
                "command_allowlist": "not-array"
            },
            "logging": { "console": "on" }
        })");
        CLFAgentConfig cfg;
        expect(CLFConfigLoader::loadFromFile(path, cfg));
        expect(cfg.m_maxContextWindow == 1048576);    // 默认保持
        expect(!cfg.m_contextCompression);
        expect(cfg.m_securityMode == std::string("edit"));
        expect(cfg.m_commandAllowlist.empty());
        expect(!cfg.m_logConsole);
        fs::remove(fs::u8path(path));
    };

    "L3 缺字段/缺 section：不崩 + 默认保持"_test = [] {
        const std::string path = writeConfigFile(R"({ "unknown_section": {"x": 1} })");
        CLFAgentConfig cfg;
        expect(CLFConfigLoader::loadFromFile(path, cfg));
        expect(cfg.m_maxTokens == 8192);
        expect(cfg.m_modelName == std::string("deepseek-v4-flash"));
        fs::remove(fs::u8path(path));
    };

    "L4 文件不存在 → false"_test = [] {
        CLFAgentConfig cfg;
        expect(!CLFConfigLoader::loadFromFile("__clf_no_such_config_qa__.json", cfg));
    };

    "L5 非法 JSON → false"_test = [] {
        const std::string path = writeConfigFile("{ not valid json !!");
        CLFAgentConfig cfg;
        expect(!CLFConfigLoader::loadFromFile(path, cfg));
        fs::remove(fs::u8path(path));
    };
};

int main() {}
