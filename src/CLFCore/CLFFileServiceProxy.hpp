// CLFFileServiceProxy.hpp — 文件服务转发代理（2.2b，§1.6 定案倾向 A 落地）
// 每次调用经 manager.getService("file") 查询后转调——**热卸载天然安全**
// （否决"unload 前通知宿主刷新"：与插件自治矛盾、刷新时序易漏）。
// nullptr（插件停用）→ 返回 false + onError "文件服务不可用（插件已停用）"
// ——C1 已保真"读失败静默"行为，调用方走既有失败路径。
// 放 CLFCore（依赖 CLFPluginManager——core；放 capabilities 会造成依赖倒置）。
// example:
//   CLFPluginManager manager;
//   manager.loadAll();
//   CLFFileServiceProxy proxy(&manager);              // 非拥有，manager 生命周期更长
//   CLFAgentLoop agent(config, &proxy);

#pragma once

#include "CLFPluginApi/CLFFileService.hpp"

namespace CLF::CLFCore {

class CLFPluginManager;

class CLFFileServiceProxy : public CLF::CLFPluginApi::ICLFFileService {
public:
    explicit CLFFileServiceProxy(const CLFPluginManager* manager)   // 非拥有
        : m_manager(manager) {}

    bool readFile(const char* path, CLF::CLFPluginApi::CLFFileInfo* info,
                  void* ctx, const CLF::CLFPluginApi::CLFFileCallbacks* cb) override;
    bool previewEdit(const char* content, size_t contentLen,
                     const char* oldStr, const char* newStr,
                     void* ctx, const CLF::CLFPluginApi::CLFFileCallbacks* cb) override;
    bool computeDiff(const char* oldText, const char* newText,
                     int contextLines, void* ctx,
                     const CLF::CLFPluginApi::CLFFileCallbacks* cb) override;
    CLF::CLFPluginApi::CLFFileInfo getFileInfo(const char* path) override;

private:
    // 查询 + 停用兜底；nullptr 时向 cb 推 onError 并返回 nullptr
    CLF::CLFPluginApi::ICLFFileService* resolve(
        void* ctx, const CLF::CLFPluginApi::CLFFileCallbacks* cb) const;

    const CLFPluginManager* m_manager;
};

} // namespace CLF::CLFCore
