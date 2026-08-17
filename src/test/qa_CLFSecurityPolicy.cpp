// qa_CLFSecurityPolicy.cpp — 安全策略单元测试
// 覆盖：4 模式 × 3 风险矩阵、模式名、字符串解析

#include <boost/ut.hpp>
#include "CLFCore/CLFSecurityPolicy.hpp"

using namespace boost::ut;
using CLF::CLFCore::CLFSecurityPolicy;
using CLF::CLFCore::CLFSecurityMode;
using CLF::CLFCore::CLFToolRisk;

namespace {

bool allowed(CLFSecurityMode mode, CLFToolRisk risk) {
    bool needConfirm = true;
    bool ok = CLFSecurityPolicy(mode).isAllowed(risk, needConfirm);
    return ok;
}

bool needConfirm(CLFSecurityMode mode, CLFToolRisk risk) {
    bool confirm = false;
    CLFSecurityPolicy(mode).isAllowed(risk, confirm);
    return confirm;
}

} // anonymous namespace

const boost::ut::suite<"CLFSecurityPolicy"> tests = [] {
    "Auto 模式：全部风险放行，无需确认"_test = [] {
        expect(allowed(CLFSecurityMode::Auto, CLFToolRisk::Read));
        expect(allowed(CLFSecurityMode::Auto, CLFToolRisk::Write));
        expect(allowed(CLFSecurityMode::Auto, CLFToolRisk::Command));
        expect(!needConfirm(CLFSecurityMode::Auto, CLFToolRisk::Command));
    };

    "Analyze 模式：读放行，写/命令阻断"_test = [] {
        expect(allowed(CLFSecurityMode::Analyze, CLFToolRisk::Read));
        expect(!allowed(CLFSecurityMode::Analyze, CLFToolRisk::Write));
        expect(!allowed(CLFSecurityMode::Analyze, CLFToolRisk::Command));
    };

    "Edit 模式：写/命令允许但需确认"_test = [] {
        expect(allowed(CLFSecurityMode::Edit, CLFToolRisk::Read));
        expect(!needConfirm(CLFSecurityMode::Edit, CLFToolRisk::Read));
        expect(allowed(CLFSecurityMode::Edit, CLFToolRisk::Write));
        expect(needConfirm(CLFSecurityMode::Edit, CLFToolRisk::Write));
        expect(allowed(CLFSecurityMode::Edit, CLFToolRisk::Command));
        expect(needConfirm(CLFSecurityMode::Edit, CLFToolRisk::Command));
    };

    "Manual 模式：与 Edit 相同（确认）"_test = [] {
        expect(allowed(CLFSecurityMode::Manual, CLFToolRisk::Write));
        expect(needConfirm(CLFSecurityMode::Manual, CLFToolRisk::Write));
        expect(needConfirm(CLFSecurityMode::Manual, CLFToolRisk::Command));
    };

    "modeFromString 解析 + 未知值回退 Edit"_test = [] {
        expect(CLFSecurityPolicy::modeFromString("auto") == CLFSecurityMode::Auto);
        expect(CLFSecurityPolicy::modeFromString("analyze") == CLFSecurityMode::Analyze);
        expect(CLFSecurityPolicy::modeFromString("edit") == CLFSecurityMode::Edit);
        expect(CLFSecurityPolicy::modeFromString("manual") == CLFSecurityMode::Manual);
        expect(CLFSecurityPolicy::modeFromString("unknown") == CLFSecurityMode::Edit);
    };

    "getModeName 返回正确名称"_test = [] {
        // 注意：getModeName 返回 const char*，与字面量直接 == 是指针比较，
        // 跨翻译单元字面量是否同址依赖链接器合并（GCC 合并 / MSVC Debug 不合并）
        expect(std::string(CLFSecurityPolicy(CLFSecurityMode::Auto).getModeName()) == "auto");
        expect(std::string(CLFSecurityPolicy(CLFSecurityMode::Analyze).getModeName()) == "analyze");
        expect(std::string(CLFSecurityPolicy(CLFSecurityMode::Edit).getModeName()) == "edit");
        expect(std::string(CLFSecurityPolicy(CLFSecurityMode::Manual).getModeName()) == "manual");
    };

    "setMode 动态切换生效"_test = [] {
        CLFSecurityPolicy policy(CLFSecurityMode::Analyze);
        bool confirm = false;
        expect(!policy.isAllowed(CLFToolRisk::Command, confirm));
        policy.setMode(CLFSecurityMode::Auto);
        expect(policy.isAllowed(CLFToolRisk::Command, confirm));
    };
};

// Boost.UT：测试在静态初始化时注册，cfg 析构时自动运行并输出报告
int main() {
    return 0;
}
