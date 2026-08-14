// CLFCommands.cpp — 内置斜杠命令 handler 实现
// 每个命令一个独立函数，通过 registerBuiltinCommands() 批量注册

#include "CLFUI/CLFCommandDispatcher.hpp"
#include "CLFTypes/ICLFOutput.hpp"
#include "CLFCore/CLFAgentLoop.hpp"
#include "CLFCore/CLFConfigLoader.hpp"
#include "CLFCore/CLFSecurityPolicy.hpp"
#include "CLFCore/CLFSessionManager.hpp"
#include "CLFCore/CLFSkillLoader.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

namespace CLF::CLFUI {
using namespace CLF::CLFCore;
using CLF::CLFTypes::ICLFOutput;

namespace {

// ============================================================================
// 会话管理
// ============================================================================

bool cmdExit(const std::string&, const std::string&,
             CLFAgentLoop& agent, const std::string& historyDir,
             ICLFOutput* output) {
    agent.generateAndCacheSummary();  // 生成会话摘要
    agent.saveSession(historyDir, true);  // 归档 latest.json
    if (output) output->emitContent("● 会话已保存。再见 — CLFCode\n");
    return true;
}

bool cmdClear(const std::string&, const std::string&,
              CLFAgentLoop& agent, const std::string& historyDir,
              ICLFOutput* output) {
    agent.generateAndCacheSummary();  // 生成会话摘要
    agent.saveSession(historyDir, true);  // 归档旧会话
    agent.clearContext();
    // F18: 新会话语义从干净状态开始
    if (output) output->setStatusKind(ICLFOutput::StatusKind::None);
    if (output) output->emitContent("✓ 会话已保存，新会话开始\n");
    return true;
}

// ============================================================================
// 信息查询
// ============================================================================

bool cmdHelp(const std::string&, const std::string&,
             CLFAgentLoop&, const std::string&,
             ICLFOutput* output) {
    if (output) output->emitContent(
        "\n● 快捷键\n"
        "  ⎿ 提交        enter / ctrl+d\n"
        "  ⎿ 换行        ctrl+n\n"
        "  ⎿ 粘贴        ctrl+v\n"
        "  ⎿ 思考过程    ctrl+t（折叠/展开推理内容）\n"
        "  ⎿ 中断 Agent  esc（单击，中断后回显上次输入）\n"
        "  ⎿ 退出程序    esc esc（空闲时双击）\n"
        "  ⎿ 切换模式    shift+tab\n"
        "  ⎿ 历史导航    ↑ / ↓（首行↑翻历史，尾行↓回草稿）\n"
        "  ⎿ 滚动内容    鼠标滚轮 / pgup / pgdn / home / end\n"
        "\n"
        "● 确认弹窗\n"
        "  ⎿ ← →     切换选项\n"
        "  ⎿ enter    确认执行 / 返回中断\n"
        "  ⎿ esc      返回（中断 Agent）\n"
        "\n"
        "● 命令\n"
        "  ⎿ /clear      保存并开始新会话\n"
        "  ⎿ /config     显示配置信息\n"
        "  ⎿ /context    显示上下文用量\n"
        "  ⎿ /exit       退出并保存会话\n"
        "  ⎿ /help       显示此帮助\n"
        "  ⎿ /history    显示最近会话\n"
        "  ⎿ /init       初始化项目规则 PROJECTRULES.md\n"
        "  ⎿ /mode       切换安全模式\n"
        "  ⎿ /model      显示当前模型\n"
        "  ⎿ /resume     恢复指定会话\n"
        "  ⎿ /skill      知识库管理\n"
        "  ⎿ /version    显示版本号\n");
    return true;
}

bool cmdModel(const std::string&, const std::string&,
              CLFAgentLoop& agent, const std::string&,
              ICLFOutput* output) {
    const auto& cfg = agent.getConfig();
    if (output) output->emitContent(
        "\n● 当前模型\n"
        "  ⎿ 主模型: " + cfg.m_modelName + "\n"
        "  ⎿ 副模型: " + cfg.m_subModel + "\n");
    return true;
}

bool cmdConfig(const std::string&, const std::string&,
               CLFAgentLoop& agent, const std::string&,
               ICLFOutput* output) {
    const auto& cfg = agent.getConfig();
    if (output) output->emitContent(
        "\n● 配置信息\n"
        "  ⎿ 连接: " + cfg.m_apiBaseUrl + "\n"
        "  ⎿ 模型: " + cfg.m_modelName + " (副: " + cfg.m_subModel + ")\n"
        "  ⎿ 参数: temperature=" + std::to_string(cfg.m_temperature)
            + " top_p=" + std::to_string(cfg.m_topP)
            + " max_tokens=" + std::to_string(cfg.m_maxTokens) + "\n"
        "  ⎿ 流式: " + (cfg.m_stream ? "开" : "关")
            + " | 安全: " + agent.getSecurityModeName() + "\n"
        "  ⎿ 上下文: " + std::to_string(cfg.m_maxContextWindow) + " tokens\n");
    return true;
}

bool cmdContext(const std::string&, const std::string&,
                CLFAgentLoop& agent, const std::string&,
                ICLFOutput* output) {
    const auto& ctx = agent.getContext();
    const auto& cfg = agent.getConfig();
    int used = ctx.estimateTokens();
    int max  = cfg.m_maxContextWindow;
    int pct  = (max > 0) ? (used * 100 / max) : 0;
    int bars = (max > 0) ? (used * 20 / max) : 0;
    if (bars > 20) bars = 20;
    std::string bar;
    for (int i = 0; i < 20; ++i)
        bar += (i < bars) ? "█" : "░";
    // P2-4: 累计 token（仅统计已落定的 usage；0 = 尚未统计，不显示）
    std::string cumulative;
    long long usedTotal = agent.getTotalTokensUsed();
    if (usedTotal > 0) {
        cumulative = "  ⎿ 本次会话累计: "
                   + CLF::CLFCore::formatTokenCount(usedTotal) + " tokens\n";
    }
    if (output) output->emitContent(
        "\n● 上下文用量\n"
        "  ⎿ 用量: " + std::to_string(used) + " / "
            + std::to_string(max) + " tokens"
            + " (" + std::to_string(pct) + "%)\n"
        "  ⎿ [" + bar + "]\n"
        "  ⎿ 剩余: ~" + std::to_string(max - used) + " tokens"
            + (pct >= 80 ? "  ⚠ 建议 /clear" : "") + "\n"
        + cumulative);
    return true;
}

// ============================================================================
// 模式切换
// ============================================================================

bool cmdMode(const std::string&, const std::string& args,
             CLFAgentLoop& agent, const std::string&,
             ICLFOutput* output) {
    if (!args.empty()) {
        agent.setSecurityMode(CLFSecurityPolicy::modeFromString(args));
    }
    if (output) output->emitContent("● 模式: " + std::string(agent.getSecurityModeName()) + "\n");
    return true;
}

// ============================================================================
// 知识库
// ============================================================================

bool cmdSkill(const std::string&, const std::string& args,
              CLFAgentLoop& agent, const std::string&,
              ICLFOutput* output) {
    if (args.empty() || args == "list") {
        auto names = CLFSkillLoader::listNames();
        auto loaded = agent.getLoadedSkills();
        std::string out = "\n● 知识库\n";
        for (const auto& n : names) {
            bool isLoaded = std::find(loaded.begin(), loaded.end(), n) != loaded.end();
            out += "  ⎿ " + n + (isLoaded ? "  [已加载]\n" : "  [未加载]\n");
        }
        if (output) output->emitContent(out);
    } else {
        std::string content = CLFSkillLoader::getContent(args);
        if (content.empty()) {
            if (output) output->emitContent("✗ 未找到: " + args + "\n");
        } else {
            agent.injectSkillToContext(args, content);
            if (output) output->emitContent("✓ 已加载: " + args + "\n");
        }
    }
    return true;
}

// ============================================================================
// 会话恢复
// ============================================================================

bool cmdHistory(const std::string&, const std::string&,
                CLFAgentLoop&, const std::string& historyDir,
                ICLFOutput* output) {
    auto sessions = CLFSessionManager::list(historyDir, 10);
    if (sessions.empty()) {
        if (output) output->emitContent("  ⎿ 暂无已保存的会话\n");
    } else {
        std::string out = "\n● 会话列表\n";
        for (const auto& s : sessions) {
            std::string prefix = s.m_isLatest ? "  ⎿ [当前] " : "  ⎿ ";
            out += prefix + s.m_savedAt + "  " + s.m_title + "\n";
        }
        if (output) output->emitContent(out);
    }
    return true;
}

bool cmdResume(const std::string&, const std::string& args,
               CLFAgentLoop& agent, const std::string& historyDir,
               ICLFOutput* output) {
    auto sessions = CLFSessionManager::list(historyDir, 10);
    if (sessions.empty()) {
        if (output) output->emitContent("  ⎿ 暂无已保存的会话\n");
        return true;
    }
    if (args.empty()) {
        std::string out = "\n● 会话列表\n";
        for (size_t i = 0; i < sessions.size(); ++i) {
            std::string tag = sessions[i].m_isLatest ? " [当前]" : "";
            out += "  ⎿ [" + std::to_string(i + 1) + "] "
                + sessions[i].m_savedAt + "  " + sessions[i].m_title + tag + "\n";
        }
        if (output) output->emitContent(out);
    } else {
        int idx = 0;
        try { idx = std::stoi(args); } catch (...) {}
        if (idx >= 1 && idx <= static_cast<int>(sessions.size())) {
            if (agent.restoreSession(sessions[idx - 1].m_path)) {
                // restoreSession 已回显历史，不再重复显示
                // F18: 恢复后状态点回到干净状态
                if (output) output->setStatusKind(ICLFOutput::StatusKind::None);
            } else {
                if (output) output->emitContent("✗ 恢复失败\n");
            }
        } else {
            if (output) output->emitContent("✗ 无效序号: " + args + "\n");
        }
    }
    return true;
}

// ============================================================================
// 版本信息
// ============================================================================

bool cmdVersion(const std::string&, const std::string&,
                CLFAgentLoop&, const std::string&,
                ICLFOutput* output) {
    std::string verPath = CLFConfigLoader::resolvePath("VERSION");
    std::error_code ec;
    std::string version = "unknown";
    if (std::filesystem::exists(verPath, ec)) {
        std::ifstream file(verPath);
        if (file.is_open()) {
            std::getline(file, version);
        }
    }
    if (output) output->emitContent("● CLFCode " + version + "\n");
    return true;
}

// ============================================================================
// 项目初始化
// ============================================================================

bool cmdInit(const std::string&, const std::string&,
             CLFAgentLoop&, const std::string&,
             ICLFOutput* output) {
    std::string projectRoot = std::filesystem::current_path().string();
    std::string rulesPath = projectRoot + "/PROJECTRULES.md";

    if (std::filesystem::exists(rulesPath)) {
        if (output) output->emitContent(
            "● PROJECTRULES.md 已存在: " + rulesPath + "\n");
        return true;
    }

    // 生成初始模板
    std::string projectName = std::filesystem::path(projectRoot).filename().string();
    std::string templateContent =
        "# " + projectName + " 项目规则\n"
        "\n"
        "> 此文件由 `/init` 自动生成，用于告诉 CLFCode 该项目的约定和规范。\n"
        "> 模型会在每次对话中读取此文件，请根据项目实际情况修改。\n"
        "> **建议控制在 128 行以内**，超过部分可能被截断，无法被模型感知。\n"
        "\n"
        "## 项目概述\n"
        "<!-- 简要描述项目目标和范围 -->\n"
        "\n"
        "## 技术栈\n"
        "<!-- 例如：C++17 / CMake / Ninja -->\n"
        "\n"
        "## 编码规范\n"
        "<!-- 命名规则、代码风格、lint 规则等 -->\n"
        "\n"
        "## 架构约定\n"
        "<!-- 模块划分、依赖方向、设计模式等 -->\n"
        "\n"
        "## 构建与测试\n"
        "<!-- 构建命令、测试框架、CI 流程等 -->\n"
        "\n"
        "## 注意事项\n"
        "<!-- 容易踩坑的地方、特殊约束等 -->\n";

    std::error_code ec;
    if (!std::filesystem::exists(projectRoot, ec)) {
        std::filesystem::create_directories(projectRoot, ec);
    }

    std::ofstream file(rulesPath);
    if (!file.is_open()) {
        if (output) output->emitContent(
            "✗ 无法创建 PROJECTRULES.md: " + rulesPath + "\n");
        return true;
    }
    file << templateContent;
    file.close();

    if (output) output->emitContent(
        "✓ 已创建 PROJECTRULES.md → " + rulesPath + "\n"
        "  请根据项目情况编辑此文件。\n");
    return true;
}

} // anonymous namespace

// ============================================================================
// 注册入口
// ============================================================================

void registerBuiltinCommands(CLFCommandDispatcher& dispatcher) {
    auto reg = [&](std::string name, std::string desc, CLFCommandHandler handler) {
        CLFCommand cmd;
        cmd.m_name        = std::move(name);
        cmd.m_description = std::move(desc);
        cmd.m_handler     = std::move(handler);
        dispatcher.registerCommand(std::move(cmd));
    };
    reg("/exit",    "退出并保存会话",                       cmdExit);
    reg("/help",    "显示帮助信息",                         cmdHelp);
    reg("/clear",   "保存会话并开始新对话",                 cmdClear);
    reg("/model",   "显示当前模型信息",                     cmdModel);
    reg("/mode",    "切换安全模式 /mode <auto|analyze|edit|manual>", cmdMode);
    reg("/config",  "显示当前配置信息",                     cmdConfig);
    reg("/context", "显示上下文用量",                       cmdContext);
    reg("/skill",   "知识库管理 /skill [list|<name>]",     cmdSkill);
    reg("/history", "显示最近保存的会话",                   cmdHistory);
    reg("/resume",  "恢复指定会话 /resume <n>",             cmdResume);
    reg("/init",    "初始化项目规则 PROJECTRULES.md",        cmdInit);
    reg("/version", "显示 CLFCode 版本号",                  cmdVersion);
}

} // namespace CLF::CLFUI
