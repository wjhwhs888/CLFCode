// CLFArgParser.cpp — 命令行参数解析实现
// 手写轻量解析，不引入第三方库

#include "CLFCore/CLFArgParser.hpp"

#include <cstring>
#include <iostream>
#include <string>

namespace CLF::CLFCore {

bool CLFArgParser::parse(int argc, char* argv[], CLFLaunchArgs& out) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        // 需要带值的参数
        auto getValue = [&](const std::string& name) -> std::string {
            if (i + 1 >= argc || argv[i + 1][0] == '-') {
                std::cerr << "[ERROR] " << name << " requires a value" << std::endl;
                return "";
            }
            return argv[++i];
        };

        if (arg == "--help" || arg == "-h") {
            out.showHelp = true;
        } else if (arg == "--version" || arg == "-v") {
            out.showVersion = true;
        } else if (arg == "--allow-write") {
            out.allowWrite = true;
        } else if (arg == "--config") {
            out.configPath = getValue("--config");
            if (out.configPath.empty()) return false;
        } else if (arg == "--project-root") {
            out.projectRoot = getValue("--project-root");
            if (out.projectRoot.empty()) return false;
        } else if (arg == "--prompt") {
            out.oneShotPrompt = getValue("--prompt");
            if (out.oneShotPrompt.empty()) return false;
        } else if (arg == "--prompt-file") {
            out.promptFilePath = getValue("--prompt-file");
            if (out.promptFilePath.empty()) return false;
        } else {
            std::cerr << "[ERROR] Unknown option: " << arg << std::endl;
            std::cerr << "  Use --help for usage information" << std::endl;
            return false;
        }
    }

    // 互斥检查
    if (!out.oneShotPrompt.empty() && !out.promptFilePath.empty()) {
        std::cerr << "[ERROR] Cannot use --prompt and --prompt-file together" << std::endl;
        return false;
    }

    return true;
}

} // namespace CLF::CLFCore
