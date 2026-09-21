// CLFFileOpsPlugin.cpp — tools.fileops.dll 工厂与插件壳（2.2a，2026-09-21）
// 多继承三接口：CLFPlugin（自描述/生命周期）+ CLFFileServiceImpl（file 服务，
// 复用进程内实现零重写——无状态无成员，§一取证）+ ICLFToolProvider（4 工具
// 元数据 + callTool 路由到共享 handler）。
// 多继承子对象约束（2.1 §1.2）：每个服务表项须指向其对应接口视角的子对象
// static_cast（编译期已知偏移，零 RTTI）。
// 插件编码约定（2.1 §1.2 注记）：禁文件级非平凡静态对象；服务表为成员；
// 异常零逸出。read_file 的 workspaceRoot 传空串 = 跳过边界校验
// （2.2a 阶段形态；2.2b 切换前校验归属定案，见 2.2a 设计 §二）。

#include <cstring>
#include <string>

#include "CLFCapabilities/FileOps/CLFFileOpsHandlers.hpp"
#include "CLFCapabilities/FileOps/CLFFileServiceImpl.hpp"
#include "CLFPluginApi/CLFPluginApi.hpp"
#include "CLFPluginApi/CLFToolApi.hpp"

namespace {

using namespace CLF::CLFPluginApi;

class CLFFileOpsPlugin : public CLFPlugin,
                         public CLF::CLFCapabilities::CLFFileServiceImpl,
                         public ICLFToolProvider {
public:
    explicit CLFFileOpsPlugin(const CLFHostApi* host)
        : m_host(host) {
        // 服务表：两项各自视角的 static_cast——禁止复用同一指针（§1.2 约束）
        m_services[0] = CLFServiceEntry{
            "file", static_cast<CLF::CLFPluginApi::ICLFFileService*>(this)};
        m_services[1] = CLFServiceEntry{
            "tool.provider", static_cast<ICLFToolProvider*>(this)};
        m_services[2] = CLFServiceEntry{nullptr, nullptr};
    }

    // —— CLFPlugin ——
    const char* name() const override { return "tools.fileops"; }
    const char* version() const override { return "1.0.0"; }
    uint32_t hostApiVersion() const override { return CLF_PLUGIN_API_VERSION; }
    const char* const* requiresServices() const override { return kNoRequires; }
    const CLFServiceEntry* services() const override { return m_services; }
    bool init() override {
        // 2.2b：经 host 取宿主级配置——read_file 校验归属定案（§二）：保持 handler
        // 层校验语义（S2-1），工作区根/逃生口经 config 通道（零 ABI 变更）。
        // 缺失 → 空串/false 兜底（空根 = 跳过校验）
        const char* root = m_host ? m_host->config(name(), "workspace_root") : nullptr;
        m_workspaceRoot  = root ? root : "";
        const char* allowAbs = m_host ? m_host->config(name(), "allow_absolute_read") : nullptr;
        m_allowAbsolute = allowAbs && std::string(allowAbs) == "true";
        return true;
    }
    void shutdown() override {
        if (m_host) {
            (void)m_host->apiVersion();
        }
    }

    // —— ICLFToolProvider ——
    int toolCount() const override { return 4; }
    const CLFToolMetaPOD* toolMeta(int index) const override {
        switch (index) {
        case 0: return &kReadFileMeta;
        case 1: return &kWriteFileMeta;
        case 2: return &kEditFileMeta;
        case 3: return &kListDirMeta;
        default: return nullptr;
        }
    }
    bool callTool(const char* name, const char* argsJson, void* ctx,
                  const CLFToolCallbacks* cb) override {
        try {
            // read_file：workspaceRoot 传空串 = 跳过边界校验（2.2a 阶段形态，
            // 2.2b 切换前定案——见 2.2a 设计 §二 硬约束）
            std::string out = dispatch(name, argsJson);
            if (cb && cb->onResult) {
                cb->onResult(ctx, out.c_str(), out.size());
            }
            return true;
        } catch (...) {   // 异常零逸出（§3.1）
            if (cb && cb->onError) {
                cb->onError(ctx, "internal error");
            }
            return false;
        }
    }

private:
    // callTool 路由 → 共享 handler（与 CLFBuiltinTools 静态注册同一实现）
    std::string dispatch(const char* name, const char* argsJson) {
        const std::string args(argsJson);
        if (std::strcmp(name, "read_file") == 0) {
            // 2.2b：校验语义与静态路径一致（init 时经 host->config 取的根与逃生口；
            // 空根 = 跳过校验——宿主级键不可用时的兜底形态）
            return CLF::CLFCapabilities::readFileToolHandler(
                args, m_allowAbsolute, m_workspaceRoot);
        }
        if (std::strcmp(name, "write_file") == 0) {
            return CLF::CLFCapabilities::writeFileToolHandler(args);
        }
        if (std::strcmp(name, "edit_file") == 0) {
            return CLF::CLFCapabilities::editFileToolHandler(args);
        }
        if (std::strcmp(name, "list_directory") == 0) {
            return CLF::CLFCapabilities::listDirectoryToolHandler(args);
        }
        return std::string("{\"success\":false,\"error\":\"unknown tool: ") + name + "\"}";
    }

    const CLFHostApi* m_host;   // 非拥有；宿主生命周期 > 插件（管理器保证）
    CLFServiceEntry m_services[3];   // 必须为成员（含 this 指针，不能是 static 局部）
    // 2.2b：init 时经 host->config 取的宿主级配置（read_file 校验用）
    std::string m_workspaceRoot;      // 空 = 跳过边界校验
    bool m_allowAbsolute = false;

    static constexpr const char* const kNoRequires[] = {nullptr};
    // 元数据与 CLFBuiltinTools 注册同文案同值（2.2b 切换后模型看到的工具定义零变化）
    static constexpr CLFToolMetaPOD kReadFileMeta{
        "read_file", "读取文件内容（限工作区内，单文件上限 50MB，支持按行范围读取）",
        R"({
        "type": "object",
        "properties": {
            "path": {"type": "string", "description": "文件路径，相对工作区或绝对路径（须位于工作区内）"},
            "offset": {"type": "integer", "description": "起始行号（0 基），省略则从头读"},
            "limit": {"type": "integer", "description": "最多读取行数，省略或 <=0 表示读到末尾"}
        },
        "required": ["path"]
    })",
        /*risk=*/0, /*flags=*/ToolFlagRead, /*concludesTurn=*/0};
    static constexpr CLFToolMetaPOD kWriteFileMeta{
        "write_file", "将内容写入指定路径的文件（覆盖模式，原子写入）",
        R"({
        "type": "object",
        "properties": {
            "path": {"type": "string", "description": "文件路径"},
            "content": {"type": "string", "description": "要写入的内容"}
        },
        "required": ["path", "content"]
    })",
        /*risk=*/1, /*flags=*/ToolFlagNone, /*concludesTurn=*/0};
    static constexpr CLFToolMetaPOD kEditFileMeta{
        "edit_file", "精确替换文件中的字符串（old_string 必须唯一匹配）",
        R"({
        "type": "object",
        "properties": {
            "path": {"type": "string", "description": "文件路径"},
            "old_string": {"type": "string", "description": "要替换的原字符串（必须唯一匹配）"},
            "new_string": {"type": "string", "description": "替换后的新字符串"}
        },
        "required": ["path", "old_string", "new_string"]
    })",
        /*risk=*/1, /*flags=*/ToolFlagNone, /*concludesTurn=*/0};
    static constexpr CLFToolMetaPOD kListDirMeta{
        "list_directory", "列出指定目录下的文件和子目录",
        R"({
        "type": "object",
        "properties": {
            "path": {"type": "string", "description": "目录路径，默认当前目录"}
        },
        "required": []
    })",
        /*risk=*/0, /*flags=*/ToolFlagRead, /*concludesTurn=*/0};
};

} // namespace

extern "C" CLF_PLUGIN_EXPORT
CLF::CLFPluginApi::CLFPlugin* CLFPluginCreate(const CLF::CLFPluginApi::CLFHostApi* host) {
    return new CLFFileOpsPlugin(host);
}
extern "C" CLF_PLUGIN_EXPORT
void CLFPluginDestroy(CLF::CLFPluginApi::CLFPlugin* plugin) {
    delete plugin;
}
