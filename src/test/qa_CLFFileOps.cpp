// qa_CLFFileOps.cpp — 文件操作边界单元测试（设计 S1-1）
// F1: editFile 空 old_string 提前拒绝
// F2: 空串校验先于文件 IO（不存在的路径也应报空串错误，而非"文件不存在"）
// F3: 正常替换路径无回归

#include <boost/ut.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "CLFTools/CLFFileOps.hpp"

using namespace boost::ut;
namespace fs = std::filesystem;

namespace {

// 在临时目录创建带内容的文件，返回路径；调用方负责删除
std::string makeTempFile(const std::string& name, const std::string& content) {
    fs::path p = fs::temp_directory_path() / name;
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << content;
    f.close();
    return p.string();
}

} // anonymous namespace

const boost::ut::suite<"CLFFileOps"> tests = [] {
    // ========== F1: 空 old_string 拒绝 ==========

    "F1 空 old_string 返回明确错误"_test = [] {
        auto path = makeTempFile("clf_qa_edit_f1.txt", "hello world");
        auto r = CLF::CLFTools::editFile(path, "", "X");
        expect(!r.m_success);
        expect(r.m_error.find("old_string must not be empty") != std::string::npos);

        // 文件内容未被改动
        std::ifstream in(path, std::ios::binary);
        std::string after((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        expect(after == std::string("hello world"));
        fs::remove(path);
    };

    // F2 是本次修复的形状钉子：校验必须发生在 readFile 之前，
    // 否则空串会先撞上文件读取，错误信息就会变成"文件不存在"
    "F2 空串校验先于文件 IO"_test = [] {
        auto r = CLF::CLFTools::editFile("__clf_no_such_file_qa__.txt", "", "X");
        expect(!r.m_success);
        expect(r.m_error.find("old_string must not be empty") != std::string::npos);
        // 不应退化为读文件失败
        expect(r.m_error.find("not found in file") == std::string::npos);
    };

    // ========== F3: 正常路径无回归 ==========

    "F3a 唯一匹配正常替换"_test = [] {
        auto path = makeTempFile("clf_qa_edit_f3a.txt", "alpha beta gamma");
        auto r = CLF::CLFTools::editFile(path, "beta", "BETA");
        expect(r.m_success);
        expect(r.m_content == std::string("alpha BETA gamma"));
        fs::remove(path);
    };

    "F3b 多次匹配仍报唯一性错误"_test = [] {
        auto path = makeTempFile("clf_qa_edit_f3b.txt", "x x x");
        auto r = CLF::CLFTools::editFile(path, "x", "y");
        expect(!r.m_success);
        expect(r.m_error.find("matches") != std::string::npos);
        fs::remove(path);
    };

    "F3c 未命中报 not found"_test = [] {
        auto path = makeTempFile("clf_qa_edit_f3c.txt", "abc");
        auto r = CLF::CLFTools::editFile(path, "zzz", "y");
        expect(!r.m_success);
        expect(r.m_error.find("not found in file") != std::string::npos);
        fs::remove(path);
    };
};

int main() {}
