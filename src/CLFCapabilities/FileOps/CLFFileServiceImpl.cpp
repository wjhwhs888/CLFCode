// CLFFileServiceImpl.cpp — ICLFFileService 进程内默认实现
// POD 接口 → CLFFileOps/CLFDiff 静态函数转调（C1 适配层，无状态）

#include "CLFCapabilities/FileOps/CLFFileServiceImpl.hpp"

#include "CLFCapabilities/FileOps/CLFDiff.hpp"
#include "CLFCapabilities/FileOps/CLFFileOps.hpp"

namespace CLF::CLFCapabilities {

namespace {

using CLF::CLFPluginApi::CLFDiffOpCode;
using CLF::CLFPluginApi::CLFFileCallbacks;
using CLF::CLFPluginApi::CLFFileInfo;

// diff 操作码枚举序双向钉死（接口头编码 ↔ 进程内类型），序漂移 = 编译期失败
static_assert(static_cast<int>(CLF::CLFTools::CLFDiffOp::Keep)   == CLFDiffOpCode::DiffOpKeep);
static_assert(static_cast<int>(CLF::CLFTools::CLFDiffOp::Add)    == CLFDiffOpCode::DiffOpAdd);
static_assert(static_cast<int>(CLF::CLFTools::CLFDiffOp::Remove) == CLFDiffOpCode::DiffOpRemove);

} // anonymous namespace

bool CLFFileServiceImpl::readFile(const char* path, CLFFileInfo* info,
                                  void* ctx, const CLFFileCallbacks* cb) {
    if (info) *info = {};
    CLF::CLFTools::CLFFileSnapshot snap;
    auto result = CLF::CLFTools::readFileWithSnapshot(path ? path : "", snap);
    if (!result.m_success) {
        if (cb && cb->onError) cb->onError(ctx, result.m_error.c_str());
        return false;
    }
    if (info) {
        info->mtime = snap.mtime;
        info->size  = snap.size;
    }
    if (cb && cb->onContent) cb->onContent(ctx, snap.content.data(), snap.content.size());
    return true;
}

bool CLFFileServiceImpl::previewEdit(const char* content, size_t contentLen,
                                     const char* oldStr, const char* newStr,
                                     void* ctx, const CLFFileCallbacks* cb) {
    auto result = CLF::CLFTools::previewEdit(
        std::string(content ? content : "", contentLen),
        oldStr ? oldStr : "", newStr ? newStr : "");
    if (!result.m_success) {
        if (cb && cb->onError) cb->onError(ctx, result.m_error.c_str());
        return false;
    }
    if (cb && cb->onContent) cb->onContent(ctx, result.m_content.data(), result.m_content.size());
    return true;
}

bool CLFFileServiceImpl::computeDiff(const char* oldText, const char* newText,
                                     int contextLines, void* ctx,
                                     const CLFFileCallbacks* cb) {
    CLF::CLFTools::CLFDiffStats stats;
    auto lines = CLF::CLFTools::computeDiff(oldText ? oldText : "",
                                            newText ? newText : "",
                                            stats, contextLines);
    if (cb && cb->onStats) {
        cb->onStats(ctx, stats.added, stats.removed, stats.hunks,
                    stats.truncated ? 1 : 0, stats.truncReason.c_str());
    }
    if (cb && cb->onDiffLine) {
        for (const auto& line : lines) {
            cb->onDiffLine(ctx, static_cast<int>(line.op),
                           line.oldLineNo, line.newLineNo, line.text.c_str());
        }
    }
    return true;
}

CLFFileInfo CLFFileServiceImpl::getFileInfo(const char* path) {
    CLFFileInfo info;
    std::string p = path ? path : "";
    info.mtime = CLF::CLFTools::getFileMtime(p);
    info.size  = CLF::CLFTools::getFileSize(p);
    return info;
}

} // namespace CLF::CLFCapabilities
