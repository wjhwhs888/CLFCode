// CLFHostApiImpl.hpp — 宿主 API 实现（core 内建，不进 clf_plugin_api；§3.4）
// 管理器把本对象经 CLFPluginCreate(host) 注入插件：日志 / 配置段读取 /
// 服务查询 / 跨边界所有权转移通道。与 CLFPluginManager 双向引用（均非拥有）：
// 构造顺序 manager 先构造 → 自建 host → 传入插件。

#pragma once

#include <map>
#include <string>

#include "CLFPluginApi/CLFPluginApi.hpp"

namespace CLF::CLFCore {

class CLFPluginManager;

class CLFHostApiImpl : public CLF::CLFPluginApi::CLFHostApi {
public:
    explicit CLFHostApiImpl(CLFPluginManager* manager);   // 服务查询回指管理器

    uint32_t apiVersion() const override;                 // 返回 CLF_PLUGIN_API_VERSION
    void     log(int level, const char* msg) const override;   // → CLFLogger 四级别
    const char* config(const char* pluginId, const char* key) const override;
    CLF::CLFPluginApi::CLFService* getService(const char* service) const override;
    void* alloc(size_t size) const override;              // → malloc
    void  free(void* ptr) const override;                 // → free

private:
    CLFPluginManager* m_manager;                  // 非拥有（管理器持有本对象）
    // config 返回值载体：按 key 缓存（同 key 再调用才覆写）——插件 init() 里连续取
    // 多个 key 并暂存指针时各指针保持有效（§3.4）。2.1 阶段 config() 为 nullptr
    // 骨架（2.2a 接真实现），本缓存随骨架声明、暂不使用
    mutable std::map<std::string, std::string> m_configCache;
};

} // namespace CLF::CLFCore
