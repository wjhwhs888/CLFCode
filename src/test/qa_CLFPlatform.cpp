// qa_CLFPlatform.cpp — 平台能力层测试（设计-平台层收敛 §七，2026-09-23）
// P1 executableDir 非空且目录下存在 CLFCode.exe（消重复实现后仍正确）
// P2 dynamicLibraryExtension == ".dll"（Windows 口径钉子）
// P3 makeTempFilePath：绝对路径 + 不创建文件 + 两次调用不碰撞
// P4 consoleSize 无控制台环境返回 false 而非崩溃（允许失败，禁 flaky）
// P5 剪贴板往返一致（无剪贴板环境允许跳过，不引入 flaky）
//
// 测试基建注意（项目既有教训）：qa 运行在 boost::ut 静态初始化期——禁止
// 依赖静态初始化顺序的生产包装；临时文件用 CLFTestTempDir RAII。

#include <boost/ut.hpp>
#include "CLFTestTempDir.hpp"

#include <filesystem>
#include <fstream>
#include <string>

#include "CLFTypes/CLFPlatform.hpp"

using namespace boost::ut;
namespace fs = std::filesystem;

using CLF::CLFCore::CLFPlatform;

namespace {

// 唯一临时目录（RAII 统一设施）
CLFTest::CLFTestTempDir makeTempDir() {
    return CLFTest::CLFTestTempDir("clf_platform_");
}

} // anonymous namespace

const boost::ut::suite<"CLFPlatform"> tests = [] {

    "P1 executableDir 非空且含 CLFCode.exe"_test = [] {
        const std::string dir = CLFPlatform::executableDir();
        expect(!dir.empty());
        if (!dir.empty()) {
            std::error_code ec;
            // 测试 exe 与主程序同输出目录（bin/<config>），CLFCode.exe 应在
            expect(fs::exists(fs::u8path(dir) / "CLFCode.exe", ec));
        }
    };

    "P2 dynamicLibraryExtension == .dll（Windows 口径钉子）"_test = [] {
        expect(std::string(CLFPlatform::dynamicLibraryExtension()) == ".dll");
    };

    "P3 makeTempFilePath：绝对路径 + 不创建 + 两次不碰撞"_test = [] {
        const std::string p1 = CLFPlatform::makeTempFilePath("clf_qa_tmp");
        const std::string p2 = CLFPlatform::makeTempFilePath("clf_qa_tmp");
        expect(!p1.empty());
        expect(!p2.empty());
        expect(p1 != p2);                        // 时钟 + 计数器后缀唯一
        expect(fs::path(p1).is_absolute());      // 契约：返回绝对路径
        std::error_code ec;
        expect(!fs::exists(fs::u8path(p1), ec)); // 契约：不创建文件
    };

    "P4 consoleSize：允许失败、禁崩溃；成功时 w>0 && h>0"_test = [] {
        int w = 0, h = 0;
        const bool ok = CLFPlatform::consoleSize(w, h);
        if (ok) {
            expect(w > 0);
            expect(h > 0);
        }
        // 无控制台环境（ctest 重定向）返回 false 是合法结果——只禁崩溃
    };

    "P5 剪贴板往返：写→读一致（无剪贴板环境允许跳过）"_test = [] {
        const std::string text = "CLFCode clipboard roundtrip 中文测试";
        if (!CLFPlatform::writeClipboard(text)) {
            // 无剪贴板环境（无桌面会话）——跳过不判失败（禁 flaky）
            return;
        }
        expect(CLFPlatform::readClipboard() == text);
    };
};

int main() {}
