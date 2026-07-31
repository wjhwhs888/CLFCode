// CLFConfigLoader.hpp — Agent 配置加载器
// 从 config/agent_settings.json 读取配置，支持 JSON 文件 + 环境变量覆盖
//
// example:
//   CLF::CLFCore::CLFAgentConfig config;
//   if (!CLF::CLFCore::CLFConfigLoader::loadFromFile("config/agent_settings.json", config)) {
//       std::cerr << "Failed to load config, using defaults" << std::endl;
//   }

#pragma once

#include <string>
#include "CLFCore/CLFAgentLoop.hpp"

namespace CLF::CLFCore {

class CLFConfigLoader {
public:
    // 从 JSON 配置文件加载 CLFAgentConfig
    // 返回 true 表示加载成功，false 表示文件不存在或格式错误（config 保持默认值）
    //
    // agent_settings.json 格式:
    // {
    //     "api": {
    //         "base_url": "https://api.deepseek.com",
    //         "api_key": "sk-xxx",
    //         "model": "deepseek-chat",
    //         "max_tokens": 8192,
    //         "temperature": 0.0
    //     },
    //     "agent": {
    //         "max_context_window": 65536,
    //         "max_tool_call_iterations": 16,
    //         "enable_thinking": true
    //     }
    // }
    static bool loadFromFile(const std::string& configPath, CLFAgentConfig& outConfig);

    // 从文件加载后，用环境变量覆盖（优先级：env > file > default）
    // 支持的环境变量:
    //   CLF_API_KEY       — 覆盖 api_key
    //   CLF_API_BASE_URL  — 覆盖 base_url
    //   CLF_MODEL         — 覆盖 model
    static bool loadFromFileWithEnv(const std::string& configPath, CLFAgentConfig& outConfig);

    // 解析配置文件路径，依次尝试：
    //   config/xxx.json（CWD=项目根）
    //   ../../config/xxx.json（CWD=bin/Debug）
    //   ../config/xxx.json（CWD=bin）
    // 返回第一个存在的路径，都不存在返回原始路径
    static std::string resolveConfigPath(const std::string& relativePath);
};

} // namespace CLF::CLFCore
