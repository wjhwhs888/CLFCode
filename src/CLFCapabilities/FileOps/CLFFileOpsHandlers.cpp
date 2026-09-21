// CLFFileOpsHandlers.cpp — 文件工具 handler 共享实现（2.2a，2026-09-21）
// 自 CLFBuiltinTools.cpp 迁入（逻辑原样保真——2.2b 前主程序行为零变化是硬约束）：
// withHandlerScaffold / isWithinWorkspaceOf（参数化）/ sliceLines（归位
// CLFTextUtil）/ 4 个 handler。read_file 的 workspace 校验参数化（§二 定案）。

#include "CLFCapabilities/FileOps/CLFFileOpsHandlers.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

#include "CLFCapabilities/FileOps/CLFFileOps.hpp"
#include "CLFTypes/CLFTextUtil.hpp"

namespace CLF::CLFCapabilities {

namespace {

// 单文件读取上限，防超大文件撑爆内存（与 search 的 1MB 上限相互独立）
constexpr std::uintmax_t kMaxReadFileSize = 50ull * 1024 * 1024;

} // namespace

// A4a：handler 脚手架（自 CLFBuiltinTools 迁入原样保真）
std::string withHandlerScaffold(
    const std::string& args,
    const std::function<void(const nlohmann::json& params, nlohmann::json& result)>& body) {
    nlohmann::json result;
    try {
        nlohmann::json params = nlohmann::json::parse(args);
        body(params, result);
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"]   = std::string("Handler error: ") + e.what();
    }
    return result.dump();
}

// 参数化工作区边界判定（插件不可链 core——工作区根由调用方传入）。
// 逐段比较防前缀误判、weakly_canonical 防软链接逃逸的语义与原版一致
bool isWithinWorkspaceOf(const std::string& workspaceRootUtf8,
                         const std::string& path, std::string& outError) {
    namespace fs = std::filesystem;
    if (workspaceRootUtf8.empty()) return true;   // 空根 = 跳过校验（§二 定案）
    std::error_code ec;

    fs::path root = fs::weakly_canonical(fs::u8path(workspaceRootUtf8), ec);
    if (ec) { outError = "无法解析工作区根目录"; return false; }

    fs::path target = fs::u8path(path);
    if (!target.is_absolute()) target = root / target;
    target = fs::weakly_canonical(target, ec);
    if (ec) { outError = "无法解析路径: " + path; return false; }

    auto rootIt = root.begin();
    auto tgtIt  = target.begin();
    for (; rootIt != root.end(); ++rootIt, ++tgtIt) {
        if (tgtIt == target.end() || *tgtIt != *rootIt) {
            outError = "路径超出工作区边界: " + path;
            return false;
        }
    }
    return true;
}

// S2-1: 边界/大小/行范围三项均在 handler 层实施——CLFFileOps::readFile 还被
// FileOps 内部路径（editFile/readFileWithSnapshot）调用，在底层加限制会误伤。
// 取证（阶段 2 分册 §3.7）：FileOps 唯一跨层调用方 = CLFToolExecutor（C1 已接口化）。
std::string readFileToolHandler(const std::string& args, bool allowAbsolute,
                                const std::string& workspaceRootUtf8) {
    return withHandlerScaffold(args, [allowAbsolute, &workspaceRootUtf8](
                                         const nlohmann::json& params, nlohmann::json& result) {
        namespace fs = std::filesystem;
        std::string path = params.value("path", "");
        const int offset = params.value("offset", 0);
        const int limit  = params.value("limit", 0);

        // ① 工作区边界（allow_absolute_read 为逃生口；空根 = 跳过——2.2a 插件侧形态）
        if (!allowAbsolute) {
            std::string boundErr;
            if (!isWithinWorkspaceOf(workspaceRootUtf8, path, boundErr)) {
                result["success"] = false;
                result["error"]   = boundErr
                    + "（如确需读取工作区外文件，请在配置中开启 agent.allow_absolute_read）";
                return;
            }
        }

        // ② 大小上限（目录时 file_size 置 ec，不会误判）
        std::error_code ec;
        const auto size = fs::file_size(fs::u8path(path), ec);
        if (!ec && size > kMaxReadFileSize) {
            result["success"] = false;
            result["error"]   = "文件过大（" + std::to_string(size / (1024 * 1024))
                              + "MB，上限 50MB）: " + path;
            return;
        }

        auto fileResult = CLF::CLFTools::readFile(path);
        result["success"] = fileResult.m_success;
        if (!fileResult.m_success) {
            result["error"] = fileResult.m_error;
            return;
        }

        // ③ 行范围切片（CLFTextUtil 归位版，语义与原 detail::sliceLines 一致）
        result["content"] = CLF::CLFCore::CLFTextUtil::sliceLines(fileResult.m_content, offset, limit);
    });
}

std::string writeFileToolHandler(const std::string& args) {
    return withHandlerScaffold(args, [](const nlohmann::json& params, nlohmann::json& result) {
        std::string path    = params.value("path", "");
        std::string content = params.value("content", "");
        auto fileResult = CLF::CLFTools::writeFile(path, content);
        result["success"] = fileResult.m_success;
        if (fileResult.m_success) {
            result["path"]    = path;
            result["written"] = content.size();
        } else {
            result["error"] = fileResult.m_error;
        }
    });
}

std::string editFileToolHandler(const std::string& args) {
    return withHandlerScaffold(args, [](const nlohmann::json& params, nlohmann::json& result) {
        std::string path    = params.value("path", "");
        std::string oldStr  = params.value("old_string", "");
        std::string newStr  = params.value("new_string", "");
        auto fileResult = CLF::CLFTools::editFile(path, oldStr, newStr);
        result["success"] = fileResult.m_success;
        if (fileResult.m_success) {
            result["path"]    = path;
            result["written"] = fileResult.m_content.size();
        } else {
            result["error"] = fileResult.m_error;
        }
    });
}

std::string listDirectoryToolHandler(const std::string& args) {
    return withHandlerScaffold(args, [](const nlohmann::json& params, nlohmann::json& result) {
        std::string path = params.value("path", ".");
        auto fileResult = CLF::CLFTools::listDirectory(path);
        result["success"] = fileResult.m_success;
        if (fileResult.m_success) {
            result["content"] = fileResult.m_content;
        } else {
            result["error"] = fileResult.m_error;
        }
    });
}

} // namespace CLF::CLFCapabilities
