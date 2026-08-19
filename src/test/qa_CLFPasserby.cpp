// qa_CLFPasserby.cpp — 会话节奏观察器单元测试
// 覆盖：轮数阈值 / 时间条件注入 / 进程内单次 / 输出内容字节校验

#include <boost/ut.hpp>
#include <functional>
#include <string>
#include <vector>

#include "CLFCore/CLFPasserby.hpp"
#include "CLFTypes/ICLFOutput.hpp"

using namespace boost::ut;
using CLF::CLFCore::CLFPasserby;

namespace {

class MockOutput : public CLF::CLFTypes::ICLFOutput {
public:
    void emitContent(const std::string&) override {}
    void emitRaw(const std::string&) override {}
    void emitStyledLine(const std::string& line, LineStyle) override {
        emitted.push_back(line);
    }
    void setStatus(const std::string&, int, int) override {}
    void setStatusTextOnly(const std::string&) override {}
    bool confirm(const std::string&) override { return false; }
    void onInterrupt(std::function<void()>) override {}
    void showProgress(const std::vector<std::string>&) override {}
    void finishProgress(const std::string&) override {}
    void emitError(const std::string&) override {}
    void appendThinking(const std::string&) override {}
    void clearThinking() override {}

    std::vector<std::string> emitted;
};

// 与实现一致的字节序列（UTF-8 十进制），断言不出现明文
std::string encodedName() {
    const unsigned char bytes[] = {233, 131, 173,
                                   228, 186, 172,
                                   229, 141, 142};
    return std::string(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

} // anonymous namespace

const boost::ut::suite<"CLFPasserby"> passerbyTests = [] {

    "未达轮数阈值不触发"_test = [] {
        MockOutput out;
        CLFPasserby p(&out, [] { return true; });
        for (int i = 0; i < 7; ++i) p.onTurnFinished();
        expect(out.emitted.empty());
    };

    "达阈值且时间满足 → 触发一次，输出含解码字节与文案"_test = [] {
        MockOutput out;
        CLFPasserby p(&out, [] { return true; });
        for (int i = 0; i < 8; ++i) p.onTurnFinished();
        expect(out.emitted.size() == 1_u);
        expect(out.emitted[0].find(encodedName()) != std::string::npos);
        expect(out.emitted[0].find("路过") != std::string::npos);
    };

    "每进程仅触发一次（继续调用不再输出）"_test = [] {
        MockOutput out;
        CLFPasserby p(&out, [] { return true; });
        for (int i = 0; i < 30; ++i) p.onTurnFinished();
        expect(out.emitted.size() == 1_u);
    };

    "时间条件不满足 → 永不触发"_test = [] {
        MockOutput out;
        CLFPasserby p(&out, [] { return false; });
        for (int i = 0; i < 30; ++i) p.onTurnFinished();
        expect(out.emitted.empty());
    };

    "无输出通道时安全（不崩溃、无输出）"_test = [] {
        CLFPasserby p(nullptr, [] { return true; });
        for (int i = 0; i < 20; ++i) p.onTurnFinished();
        expect(true);
    };
};

// Boost.UT：测试在静态初始化时注册，cfg 析构时自动运行并输出报告
int main() {
    return 0;
}
