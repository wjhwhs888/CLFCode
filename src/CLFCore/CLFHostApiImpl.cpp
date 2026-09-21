// CLFHostApiImpl.cpp — 宿主 API 实现（§3.4）
// log 级别映射：CLFLogLevelCode 与 CLFLogLevel 枚举序由 static_assert 双向钉死
// （照 CLFDiffOpCode 先例）——任一侧改动枚举序即编译失败。
// config：宿主级键（workspace_root/allow_absolute_read——2.2b read_file 校验
// 归属定案通道）+ 插件配置文件 config/plugins/<pluginId>.json（修正② JSON）。

#include "CLFCore/CLFHostApiImpl.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "CLFCore/CLFConfigLoader.hpp"
#include "CLFCore/CLFLogger.hpp"
#include "CLFCore/CLFPluginManager.hpp"

namespace CLF::CLFCore {

// 枚举序双向钉死（0=Debug/1=Info/2=Warn/3=Error）
static_assert(CLF::CLFPluginApi::LogCodeDebug == static_cast<int>(CLFLogLevel::Debug));
static_assert(CLF::CLFPluginApi::LogCodeInfo  == static_cast<int>(CLFLogLevel::Info));
static_assert(CLF::CLFPluginApi::LogCodeWarn  == static_cast<int>(CLFLogLevel::Warn));
static_assert(CLF::CLFPluginApi::LogCodeError == static_cast<int>(CLFLogLevel::Error));

CLFHostApiImpl::CLFHostApiImpl(CLFPluginManager* manager)
    : m_manager(manager) {}

uint32_t CLFHostApiImpl::apiVersion() const {
    return CLF::CLFPluginApi::CLF_PLUGIN_API_VERSION;
}

void CLFHostApiImpl::log(int level, const char* msg) const {
    switch (level) {
    case CLF::CLFPluginApi::LogCodeDebug: CLFLogger::instance().debug(msg); break;
    case CLF::CLFPluginApi::LogCodeInfo:  CLFLogger::instance().info(msg);  break;
    case CLF::CLFPluginApi::LogCodeWarn:  CLFLogger::instance().warn(msg);  break;
    case CLF::CLFPluginApi::LogCodeError: CLFLogger::instance().error(msg); break;
    default:                              CLFLogger::instance().info(msg);  break;
    }
}

// 2.2b 真实现（2.1 为 nullptr 骨架）：
// - 宿主级键（任何 pluginId 适用）："workspace_root" / "allow_absolute_read"——
//   read_file 校验归属定案通道（2.2b §二：保持 handler 层校验，插件经此取根）
// - 插件键：config/plugins/<pluginId>.json（修正② JSON；懒加载 + 复合键缓存）
// - 未命中/文件缺失 → nullptr（插件用内置默认，§3.4 fallback 模式）
const char* CLFHostApiImpl::config(const char* pluginId, const char* key) const {
    const std::string plugin(pluginId ? pluginId : "");
    const std::string keyStr(key ? key : "");

    // —— 宿主级键 ——
    if (keyStr == "workspace_root" || keyStr == "allow_absolute_read") {
        std::string& v = m_configCache[std::string("host:") + keyStr];
        v = keyStr == "workspace_root"
                ? CLFConfigLoader::getWorkingDir()
                : (CLFConfigLoader::allowAbsoluteRead() ? "true" : "false");
        return v.c_str();
    }

    // —— 插件键（复合键缓存：plugin:key——防不同插件同 key 串值）——
    const std::string cacheKey = plugin + ":" + keyStr;
    auto it = m_configCache.find(cacheKey);
    if (it != m_configCache.end()) {
        return it->second.c_str();
    }
    const std::string cfgPath =
        CLFConfigLoader::resolvePath("config/plugins/" + plugin + ".json");
    std::error_code ec;
    if (!std::filesystem::exists(std::filesystem::u8path(cfgPath), ec)) {
        return nullptr;   // 文件缺失 → 插件用内置默认
    }
    std::ifstream f(std::filesystem::u8path(cfgPath));
    if (!f.is_open()) {
        return nullptr;
    }
    try {
        nlohmann::json root = nlohmann::json::parse(f);
        const std::string value = root.value(keyStr, "");
        if (value.empty()) {
            return nullptr;   // key 未命中
        }
        m_configCache[cacheKey] = value;
        return m_configCache[cacheKey].c_str();
    } catch (const std::exception& e) {
        CLFLogger::instance().warn(std::string("[Plugin] config parse failed: ") +
                                   cfgPath + " (" + e.what() + ")");
        return nullptr;
    }
}

CLF::CLFPluginApi::CLFService* CLFHostApiImpl::getService(const char* service) const {
    return m_manager->getService(service);
}

void* CLFHostApiImpl::alloc(size_t size) const {
    return std::malloc(size);
}

void CLFHostApiImpl::free(void* ptr) const {
    std::free(ptr);
}

} // namespace CLF::CLFCore
