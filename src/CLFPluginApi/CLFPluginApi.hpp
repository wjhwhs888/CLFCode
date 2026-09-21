// CLFPluginApi.hpp — 插件 ABI 核心（clf_plugin_api，唯一跨 DLL 共享头之一）
// 阶段 2 分册 §3.1 细化：C 工厂符号 + 纯虚接口 + POD 服务表；
// 跨 DLL 边界禁传 STL / 异常 / RTTI（§3.1 + 2.1 补充纪律）。
// 本头不依赖 CLFPluginApi/ 之外的任何项目头，宿主与插件统一编译链共享。
//
// 生命周期约定（实现侧义务）：
// - 所有 const char* 返回值由**返回方**持有，调用期间有效，接收方必须立即复制
// - 服务实例（CLFService*）由**提供方插件**持有，生命周期 ≥ 其 DLL 被加载期间
// - 【审查补 2026-09-11】unload 后该插件全部服务实例失效——消费方**不得跨 unload
//   缓存 CLFService***；每次使用前经 getService 重新获取（卸载后查询返回 nullptr）
// - 【同步契约 2026-09-21】本 ABI 全部回调（CLFToolCallbacks / CLFFileCallbacks 等）
//   为**同步阻塞契约**：回调必须在调用返回前完成；ctx 由调用方调用栈持有、
//   仅在调用期间有效。插件实现内部走异步（如阶段 3 集成插件的 IPC）须自行阻塞
//   等待并在返回前完成全部回调——异步延迟调用 = 悬空 ctx；
//   确需异步能力时以新接口表达（新增服务接口 ≠ ABI 版本变更），不破坏本契约
// - 【定案倾向 2026-09-21】宿主侧**长期持有**的服务指针（C1 的构造注入
//   ICLFFileService*）与热卸载的交互：2.1 无宿主长期持有、无热卸载，无冲突；
//   2.2a 落地方向 = **进程内转发代理**（每次调用经 getService 查询），
//   否决"unload 前通知宿主刷新"——论证见 2.1 §1.6
//
// example（插件侧）：
//   class MyPlugin : public CLF::CLFPluginApi::CLFPlugin { ... };
//   extern "C" CLF_PLUGIN_EXPORT CLFPlugin* CLFPluginCreate(const CLFHostApi* host) {
//       return new MyPlugin(host);
//   }

#pragma once
#include <cstddef>
#include <cstdint>

namespace CLF::CLFPluginApi {

// ============================================================================
// ABI 版本
// ============================================================================

// 不兼容变更 = +1；宿主与插件版本不等 → 管理器拒绝加载 + 提示重建
constexpr uint32_t CLF_PLUGIN_API_VERSION = 1;

// ============================================================================
// 日志级别（跨边界 POD 编码）
// ============================================================================

// 与 CLF::CLFCore::CLFLogLevel 序对齐（0=Debug/1=Info/2=Warn/3=Error）；
// 对应关系由 CLFHostApiImpl.cpp 的 static_assert 双向钉死（照 CLFDiffOpCode 先例）
enum CLFLogLevelCode : int {
    LogCodeDebug = 0,
    LogCodeInfo  = 1,
    LogCodeWarn  = 2,
    LogCodeError = 3,
};

// ============================================================================
// 服务基类与服务表
// ============================================================================

// 所有跨边界服务接口的公共基类（ICLFFileService / ICLFToolProvider 等继承之）。
// 存在的唯一目的：让 CLFHostApi::getService 有类型安全的返回类型——
// 单继承链下 static_cast<具体接口*>(CLFService*) 地址调整正确（禁 RTTI）
//
// 【实现约束】服务表项必须指向"该接口自身的 CLFService 基类子对象"：
// 一个插件类若同时实现多个服务接口（多继承 → 多个 CLFService 基类子对象），
// 每个服务表项须用其对应接口视角的 static_cast<具体接口*>(this)（编译期已知
// 偏移）；宿主侧 downcast 仅在指针指向对应子对象时正确。
// 禁止把"某一个"CLFService* 复用于多个服务项。
class CLFService {
public:
    virtual ~CLFService() = default;
};

// 服务表中的一项（"服务名 → 实例"）
struct CLFServiceEntry {
    const char* name;      // 服务名（宿主契约，如 "file" / "tool.provider"；非插件 ID）
    CLFService* instance;  // 由提供方插件持有
};

// ============================================================================
// 插件本体（自描述 + 生命周期）
// ============================================================================

class CLFPlugin {
public:
    virtual ~CLFPlugin() = default;

    // 唯一 ID，如 "tools.fileops"（依赖图与日志用；约定与 DLL 文件名一致）
    virtual const char* name() const = 0;

    // 插件自身版本（如 "1.0.0"，仅展示/日志，不参与兼容判定）
    virtual const char* version() const = 0;

    // 编译期依赖的 ABI 版本（恒为 CLF_PLUGIN_API_VERSION 的值），
    // 与宿主 apiVersion() 精确相等才加载
    virtual uint32_t hostApiVersion() const = 0;

    // 依赖的**服务名**表（nullptr 结尾；无依赖返回空表）。
    // 管理器解析为"谁 provides 该服务"建立加载顺序
    // 【命名 2026-09-21】原名 requires() 与 C++20 关键字冲突——ABI 头须可被
    // C++20 消费者编译（qa 测试即 C++20；未来插件作者同样），构建期实测发现后改名
    virtual const char* const* requiresServices() const = 0;

    // 本插件提供的服务表（nullptr 结尾；无服务返回空表）。
    // 管理器加载后收集入注册表；**禁 RTTI 的关键**——不靠 dynamic_cast 取领域接口
    virtual const CLFServiceEntry* services() const = 0;

    // 自治初始化（失败 → 管理器标记该插件禁用，其余插件不受影响）
    virtual bool init() = 0;

    // 自治清理（卸载前调用；不得抛异常跨越边界）
    virtual void shutdown() = 0;
};

// ============================================================================
// 宿主注入面（管理器 → 插件）
// ============================================================================

class CLFHostApi {
public:
    virtual ~CLFHostApi() = default;

    // 宿主当前 ABI 版本
    virtual uint32_t apiVersion() const = 0;

    // 日志（级别见 CLFLogLevelCode；宿主不再二次判空，插件保证 msg 非空）
    virtual void log(int level, const char* msg) const = 0;

    // 本插件配置段读取：key 不存在返回 nullptr（插件用内置默认）；
    // 返回值宿主持有，调用期间有效
    virtual const char* config(const char* pluginId, const char* key) const = 0;

    // 服务查询（按**服务名**路由，与 requiresServices() 的 SDP 语义统一）：
    // 不存在返回 nullptr → **调用方自兜底**（降级/报错，不崩）
    virtual CLFService* getService(const char* service) const = 0;

    // 跨边界所有权转移通道（/MD 统一前提下当前实现可直转 malloc/free；
    // 保留此面是为 ABI 不依赖"必须 /MD"的隐式约定，未来 /MT 场景无需改 ABI）
    virtual void* alloc(size_t size) const = 0;
    virtual void  free(void* ptr) const = 0;
};

// ============================================================================
// 插件导出符号（每个插件 DLL 只导出这两个 C 符号）
// ============================================================================

// 跨编译器导出宏（MSVC 与 GCC/MinGW 双构建链对称——与禁 RTTI 同一理由）
#if defined(_MSC_VER)
#define CLF_PLUGIN_EXPORT __declspec(dllexport)
#else
#define CLF_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

// typedef 形态便于插件侧书写（跨边界符号不得为 C++ 名字修饰）
typedef CLFPlugin* (*CLFPluginCreateFn)(const CLFHostApi* host);
typedef void       (*CLFPluginDestroyFn)(CLFPlugin* plugin);

} // namespace CLF::CLFPluginApi
