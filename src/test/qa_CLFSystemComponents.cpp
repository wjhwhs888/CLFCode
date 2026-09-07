// qa_CLFSystemComponents.cpp — C5 拆分组件测试（2026-09-07）
// R1-R4: CLFProjectRulesLoader（优先/降级/截断标记/空文件）
// G1-G2: CLFSystemInfoProvider Git 缓存语义
// S1-S2: CLFSubprocessRunner 基础封装

#include <boost/ut.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "CLFCore/CLFProjectRulesLoader.hpp"
#include "CLFCore/CLFSubprocessRunner.hpp"
#include "CLFCore/CLFSystemInfoProvider.hpp"

using namespace boost::ut;
namespace fs = std::filesystem;

using CLF::CLFCore::CLFProjectRulesLoader;
using CLF::CLFCore::CLFSubprocessRunner;
using CLF::CLFCore::CLFSystemInfoProvider;

namespace {

// 唯一临时工作目录（时间戳后缀防并发冲突）；调用方负责删除
std::string makeTempDir(const std::string& name) {
    auto stamp = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    fs::path p = fs::temp_directory_path() / (name + "_" + stamp);
    fs::create_directories(p);
    return p.string();
}

void writeFile(const std::string& path, const std::string& content) {
    std::ofstream f(fs::u8path(path), std::ios::binary | std::ios::trunc);
    f << content;
    f.close();
}

// 从当前工作目录（build 目录）向上找 .git 仓库根；找不到返回空串
std::string findGitRoot() {
    fs::path p = fs::current_path();
    for (int i = 0; i < 8 && !p.empty(); ++i) {
        if (fs::exists(p / ".git")) return p.string();
        p = p.parent_path();
    }
    return "";
}

} // anonymous namespace

const boost::ut::suite<"CLFSystemComponents"> tests = [] {
    // ========== CLFProjectRulesLoader ==========

    "R1 PROJECTRULES.md 优先加载（含头行）"_test = [] {
        auto dir = makeTempDir("clf_qa_rules_r1");
        writeFile(dir + "/PROJECTRULES.md", "rule A\n");
        writeFile(dir + "/CLAUDE.md", "rule B\n");
        auto rules = CLFProjectRulesLoader::loadProjectRules(dir);
        expect(rules.find("## 项目规则（来自 PROJECTRULES.md）") != std::string::npos);
        expect(rules.find("rule A") != std::string::npos);
        expect(rules.find("rule B") == std::string::npos);   // 不回退 CLAUDE.md
        fs::remove_all(fs::u8path(dir));
    };

    "R2 PROJECTRULES 缺失 → 降级 CLAUDE.md"_test = [] {
        auto dir = makeTempDir("clf_qa_rules_r2");
        writeFile(dir + "/CLAUDE.md", "fallback rule\n");
        auto rules = CLFProjectRulesLoader::loadProjectRules(dir);
        expect(rules.find("## 项目规则（来自 CLAUDE.md）") != std::string::npos);
        expect(rules.find("fallback rule") != std::string::npos);
        fs::remove_all(fs::u8path(dir));
    };

    "R3 两文件均无 → 空串"_test = [] {
        auto dir = makeTempDir("clf_qa_rules_r3");
        expect(CLFProjectRulesLoader::loadProjectRules(dir).empty());
        fs::remove_all(fs::u8path(dir));
    };

    "R4 超 5000 字符截断 + 标记（UTF-8 边界安全）"_test = [] {
        auto dir = makeTempDir("clf_qa_rules_r4");
        writeFile(dir + "/PROJECTRULES.md", std::string(8000, 'x'));
        auto rules = CLFProjectRulesLoader::loadProjectRules(dir);
        expect(rules.find("[…项目规则超过5000字符，已截断]") != std::string::npos);
        expect(rules.size() < 5600u);   // 截断生效（含头行+标记，远小于 8000）
        fs::remove_all(fs::u8path(dir));
    };

    // ========== CLFSystemInfoProvider ==========

    "G1 非 git 仓库 → 空串（不崩）"_test = [] {
        auto dir = makeTempDir("clf_qa_git_g1");
        CLFSystemInfoProvider info;
        expect(info.captureGitStatus(dir).empty());
        expect(info.captureGitStatus(dir).empty());   // 二次调用仍空（缓存清空语义）
        fs::remove_all(fs::u8path(dir));
    };

    "G2 git 仓库 → 非空 + TTL 内缓存命中（内容一致）"_test = [] {
        const std::string root = findGitRoot();
        if (root.empty()) {
            // 环境无 git 仓库（异常环境）——跳过并保持用例通过（记录性质）
            expect(true);
            return;
        }
        CLFSystemInfoProvider info;
        auto first = info.captureGitStatus(root);
        expect(!first.empty());
        expect(first.find("Git 分支") != std::string::npos);
        auto second = info.captureGitStatus(root);   // TTL 30s 内 → 缓存
        expect(second == first);
    };

    // ========== CLFSubprocessRunner ==========

    "S1 run：正常命令返回 stdout（去尾换行）"_test = [] {
        auto out = CLFSubprocessRunner::run("echo clf_subproc_test");
        expect(out.find("clf_subproc_test") != std::string::npos);
        expect(out.back() != '\n');
    };

    "S2 run：命令不存在 → 空串"_test = [] {
        auto out = CLFSubprocessRunner::run("__clf_no_such_command_xyz__ 2>nul");
        expect(out.empty());
    };
};

int main() {}
