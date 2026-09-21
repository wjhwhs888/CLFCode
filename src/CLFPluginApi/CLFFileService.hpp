// CLFFileService.hpp — 文件能力域跨边界接口（clf_plugin_api 头，唯一跨 DLL 共享头）
// 阶段 2 分册 §3.1② 草案细化，C1（2026-09-07）落地：POD 签名 + 回调推送，
// 跨 DLL 边界禁传 STL。进程内表示（CLFDiffLine 等）由宿主侧持有。
// 2.1 修正①（2026-09-21）：ICLFFileService 加 CLFService 基类（getService
// downcast 的类型安全返回）。
// 本头不依赖 CLFPluginApi/ 之外的任何项目头（§3.5 纪律），宿主与插件统一编译链共享。
//
// 回调约定（实现侧义务）：
// - ctx 由调用方提供，原样转发给该次调用的全部回调
// - 回调函数指针可能为 null，实现侧调用前判空
// - 所有指针参数（path/content/oldStr/…）与回调内字符串指针仅在调用期间有效，
//   接收方必须立即复制，不得持有
// - 【同步契约 2026-09-21】回调必须在本次调用返回前同步完成：ctx 由宿主侧调用栈
//   持有（如 CLFToolExecutor 的局部 std::string），异步延迟调用 = 悬空指针；
//   插件实现内部走异步（如阶段 3 集成插件的 IPC）须自行阻塞等待并在返回前完成
//   全部回调。确需异步能力时以新接口表达（新增服务接口 ≠ ABI 版本变更），
//   不破坏本契约
//
// example:
//   CLFFileCallbacks cb{};
//   cb.onContent = [](void* ctx, const char* data, size_t len) {
//       static_cast<Collector*>(ctx)->content.append(data, len);
//   };
//   CLFFileInfo info;
//   Collector col;
//   if (!fileService->readFile(path, &info, &col, &cb)) { /* onError 已收错误 */ }

#pragma once

#include <cstddef>
#include <cstdint>
#include "CLFPluginApi/CLFPluginApi.hpp"

namespace CLF::CLFPluginApi {

// 文件元信息（TOCTOU 校验用；失败时全 0）
struct CLFFileInfo {
    uint64_t mtime = 0;
    uint64_t size  = 0;
};

// diff 行操作码编码（与 CLF::CLFTools::CLFDiffOp 枚举序一致：0=Keep 1=Add 2=Remove，
// 枚举序由 CLFFileServiceImpl.cpp 的 static_assert 钉死，勿改）
enum CLFDiffOpCode : int {
    DiffOpKeep   = 0,
    DiffOpAdd    = 1,
    DiffOpRemove = 2,
};

// 宿主侧回调集合（POD 函数指针 struct，由 core 提供，经 call 参数传入）
struct CLFFileCallbacks {
    // 错误文本（调用失败时推，UTF-8，调用期间有效）
    void (*onError)(void* ctx, const char* msg) = nullptr;
    // 文件/预览内容整块（len 可为 0；data 非空，调用期间有效）
    void (*onContent)(void* ctx, const char* data, size_t len) = nullptr;
    // diff 逐行推（op 为 CLFDiffOpCode 编码，调用期间有效）
    void (*onDiffLine)(void* ctx, int op, int oldLineNo, int newLineNo, const char* text) = nullptr;
    // diff 统计（行流之前推；truncated 为 0/1，truncReason 可为空串）
    void (*onStats)(void* ctx, int added, int removed, int hunks,
                    int truncated, const char* truncReason) = nullptr;
};

// 文件能力域服务接口（getService("file") 返回；getService 已定案单参服务名，见 2.1 §3.3）
// 2.1 修正①：继承 CLFService——getService 返回 CLFService* 后
// static_cast<ICLFFileService*> 要求单继承链成立（多继承子对象约束见 CLFPluginApi.hpp）
class ICLFFileService : public CLFService {
public:
    virtual ~ICLFFileService() = default;

    // 读文件：成功返回 true，内容经 onContent 整块推（len 可为 0），元信息写 info；
    // 失败返回 false + onError，info 全 0
    virtual bool readFile(const char* path, CLFFileInfo* info,
                          void* ctx, const CLFFileCallbacks* cb) = 0;

    // 预览替换（纯内存模拟，不落盘）：成功返回 true，新内容经 onContent 推；
    // 失败（0/多次匹配）返回 false + onError
    virtual bool previewEdit(const char* content, size_t contentLen,
                             const char* oldStr, const char* newStr,
                             void* ctx, const CLFFileCallbacks* cb) = 0;

    // 行级 diff：先 onStats 后逐行 onDiffLine（截断时 truncated=1 且行流可能为空）；
    // 恒返回 true（错误语义经 stats.truncated/truncReason 表达）
    virtual bool computeDiff(const char* oldText, const char* newText,
                             int contextLines, void* ctx, const CLFFileCallbacks* cb) = 0;

    // 文件元信息（TOCTOU 校验）：失败全 0
    virtual CLFFileInfo getFileInfo(const char* path) = 0;
};

} // namespace CLF::CLFPluginApi
