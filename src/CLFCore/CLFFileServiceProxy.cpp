// CLFFileServiceProxy.cpp — 文件服务转发代理实现（2.2b，§1.6）
// 每次调用经 getService("file") 查询——不缓存服务指针（§1.2 禁跨 unload 缓存），
// 热卸载后查询 nullptr → 走 onError 兜底（调用方按既有失败路径处理）。

#include "CLFCore/CLFFileServiceProxy.hpp"

#include "CLFCore/CLFPluginManager.hpp"

namespace CLF::CLFCore {

CLF::CLFPluginApi::ICLFFileService* CLFFileServiceProxy::resolve(
    void* ctx, const CLF::CLFPluginApi::CLFFileCallbacks* cb) const {
    auto* svc = m_manager ? m_manager->getService("file") : nullptr;
    if (!svc) {
        if (cb && cb->onError) {
            cb->onError(ctx, "文件服务不可用（插件已停用）");
        }
        return nullptr;
    }
    return static_cast<CLF::CLFPluginApi::ICLFFileService*>(svc);
}

bool CLFFileServiceProxy::readFile(const char* path, CLF::CLFPluginApi::CLFFileInfo* info,
                                   void* ctx, const CLF::CLFPluginApi::CLFFileCallbacks* cb) {
    auto* svc = resolve(ctx, cb);
    if (!svc) {
        if (info) *info = {};   // 失败时元信息全 0（与 C1 契约一致）
        return false;
    }
    return svc->readFile(path, info, ctx, cb);
}

bool CLFFileServiceProxy::previewEdit(const char* content, size_t contentLen,
                                      const char* oldStr, const char* newStr,
                                      void* ctx, const CLF::CLFPluginApi::CLFFileCallbacks* cb) {
    auto* svc = resolve(ctx, cb);
    return svc && svc->previewEdit(content, contentLen, oldStr, newStr, ctx, cb);
}

bool CLFFileServiceProxy::computeDiff(const char* oldText, const char* newText,
                                      int contextLines, void* ctx,
                                      const CLF::CLFPluginApi::CLFFileCallbacks* cb) {
    auto* svc = resolve(ctx, cb);
    return svc && svc->computeDiff(oldText, newText, contextLines, ctx, cb);
}

CLF::CLFPluginApi::CLFFileInfo CLFFileServiceProxy::getFileInfo(const char* path) {
    auto* svc = m_manager ? m_manager->getService("file") : nullptr;
    if (!svc) {
        return {};   // 失败全 0（与 C1 契约一致）
    }
    return static_cast<CLF::CLFPluginApi::ICLFFileService*>(svc)->getFileInfo(path);
}

} // namespace CLF::CLFCore
