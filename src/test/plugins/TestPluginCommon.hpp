// TestPluginCommon.hpp — 测试插件公共实现（2.1 §4.2；各变体 DLL 编译各自一份。
// 插件 DLL 不链宿主库，仅依赖 clf_plugin_api——本头也只依赖 ABI 头）
//
// 插件编码约定（2.1 §1.2 注记）：
// - 插件 DLL 禁文件级非平凡静态对象（静态析构顺序未定义，项目三次踩坑）；
//   可 static 的仅限字符串字面量、constexpr POD
// - 服务表必须为成员（含 this 指针，不能是 static 局部）
// - 跨边界异常零逸出：实现侧 try/catch 转 onError/返回 false

#pragma once

#include <cstring>

#include "CLFPluginApi/CLFPluginApi.hpp"
#include "CLFPluginApi/CLFToolApi.hpp"

namespace clftest {

// 测试专用第二服务接口（P13 多继承子对象约束验证）：
// 插件侧与 qa 侧共享此定义（qa_CLFPluginManager 同 include 本头）
class ITestSecondService : public CLF::CLFPluginApi::CLFService {
public:
    virtual const char* describe() const = 0;
};

// 提供 test_echo 工具的 CLFPlugin + ICLFToolProvider 完整实现；
// 变体经覆写 name/hostApiVersion/init/requires/services 表达差异
class TestToolProvider : public CLF::CLFPluginApi::CLFPlugin,
                         public CLF::CLFPluginApi::ICLFToolProvider {
public:
    explicit TestToolProvider(const CLF::CLFPluginApi::CLFHostApi* host)
        : m_host(host) {
        // 服务表项必须指向"该接口自身的 CLFService 基类子对象"——多继承下用
        // ICLFToolProvider 视角的 static_cast（编译期已知偏移，零 RTTI）
        m_services[0] = CLF::CLFPluginApi::CLFServiceEntry{
            "tool.provider", static_cast<CLF::CLFPluginApi::ICLFToolProvider*>(this)};
        m_services[1] = CLF::CLFPluginApi::CLFServiceEntry{nullptr, nullptr};
    }

    // —— CLFPlugin ——
    const char* name() const override { return "clf.teststub"; }
    const char* version() const override { return "1.0.0"; }
    uint32_t hostApiVersion() const override {
        return CLF::CLFPluginApi::CLF_PLUGIN_API_VERSION;
    }
    const char* const* requiresServices() const override { return kNoRequires; }
    const CLF::CLFPluginApi::CLFServiceEntry* services() const override {
        return m_services;
    }
    bool init() override { return true; }
    void shutdown() override {
        // P17 析构顺序检测锚点：析构语义错误（host 先死）则此处当场崩；
        // host 存活时仅为一次轻量查询 + 一条日志（qa 断言"shutdown 确被调用"用）
        if (m_host) {
            (void)m_host->apiVersion();
            m_host->log(CLF::CLFPluginApi::LogCodeInfo, "clf.teststub shutdown");
        }
    }

    // —— ICLFToolProvider ——
    int toolCount() const override { return 1; }
    const CLF::CLFPluginApi::CLFToolMetaPOD* toolMeta(int index) const override {
        return index == 0 ? &kMeta : nullptr;
    }
    bool callTool(const char* name, const char* argsJson, void* ctx,
                  const CLF::CLFPluginApi::CLFToolCallbacks* cb) override {
        (void)name;
        try {
            // 同步契约（2026-09-21）：回调在返回前完成；cb 与函数指针均判空（§1.3）
            if (cb && cb->onResult) {
                cb->onResult(ctx, argsJson, std::strlen(argsJson));
            }
            return true;
        } catch (...) {   // 异常零逸出
            if (cb && cb->onError) {
                cb->onError(ctx, "internal error");
            }
            return false;
        }
    }

protected:
    const CLF::CLFPluginApi::CLFHostApi* m_host;   // 非拥有；宿主生命周期 > 插件（管理器保证）
    CLF::CLFPluginApi::CLFServiceEntry m_services[2];   // 必须为成员（含 this 指针）

    static constexpr const char* const kNoRequires[] = {nullptr};
    static constexpr CLF::CLFPluginApi::CLFToolMetaPOD kMeta{
        "test_echo", "测试回显工具", R"({"type":"object","properties":{}})",
        /*risk=*/0, /*flags=*/CLF::CLFPluginApi::ToolFlagNone, /*concludesTurn=*/0
    };
};

} // namespace clftest
