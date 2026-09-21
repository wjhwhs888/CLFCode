// CLFHostApiImpl.cpp — 宿主 API 实现（§3.4）
// log 级别映射：CLFLogLevelCode 与 CLFLogLevel 枚举序由 static_assert 双向钉死
// （照 CLFDiffOpCode 先例）——任一侧改动枚举序即编译失败。

#include "CLFCore/CLFHostApiImpl.hpp"

#include <cstdlib>

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

// TODO(2.2a)：读 config/plugins/<pluginId>.json（修正②：JSON，复用 nlohmann::json），
// 懒加载 + 按 key 缓存进 m_configCache；key 未命中或文件缺失 → nullptr（插件用内置
// 默认）。2.1 阶段先落 nullptr 骨架
const char* CLFHostApiImpl::config(const char* pluginId, const char* key) const {
    (void)pluginId;
    (void)key;
    return nullptr;
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
