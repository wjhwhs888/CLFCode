// qa_CLFToolScheduler.cpp — 并发工具调度器测试（事件化重构分册 3 §五）
// 第一批：C 组分类测试（C1-C5）。G/O/R/P/A/F/S/E 组随调度器主体实施补入。

#include <boost/ut.hpp>

#include "CLFCore/CLFToolScheduler.hpp"
#include "CLFTypes/CLFTypes.hpp"

using namespace boost::ut;
using CLF::CLFCore::CLFTool;
using CLF::CLFCore::ToolExecMode;
using CLF::CLFCore::classifyMode;

namespace {

CLFTool makeTool(const std::string& name) {
    CLFTool t;
    t.m_name = name;
    t.m_risk = CLF::CLFCore::CLFToolRisk::Read;
    return t;
}

} // anonymous namespace

const boost::ut::suite<"CLFToolScheduler"> tests = [] {
    "C1 未声明 = 独占（fail-closed 默认）"_test = [] {
        CLFTool t = makeTool("plain");
        expect(classifyMode(t, "{}") == ToolExecMode::Exclusive);
    };

    "C2 声明 true = 并行"_test = [] {
        CLFTool t = makeTool("parallel");
        t.m_isConcurrencySafe = [](const std::string&) { return true; };
        expect(classifyMode(t, "{}") == ToolExecMode::Parallel);
    };

    "C3 非 true 返回（含异常）= 独占"_test = [] {
        CLFTool t1 = makeTool("false-decl");
        t1.m_isConcurrencySafe = [](const std::string&) { return false; };
        expect(classifyMode(t1, "{}") == ToolExecMode::Exclusive);

        CLFTool t2 = makeTool("throw-decl");
        // 判定内抛异常（违反 dsh §16.2 约束的第三方声明）→ catch 兜底独占
        t2.m_isConcurrencySafe = [](const std::string&) -> bool {
            throw std::runtime_error("boom");
        };
        expect(classifyMode(t2, "{}") == ToolExecMode::Exclusive);
    };

    "C4 逐参数判定：读模式并行、写模式独占"_test = [] {
        CLFTool t = makeTool("param-sensitive");
        t.m_isConcurrencySafe = [](const std::string& argsJson) {
            // 参数含 "write" 特征 → 独占
            return argsJson.find("\"mode\":\"write\"") == std::string::npos;
        };
        expect(classifyMode(t, R"({"mode":"read","path":"a.txt"})")
               == ToolExecMode::Parallel);
        expect(classifyMode(t, R"({"mode":"write","path":"a.txt"})")
               == ToolExecMode::Exclusive);
    };

    "C5 判定不进工具描述/schema（纯调度元数据——不泄漏给模型）"_test = [] {
        CLFTool t = makeTool("meta");
        t.m_description = "读取文件内容";
        t.m_parametersSchema = R"({"type":"object","properties":{"path":{"type":"string"}}})";
        t.m_isConcurrencySafe = [](const std::string&) { return true; };
        // 并发声明不进 m_description/m_parametersSchema
        expect(t.m_description.find("并发") == std::string::npos);
        expect(t.m_description.find("concurren") == std::string::npos);
        expect(t.m_parametersSchema.find("concurren") == std::string::npos);
    };
};

int main() {}
