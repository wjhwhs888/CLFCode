// qa_CLFWebFetch.cpp — 网络抓取辅助函数单元测试（设计 S2-5）
// W1: splitUrl URL 拆分
// W2: headTailCapBytes 字节级截断 + UTF-8 边界安全
// W3: looksBinary 二进制探测
// 说明：不做真实网络请求，只覆盖纯函数

#include <boost/ut.hpp>

#include <string>

#include "CLFTools/CLFWebFetch.hpp"
#include "CLFTypes/CLFEncoding.hpp"

using namespace boost::ut;
using CLF::CLFCore::CLFEncoding;
using CLF::CLFTools::detail::headTailCapBytes;
using CLF::CLFTools::detail::looksBinary;
using CLF::CLFTools::detail::splitUrl;

const boost::ut::suite<"CLFWebFetch"> tests = [] {
    // ========== W1: URL 拆分 ==========

    "W1a 带路径的 URL"_test = [] {
        std::string base, path;
        expect(splitUrl("https://example.com/a/b", base, path));
        expect(base == std::string("https://example.com"));
        expect(path == std::string("/a/b"));
    };

    "W1b 无路径时补 /"_test = [] {
        std::string base, path;
        expect(splitUrl("https://example.com", base, path));
        expect(base == std::string("https://example.com"));
        expect(path == std::string("/"));
    };

    "W1c 保留 query 与端口"_test = [] {
        std::string base, path;
        expect(splitUrl("http://host:8080/x?y=1&z=2", base, path));
        expect(base == std::string("http://host:8080"));
        expect(path == std::string("/x?y=1&z=2"));
    };

    "W1d 非法 URL 被拒"_test = [] {
        std::string base, path;
        expect(!splitUrl("example.com/a", base, path));   // 缺 scheme
        expect(!splitUrl("", base, path));
        expect(!splitUrl("https://", base, path));        // 缺 host
    };

    // ========== W2: 字节级截断 ==========

    "W2a 未超限时原样返回"_test = [] {
        const std::string s(100, 'x');
        expect(headTailCapBytes(s, 80, 40) == s);
    };

    "W2b 超限时保留首尾并插入省略标记"_test = [] {
        const std::string s(1000, 'x');
        const auto out = headTailCapBytes(s, 100, 50);
        expect(out.size() < s.size());
        expect(out.find("中间省略") != std::string::npos);
        expect(out.rfind("xxxxx", 0) == 0);          // 以 head 开头
        expect(out.back() == 'x');                   // 以 tail 结尾
    };

    // W2c 是本函数的核心约束：切点不得落在多字节字符中间，
    // 否则结果不是合法 UTF-8，序列化进 JSON 会抛异常
    "W2c 截断后仍是合法 UTF-8（切点回退到字符边界）"_test = [] {
        // 全中文（每字 3 字节），刻意让切点落在字符中间
        std::string s;
        for (int i = 0; i < 400; ++i) s += "中";   // 1200 字节
        expect(CLFEncoding::isValidUtf8(s));

        // 逐个尝试可能切坏字符的边界值
        for (size_t head : {100u, 101u, 102u, 103u}) {
            for (size_t tail : {50u, 51u, 52u}) {
                const auto out = headTailCapBytes(s, head, tail);
                expect(CLFEncoding::isValidUtf8(out))
                    << "head=" << head << " tail=" << tail;
            }
        }
    };

    "W2d 混合 ASCII 与多字节仍合法"_test = [] {
        std::string s;
        for (int i = 0; i < 200; ++i) s += "ab中cd文";
        expect(CLFEncoding::isValidUtf8(s));
        const auto out = headTailCapBytes(s, 111, 57);
        expect(CLFEncoding::isValidUtf8(out));
    };

    // ========== W3: 二进制探测 ==========

    "W3a 纯文本不判为二进制"_test = [] {
        expect(!looksBinary("hello world"));
        expect(!looksBinary("中文内容\n第二行"));
        expect(!looksBinary(""));
    };

    "W3b 含 NUL 判为二进制"_test = [] {
        std::string s = "MZ";
        s.push_back('\0');
        s += "binary payload";
        expect(looksBinary(s));
    };

    "W3c NUL 在探测区之外则不判定"_test = [] {
        std::string s(9000, 'x');   // 超过 8KB 探测区
        s.push_back('\0');
        expect(!looksBinary(s));
    };
};

int main() {}
