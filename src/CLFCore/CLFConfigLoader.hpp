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
#include "CLFTypes/CLFTypes.hpp"

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

    // 确定项目根目录：从可执行文件向上查找 CMakeLists.txt
    // 结果缓存到 s_projectRoot，后续 resolvePath 都基于它
    static std::string findProjectRoot();

    // 基于项目根目录解析相对路径
    static std::string resolvePath(const std::string& relativePath);

    // 获取当前工作目录（用户启动 CLFCode 的目录，即 user's project root）
    static std::string getWorkingDir();

    // 读取应用版本（VERSION 文件首行）——三态契约（启动横幅批次，2026-09-22）：
    //   文件不存在 → "unknown"；存在但读取失败（打不开/空文件）→ ""；成功 → 首行。
    // 不吞异常（resolvePath 抛异常时保持 set_terminate 语义——与既有 --version
    // 行为一致）。两调用点（main.cpp printVersion / CLFCommands cmdVersion）
    // 输出形制各自保留，逐字一致。
    static std::string readVersionFile();

    // 上次 loadFromFileWithEnv 解析的 agent.allow_absolute_read（2.2b 静态缓存——
    // CLFHostApiImpl 宿主级键取值通道；loadFromFileWithEnv 末尾赋值，默认 false）
    static bool allowAbsoluteRead();

    // 命令执行超时配置（2026-09-23 命令执行层 §10.2）：同 allowAbsoluteRead
    // 静态缓存模式——CLFHostApiImpl 宿主级键取值通道；默认 120 / 600
    static int commandDefaultTimeoutSec();
    static int commandMaxTimeoutSec();

private:
    static std::string s_projectRoot;
    static bool        s_allowAbsoluteRead;
    static int         s_commandDefaultTimeoutSec;
    static int         s_commandMaxTimeoutSec;
};

} // namespace CLF::CLFCore
