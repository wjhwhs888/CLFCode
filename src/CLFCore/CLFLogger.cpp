// CLFLogger.cpp — 日志系统实现

#include "CLFCore/CLFLogger.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace CLF::CLFCore {

namespace {

const char* levelName(CLFLogLevel level) {
    switch (level) {
        case CLFLogLevel::Debug: return "DEBUG";
        case CLFLogLevel::Info:  return "INFO";
        case CLFLogLevel::Warn:  return "WARN";
        case CLFLogLevel::Error: return "ERROR";
    }
    return "INFO";
}

// 获取当前时间字符串 "YYYY-MM-DD HH:MM:SS"
std::string currentTimeStr() {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string(buf);
}

} // anonymous namespace

CLFLogger& CLFLogger::instance() {
    static CLFLogger logger;
    return logger;
}

void CLFLogger::init(CLFLogLevel level, const std::string& filePath, bool consoleOutput) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_level    = level;
    m_filePath = filePath;
    m_console  = consoleOutput;
    m_initialized = true;
}

CLFLogger::~CLFLogger() = default;

CLFLogLevel CLFLogger::levelFromString(const std::string& levelStr) {
    if (levelStr == "debug") return CLFLogLevel::Debug;
    if (levelStr == "warn")  return CLFLogLevel::Warn;
    if (levelStr == "error") return CLFLogLevel::Error;
    return CLFLogLevel::Info;
}

void CLFLogger::debug(const std::string& msg) { log(CLFLogLevel::Debug, msg); }
void CLFLogger::info(const std::string& msg)  { log(CLFLogLevel::Info,  msg); }
void CLFLogger::warn(const std::string& msg)  { log(CLFLogLevel::Warn,  msg); }
void CLFLogger::error(const std::string& msg) { log(CLFLogLevel::Error, msg); }

void CLFLogger::log(CLFLogLevel msgLevel, const std::string& msg) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_initialized) {
        m_level    = CLFLogLevel::Info;
        m_filePath = "clf_agent.log";
        m_console  = false;
        m_initialized = true;
    }

    // 级别过滤
    if (msgLevel < m_level) return;

    std::string line = "[" + currentTimeStr() + "] ["
                     + levelName(msgLevel) + "] " + msg + "\n";

    // 控制台输出
    if (m_console) {
        if (msgLevel >= CLFLogLevel::Warn) {
            std::cerr << line;
        } else {
            std::cout << line;
        }
    }

    // 文件输出（失败静默降级）
    if (!m_filePath.empty()) {
        // 确保父目录存在（如 log/）
        std::filesystem::path logPath(m_filePath);
        if (logPath.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(logPath.parent_path(), ec);
        }

        std::ofstream file(m_filePath, std::ios::app);
        if (file.is_open()) {
            file << line;
        }
    }
}

} // namespace CLF::CLFCore
