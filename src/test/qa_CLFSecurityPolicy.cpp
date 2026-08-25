// qa_CLFSecurityPolicy.cpp — 安全策略单元测试
// 覆盖：4 模式 × 3 风险矩阵、模式名、字符串解析

#include <boost/ut.hpp>
#include <vector>

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

    // ========== S2-2: 危险命令检测 ==========

    "D1 典型危险命令被识别"_test = [] {
        const std::vector<std::string> none;
        expect(CLFSecurityPolicy::isDangerousCommand("rm -rf /", none));
        expect(CLFSecurityPolicy::isDangerousCommand("rm -fr build", none));
        expect(CLFSecurityPolicy::isDangerousCommand("del /s /q C:\\tmp", none));
        expect(CLFSecurityPolicy::isDangerousCommand("rd /s /q build", none));
        expect(CLFSecurityPolicy::isDangerousCommand("git reset --hard HEAD~3", none));
        expect(CLFSecurityPolicy::isDangerousCommand("git push --force origin master", none));
        expect(CLFSecurityPolicy::isDangerousCommand("shutdown /s /t 0", none));
        expect(CLFSecurityPolicy::isDangerousCommand("diskpart", none));
    };

    "D2 大小写与引号变形仍被识别"_test = [] {
        const std::vector<std::string> none;
        expect(CLFSecurityPolicy::isDangerousCommand("RM -RF /tmp", none));
        expect(CLFSecurityPolicy::isDangerousCommand("Remove-Item -Recurse -Force x", none));
        expect(CLFSecurityPolicy::isDangerousCommand("\"rm\" -rf /tmp", none));
    };

    "D3 常规命令不被误判"_test = [] {
        const std::vector<std::string> none;
        expect(!CLFSecurityPolicy::isDangerousCommand("git status", none));
        expect(!CLFSecurityPolicy::isDangerousCommand("cmake --build build", none));
        expect(!CLFSecurityPolicy::isDangerousCommand("ls -la", none));
        expect(!CLFSecurityPolicy::isDangerousCommand("git push origin master", none));
        expect(!CLFSecurityPolicy::isDangerousCommand("", none));
    };

    "D4 白名单前缀命中则跳过检测"_test = [] {
        const std::vector<std::string> allow{"rm -rf build/"};
        expect(!CLFSecurityPolicy::isDangerousCommand("rm -rf build/", allow));
        // 白名单是前缀匹配，未命中前缀的仍然拦截
        expect(CLFSecurityPolicy::isDangerousCommand("rm -rf /etc", allow));
    };

    "D5 实例方法使用自身白名单"_test = [] {
        CLFSecurityPolicy policy(CLFSecurityMode::Auto);
        expect(policy.isDangerousCommand("rm -rf /tmp"));
        policy.setCommandAllowlist({"rm -rf /tmp"});
        expect(!policy.isDangerousCommand("rm -rf /tmp"));
    };

    // 危险检测独立于安全模式：Auto 模式全放行，但危险命令仍应被识别出来
    // （由 CLFToolExecutor 据此强制确认）
    "D6 检测结果不受安全模式影响"_test = [] {
        for (auto mode : CLFSecurityPolicy::kAllModes) {
            CLFSecurityPolicy policy(mode);
            expect(policy.isDangerousCommand("rm -rf /"));
        }
    };
};

// Boost.UT：测试在静态初始化时注册，cfg 析构时自动运行并输出报告
int main() {
    return 0;
}
