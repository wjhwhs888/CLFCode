// qa_CLFSearchContent.cpp — search_content head/tail 环形截断测试（T2'）
// P0-2: 停表式截断 → head 240 + tail 240 环形缓冲（总预算 500 不变）

#include <boost/ut.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "CLFTools/CLFSearchContent.hpp"

using namespace boost::ut;
using CLF::CLFTools::searchContent;

namespace {

// 临时目录 RAII（构造时创建，析构时清理）
struct TempDir {
    std::filesystem::path p;
    TempDir() {
        auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        p = std::filesystem::temp_directory_path()
          / ("clf_search_test_" + std::to_string(suffix));
        std::filesystem::create_directories(p);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(p, ec);
    }
};

// 写入含 N 行 "needle line i" 的文件，返回行列表
void writeNeedleFile(const std::filesystem::path& file, int n) {
    std::ofstream f(file);
    for (int i = 0; i < n; ++i)
        f << "needle line " << i << "\n";
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) {
            lines.push_back(text.substr(pos));
            break;
        }
        lines.push_back(text.substr(pos, nl - pos));
        pos = nl + 1;
    }
    if (!lines.empty() && lines.back().empty()) lines.pop_back();  // 尾部换行
    return lines;
}

} // anonymous namespace

const boost::ut::suite<"CLFSearchContent"> tests = [] {
    "恰好 500 结果：head 240 + 省略 20 + tail 240 + 截断标记"_test = [] {
        TempDir dir;
        writeNeedleFile(dir.p / "f.txt", 500);

        auto out = searchContent("needle", dir.p.string(), "");
        auto lines = splitLines(out);

        expect(lines.size() == 482);  // 240 + 1 省略 + 240 + 1 截断标记
        expect(lines[0] == "f.txt:1: needle line 0");
        expect(lines[239] == "f.txt:240: needle line 239");
        expect(lines[240] == "[中间省略 20 行]");
        expect(lines[241] == "f.txt:261: needle line 260");
        expect(lines[480] == "f.txt:500: needle line 499");
        expect(lines[481].find("[结果超过 500 行，已截断]") != std::string::npos);
    };

    "300 结果（<480）：无省略标记，顺序完整"_test = [] {
        TempDir dir;
        writeNeedleFile(dir.p / "f.txt", 300);

        auto out = searchContent("needle", dir.p.string(), "");
        auto lines = splitLines(out);

        expect(lines.size() == 300);
        expect(lines[0] == "f.txt:1: needle line 0");
        expect(lines[299] == "f.txt:300: needle line 299");
        for (const auto& l : lines)
            expect(l.find("省略") == std::string::npos);
    };

    "无匹配：'(no matches)'"_test = [] {
        TempDir dir;
        writeNeedleFile(dir.p / "f.txt", 5);

        auto out = searchContent("zzz-no-such", dir.p.string(), "");
        expect(out == "(no matches)");
    };
};

int main() {}
