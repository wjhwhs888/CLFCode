// CLFFileServiceImpl.hpp — ICLFFileService 进程内默认实现（适配层）
// POD 接口 → CLFFileOps/CLFDiff 静态函数转调，无状态、无成员。
// 阶段 2 试点（FileOps 域迁 DLL）时由 DLL 工厂导出替代，
// 宿主装配点（AgentLoop 默认实现 / main 注入）不变。
//
// example:
//   std::unique_ptr<CLF::CLFPluginApi::ICLFFileService> svc =
//       std::make_unique<CLF::CLFCapabilities::CLFFileServiceImpl>();

#pragma once

#include "CLFPluginApi/CLFFileService.hpp"

namespace CLF::CLFCapabilities {

class CLFFileServiceImpl : public CLF::CLFPluginApi::ICLFFileService {
public:
    bool readFile(const char* path, CLF::CLFPluginApi::CLFFileInfo* info,
                  void* ctx, const CLF::CLFPluginApi::CLFFileCallbacks* cb) override;

    bool previewEdit(const char* content, size_t contentLen,
                     const char* oldStr, const char* newStr,
                     void* ctx, const CLF::CLFPluginApi::CLFFileCallbacks* cb) override;

    bool computeDiff(const char* oldText, const char* newText,
                     int contextLines, void* ctx,
                     const CLF::CLFPluginApi::CLFFileCallbacks* cb) override;

    CLF::CLFPluginApi::CLFFileInfo getFileInfo(const char* path) override;
};

} // namespace CLF::CLFCapabilities
