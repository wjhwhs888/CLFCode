// qa_CLFCapabilities.cpp — 能力层接口契约测试（C1，2026-09-07）
// S1-S8: ICLFFileService 接口契约（CLFFileServiceImpl 进程内实现）
// E1-E5: ToolExecutor 经接口全链路行为保真（write/edit 预览 + TOCTOU + 读失败静默）
//        背景：qa_CLFToolExecutor 原无 Write 工具用例，C1 接口化改写
//        prepareWritePreview 路径后补此覆盖

#include <boost/ut.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include "CLFCapabilities/FileOps/CLFDiff.hpp"
#include "CLFCapabilities/FileOps/CLFFileServiceImpl.hpp"
#include "CLFCore/CLFToolExecutor.hpp"
#include "CLFCore/CLFSecurityPolicy.hpp"
#include "CLFTypes/ICLFOutput.hpp"
#include <nlohmann/json.hpp>

using namespace boost::ut;
namespace fs = std::filesystem;

using CLF::CLFCore::CLFToolExecutor;
using CLF::CLFCore::CLFSecurityMode;
using CLF::CLFCore::CLFSecurityPolicy;
using CLF::CLFCore::CLFTool;
using CLF::CLFCore::CLFToolCall;
using CLF::CLFCore::CLFToolRisk;
using CLF::CLFCore::ToolStats;

namespace {

// 在临时目录创建带内容的文件，返回路径；调用方负责删除
std::string makeTempFile(const std::string& name, const std::string& content) {
    fs::path p = fs::temp_directory_path() / name;
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << content;
    f.close();
    return p.string();
}

// ========== S 系列：接口契约接收器与回调 ==========

using CLF::CLFPluginApi::CLFDiffOpCode;
using CLF::CLFPluginApi::CLFFileCallbacks;
using CLF::CLFPluginApi::CLFFileInfo;
using CLF::CLFPluginApi::ICLFFileService;

struct ContentCollector {
    std::string content;
};
struct ErrorCollector {
    std::string error;
};
struct DiffCollector {
    std::vector<CLF::CLFTools::CLFDiffLine> lines;
    CLF::CLFTools::CLFDiffStats stats;
};

void onContent(void* ctx, const char* data, size_t len) {
    static_cast<ContentCollector*>(ctx)->content.append(data, len);
}
void onError(void* ctx, const char* msg) {
    static_cast<ErrorCollector*>(ctx)->error = msg;
}
void onDiffLine(void* ctx, int op, int oldLineNo, int newLineNo, const char* text) {
    auto* c = static_cast<DiffCollector*>(ctx);
    c->lines.push_back({static_cast<CLF::CLFTools::CLFDiffOp>(op), oldLineNo, newLineNo, text});
}
void onStats(void* ctx, int added, int removed, int hunks, int truncated, const char* reason) {
    auto* s = &static_cast<DiffCollector*>(ctx)->stats;
    s->added = added;
    s->removed = removed;
    s->hunks = hunks;
    s->truncated = truncated != 0;
    s->truncReason = reason ? reason : "";
}

// ========== E 系列：MockOutput 与 executor 构造 ==========

class MockOutput : public CLF::CLFTypes::ICLFOutput {
public:
    std::vector<std::string> contents;
    std::vector<std::string> styledLines;

    void emitContent(const std::string& t) override { contents.push_back(t); }
    void emitRaw(const std::string&) override {}
    void emitStyledLine(const std::string& t, LineStyle) override { styledLines.push_back(t); }
    void setStatus(const std::string&, int, int) override {}
    void setStatusTextOnly(const std::string&) override {}
    bool confirm(const std::string&) override { return false; }
    void onInterrupt(std::function<void()>) override {}
    void showProgress(const std::vector<std::string>&) override {}
    void finishProgress(const std::string&) override {}
    void emitError(const std::string&) override {}
    void appendThinking(const std::string&) override {}
    void clearThinking() override {}
    void requestRefresh() override {}

    bool anyStyledContains(const std::string& needle) const {
        for (const auto& s : styledLines)
            if (s.find(needle) != std::string::npos) return true;
        return false;
    }
    bool anyContentContains(const std::string& needle) const {
        for (const auto& s : contents)
            if (s.find(needle) != std::string::npos) return true;
        return false;
    }
};

// Edit 模式（L3：Write 工具走确认流）+ 非渐进（thinkingSec=nullptr）
CLFToolExecutor makeWriteExecutor(std::vector<CLF::CLFCore::CLFTool>& tools,
                                  MockOutput& out,
                                  CLF::CLFCore::ToolStats& stats,
                                  std::function<bool(const std::string&)> confirm) {
    static CLF::CLFCore::CLFTimerLabels labels;
    static CLFSecurityPolicy policy(CLFSecurityMode::Edit);  // L3：写/命令需确认
    static std::atomic<bool> interruptFlag{false};
    static CLF::CLFCapabilities::CLFFileServiceImpl fileService;  // 无状态转调层
    return CLFToolExecutor(tools, policy, std::move(confirm), stats, &fileService,
                           &out, &interruptFlag, &labels, nullptr);
}

CLF::CLFCore::CLFTool makeWriteTool(const std::string& name, int& handlerCalls) {
    CLF::CLFCore::CLFTool t;
    t.m_name = name;
    t.m_risk = CLF::CLFCore::CLFToolRisk::Write;
    t.m_handler = [&handlerCalls](const std::string&) {
        ++handlerCalls;
        return std::string(R"({"success":true})");
    };
    return t;
}

} // anonymous namespace

const boost::ut::suite<"CLFCapabilities"> tests = [] {
    // ========== S1/S2: readFile 契约 ==========

    "S1 readFile 成功：内容整块推 + 元信息"_test = [] {
        auto path = makeTempFile("clf_qa_cap_s1.txt", "hello capability");
        CLF::CLFCapabilities::CLFFileServiceImpl svc;
        ContentCollector col;
        CLFFileCallbacks cb;
        cb.onContent = onContent;
        CLFFileInfo info;
        expect(svc.readFile(path.c_str(), &info, &col, &cb));
        expect(col.content == std::string("hello capability"));
        expect(info.size == 16u);
        expect(info.mtime > 0u);
        fs::remove(path);
    };

    "S2 readFile 失败：false + onError + info 置零"_test = [] {
        CLF::CLFCapabilities::CLFFileServiceImpl svc;
        ErrorCollector err;
        CLFFileCallbacks cb;
        cb.onError = onError;
        CLFFileInfo info{42, 42};
        expect(!svc.readFile("__clf_no_such_cap_s2__.txt", &info, &err, &cb));
        expect(info.mtime == 0u);
        expect(info.size == 0u);
        expect(err.error.find("Cannot open") != std::string::npos);
    };

    // ========== S3/S4: previewEdit 契约 ==========

    "S3 previewEdit 成功：新内容整块推"_test = [] {
        CLF::CLFCapabilities::CLFFileServiceImpl svc;
        ContentCollector col;
        CLFFileCallbacks cb;
        cb.onContent = onContent;
        expect(svc.previewEdit("aXb", 3, "X", "Y", &col, &cb));
        expect(col.content == std::string("aYb"));
    };

    "S4 previewEdit 失败：false + onError"_test = [] {
        CLF::CLFCapabilities::CLFFileServiceImpl svc;
        ErrorCollector err;
        CLFFileCallbacks cb;
        cb.onError = onError;
        expect(!svc.previewEdit("abc", 3, "zzz", "y", &err, &cb));
        expect(err.error.find("not found in file") != std::string::npos);
    };

    // ========== S5/S6: computeDiff 契约 ==========

    "S5 computeDiff：统计 + 行流（op 编码正确）"_test = [] {
        CLF::CLFCapabilities::CLFFileServiceImpl svc;
        DiffCollector col;
        CLFFileCallbacks cb;
        cb.onDiffLine = onDiffLine;
        cb.onStats = onStats;
        expect(svc.computeDiff("a\nb\n", "a\nc\n", 5, &col, &cb));
        expect(col.stats.added == 1);
        expect(col.stats.removed == 1);
        expect(!col.stats.truncated);
        bool hasRemoveB = false, hasAddC = false;
        for (const auto& l : col.lines) {
            if (l.op == CLF::CLFTools::CLFDiffOp::Remove && l.text == "b") hasRemoveB = true;
            if (l.op == CLF::CLFTools::CLFDiffOp::Add && l.text == "c") hasAddC = true;
        }
        expect(hasRemoveB);
        expect(hasAddC);
    };

    "S6 computeDiff 超限截断：truncated + 原因"_test = [] {
        CLF::CLFCapabilities::CLFFileServiceImpl svc;
        std::string big(600 * 1024, 'x');
        DiffCollector col;
        CLFFileCallbacks cb;
        cb.onDiffLine = onDiffLine;
        cb.onStats = onStats;
        expect(svc.computeDiff(big.c_str(), big.c_str(), 5, &col, &cb));
        expect(col.stats.truncated);
        expect(col.stats.truncReason.find("diff skipped") != std::string::npos);
        expect(col.lines.empty());
    };

    // ========== S7/S8: getFileInfo 与空回调防御 ==========

    "S7 getFileInfo：存在/不存在"_test = [] {
        auto path = makeTempFile("clf_qa_cap_s7.txt", "xyz");
        CLF::CLFCapabilities::CLFFileServiceImpl svc;
        auto info = svc.getFileInfo(path.c_str());
        expect(info.size == 3u);
        expect(info.mtime > 0u);
        auto bad = svc.getFileInfo("__clf_no_such_cap_s7__.txt");
        expect(bad.mtime == 0u);
        expect(bad.size == 0u);
        fs::remove(path);
    };

    "S8 空回调防御：cb=nullptr / 回调全 null / info=nullptr 不崩"_test = [] {
        auto path = makeTempFile("clf_qa_cap_s8.txt", "def");
        CLF::CLFCapabilities::CLFFileServiceImpl svc;
        CLFFileCallbacks emptyCb;  // 全 null 函数指针
        CLFFileInfo info;
        ContentCollector col;

        expect(svc.readFile(path.c_str(), &info, &col, nullptr));       // cb=nullptr
        expect(svc.readFile(path.c_str(), nullptr, &col, &emptyCb));    // info=nullptr
        expect(!svc.readFile("__clf_no_such_cap_s8__.txt", &info, &col, nullptr));
        expect(svc.previewEdit("a", 1, "a", "b", &col, nullptr));
        expect(svc.computeDiff("a", "b", 5, nullptr, nullptr));         // ctx/cb 全 null
        fs::remove(path);
    };

    // ========== E1: write_file 预览全链路（经接口） ==========

    "E1 write_file：预览 diff 渲染 + 确认 + handler 执行"_test = [] {
        auto path = makeTempFile("clf_qa_cap_e1.txt", "line1\nline2\n");
        std::vector<CLF::CLFCore::CLFTool> tools;
        int handlerCalls = 0;
        tools.push_back(makeWriteTool("write_file", handlerCalls));

        MockOutput out;
        CLF::CLFCore::ToolStats stats;
        std::string confirmPrompt;
        auto executor = makeWriteExecutor(tools, out, stats,
            [&](const std::string& p) { confirmPrompt = p; return true; });

        CLF::CLFCore::CLFToolCall call;
        call.m_name = "write_file";
        // 经 json 对象构造：Windows 路径反斜杠须转义（手拼会成非法 JSON 转义）
        nlohmann::json args;
        args["path"] = path;
        args["content"] = "line1\nlineX\n";
        call.m_arguments = args.dump();
        auto results = executor.execute({call});

        expect(results.size() == 1u);
        expect(handlerCalls == 1);
        expect(confirmPrompt.find(path) != std::string::npos);
        expect(out.anyStyledContains("+1 -1"));        // 摘要行
        expect(out.anyStyledContains("+ lineX"));      // 加行（行号列后）
        expect(out.anyStyledContains("- line2"));      // 删行
        fs::remove(path);
    };

    // ========== E2: edit_file 预览成功 ==========

    "E2 edit_file：previewEdit 成功 → diff + handler 执行"_test = [] {
        auto path = makeTempFile("clf_qa_cap_e2.txt", "aaa\nbbb\n");
        std::vector<CLF::CLFCore::CLFTool> tools;
        int handlerCalls = 0;
        tools.push_back(makeWriteTool("edit_file", handlerCalls));

        MockOutput out;
        CLF::CLFCore::ToolStats stats;
        auto executor = makeWriteExecutor(tools, out, stats,
            [](const std::string&) { return true; });

        CLF::CLFCore::CLFToolCall call;
        call.m_name = "edit_file";
        nlohmann::json args;
        args["path"] = path;
        args["old_string"] = "bbb";
        args["new_string"] = "BBB";
        call.m_arguments = args.dump();
        auto results = executor.execute({call});

        expect(results.size() == 1u);
        expect(handlerCalls == 1);
        expect(out.anyStyledContains("+ BBB"));
        expect(out.anyStyledContains("- bbb"));
        fs::remove(path);
    };

    // ========== E3: edit_file 预览失败（行为保真：错误回传 + handler 不调） ==========

    "E3 edit_file：previewEdit 失败 → 错误回传 + handler 不执行"_test = [] {
        auto path = makeTempFile("clf_qa_cap_e3.txt", "aaa\n");
        std::vector<CLF::CLFCore::CLFTool> tools;
        int handlerCalls = 0;
        tools.push_back(makeWriteTool("edit_file", handlerCalls));

        MockOutput out;
        CLF::CLFCore::ToolStats stats;
        auto executor = makeWriteExecutor(tools, out, stats,
            [](const std::string&) { return true; });

        CLF::CLFCore::CLFToolCall call;
        call.m_name = "edit_file";
        nlohmann::json args;
        args["path"] = path;
        args["old_string"] = "zzz";
        args["new_string"] = "y";
        call.m_arguments = args.dump();
        auto results = executor.execute({call});

        expect(results.size() == 1u);
        expect(handlerCalls == 0);
        expect(results[0].m_content.find("not found in file") != std::string::npos);
        expect(out.anyContentContains("✗"));
        fs::remove(path);
    };

    // ========== E4: TOCTOU（经接口 getFileInfo；确认后文件被改 → 阻断） ==========

    "E4 TOCTOU：确认期间文件被改 → 阻断 + handler 不执行"_test = [] {
        auto path = makeTempFile("clf_qa_cap_e4.txt", "v1");
        std::vector<CLF::CLFCore::CLFTool> tools;
        int handlerCalls = 0;
        tools.push_back(makeWriteTool("write_file", handlerCalls));

        MockOutput out;
        CLF::CLFCore::ToolStats stats;
        // 确认回调内改写文件（size 变化）→ Step 5 TOCTOU 检测
        auto executor = makeWriteExecutor(tools, out, stats,
            [&](const std::string&) {
                std::ofstream f(fs::u8path(path), std::ios::binary | std::ios::trunc);
                f << "v1-CHANGED-LONGER";
                f.close();
                return true;
            });

        CLF::CLFCore::CLFToolCall call;
        call.m_name = "write_file";
        nlohmann::json args;
        args["path"] = path;
        args["content"] = "new";
        call.m_arguments = args.dump();
        auto results = executor.execute({call});

        expect(results.size() == 1u);
        expect(handlerCalls == 0);
        expect(results[0].m_content.find("File modified after preview") != std::string::npos);
        fs::remove(path);
    };

    // ========== E5: 读失败静默保真（不存在路径 = 新文件语义） ==========

    "E5 write_file 不存在路径：读失败静默 → 全 Add + handler 执行"_test = [] {
        std::vector<CLF::CLFCore::CLFTool> tools;
        int handlerCalls = 0;
        tools.push_back(makeWriteTool("write_file", handlerCalls));

        MockOutput out;
        CLF::CLFCore::ToolStats stats;
        auto executor = makeWriteExecutor(tools, out, stats,
            [](const std::string&) { return true; });

        CLF::CLFCore::CLFToolCall call;
        call.m_name = "write_file";
        call.m_arguments = R"({"path":"__clf_no_such_cap_e5__.txt","content":"brand new"})";
        auto results = executor.execute({call});

        expect(results.size() == 1u);
        expect(handlerCalls == 1);       // 未被读失败阻断（新文件语义）
        expect(out.anyStyledContains("+ brand new"));
    };
};

int main() {}
