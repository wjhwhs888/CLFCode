// CLFCommandToolHandler.cpp — execute_command handler 共享实现（2.3，2026-09-21）
// 自 CLFBuiltinTools.cpp 迁入（逻辑原样保真）：exitCodeMeansSuccess 白名单 +
// cwd 边界校验（参数化根）+ 脚手架。CLFBuiltinTools 的 detail::exitCodeMeansSuccess
// 保留为转发钉子（qa 直接断言其签名）。

#include "CLFTools/CLFCommandToolHandler.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

#include <nlohmann/json.hpp>

#include "CLFCapabilities/CLFHandlerScaffold.hpp"   // withHandlerScaffold（2.2a 能力域共享）
#include "CLFTools/CLFCommandExec.hpp"
#include "CLFTypes/CLFTextUtil.hpp"   // isWithinWorkspaceOf（2.3 归位）

namespace CLF::CLFTools {

bool exitCodeMeansSuccess(const std::string& command, int exitCode) {
    if (exitCode == 0) return true;
    if (exitCode != 1) return false;   // 仅退出码 1 参与白名单判定

    // 取命令首 token → 去引号 → 去路径 → 去扩展名 → 转小写
    std::istringstream iss(command);
    std::string first;
    iss >> first;
    first.erase(std::remove(first.begin(), first.end(), '"'), first.end());
    if (const auto slash = first.find_last_of("/\\"); slash != std::string::npos) {
        first = first.substr(slash + 1);
    }
    if (const auto dot = first.rfind('.'); dot != std::string::npos) {
        first = first.substr(0, dot);
    }
    for (auto& c : first) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    for (const char* name : {"grep", "rg", "findstr", "diff", "fc"}) {
        if (first == name) return true;
    }
    return false;
}

std::string executeCommandToolHandler(const std::string& args,
                                      const std::string& workspaceRootUtf8,
                                      const std::function<bool()>& isCancelled,
                                      int defaultTimeoutSec,
                                      int maxTimeoutSec) {
    return CLF::CLFCapabilities::withHandlerScaffold(
        args, [&workspaceRootUtf8, &isCancelled,
               defaultTimeoutSec, maxTimeoutSec](const nlohmann::json& params, nlohmann::json& result) {
            std::string command = params.value("command", "");
            // 命令执行层 §10.2（2026-09-23）：请求值缺省用配置默认；有效超时 =
            // min(请求值, 配置上限)——handler 层单一 clamp 点（执行器只留硬顶）
            int timeout = params.value("timeout", defaultTimeoutSec);
            if (timeout < 1) timeout = 1;
            if (timeout > maxTimeoutSec) timeout = maxTimeoutSec;
            std::string cwd = params.value("cwd", "");

            // cwd 须位于工作区内（复用 S2-1 边界校验；根参数化——空根跳过校验，
            // 2.3 插件侧形态）；仅约束该参数本身，命令文本里的 cd 不在管控范围内
            if (!cwd.empty()) {
                std::string boundErr;
                if (!CLF::CLFCore::CLFTextUtil::isWithinWorkspaceOf(
                        workspaceRootUtf8, cwd, boundErr)) {
                    result["success"] = false;
                    result["error"]   = "cwd 无效：" + boundErr;
                    return;
                }
            }

            auto cmdResult = CLF::CLFTools::executeCommand(command, timeout, cwd, isCancelled);
            if (cmdResult.m_interrupted) {
                // 中断语义（设计-中断时效性 A.4）：不假装成功/失败——success=false +
                // interrupted=true + 已有输出保留；"结果未知"由协议闭合层承接
                // （被立即强杀的可能有半截副作用——模型需知"结果未知"不盲目重试）
                result["success"]     = false;
                result["interrupted"] = true;
                result["exitCode"]    = cmdResult.m_exitCode;
                result["stdout"]      = cmdResult.m_stdout;
                std::string err = cmdResult.m_stderr;
                if (!err.empty()) err += "\n";
                result["stderr"] = err + "命令被用户中断（已终止进程树）";
                return;
            }
            result["success"]  = exitCodeMeansSuccess(command, cmdResult.m_exitCode);
            result["exitCode"] = cmdResult.m_exitCode;
            result["stdout"]   = cmdResult.m_stdout;
            result["stderr"]   = cmdResult.m_stderr;
            if (cmdResult.m_timedOut) {
                result["timedOut"] = true;
            }
        });
}

} // namespace CLF::CLFTools
