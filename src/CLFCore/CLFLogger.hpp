// CLFLogger.hpp — 日志系统（单例）
// 提供带级别过滤、时间戳的日志输出，支持文件 + 控制台
//
// example:
//   CLF::CLFCore::CLFLogger::instance().init(
//       CLF::CLFCore::CLFLogLevel::Info, "clf_agent.log", false);
//   CLF::CLFCore::CLFLogger::instance().info("Agent started");

#pragma once

#include <fstream>
#include <mutex>
#include <string>

namespace CLF::CLFCore {

enum class CLFLogLevel {
    Debug = 0,
    Info  = 1,
    Warn  = 2,
    Error = 3
};

class CLFLogger {
public:
    // 单例访问
    static CLFLogger& instance();

    // 初始化（启动早期调用；可重复调用以重配置）
    void init(CLFLogLevel level, const std::string& filePath, bool consoleOutput);

    // 日志方法
    void debug(const std::string& msg);
    void info(const std::string& msg);
    void warn(const std::string& msg);
    void error(const std::string& msg);

    // 将日志级别字符串解析为枚举（"debug"|"info"|"warn"|"error"）
    // 未知字符串返回 Info
    static CLFLogLevel levelFromString(const std::string& levelStr);

private:
    CLFLogger() = default;
    ~CLFLogger();
    CLFLogger(const CLFLogger&) = delete;
    CLFLogger& operator=(const CLFLogger&) = delete;

    void log(CLFLogLevel msgLevel, const std::string& msg);

    CLFLogLevel m_level    = CLFLogLevel::Info;
    std::string m_filePath;
    bool        m_console  = false;
    bool        m_initialized = false;
    std::mutex  m_mutex;
    std::ofstream m_fileStream; // 缓存文件句柄（懒打开，析构关闭）
};

} // namespace CLF::CLFCore
