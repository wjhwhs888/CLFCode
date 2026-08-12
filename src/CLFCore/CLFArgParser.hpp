// CLFArgParser.hpp — 命令行参数解析器
// 解析 argc/argv，返回结构化结果
//
// example:
//   auto args = CLF::CLFCore::CLFArgParser::parse(argc, argv);
//   if (args.showHelp) { ... }

#pragma once

#include <string>

namespace CLF::CLFCore {

struct CLFLaunchArgs {
    bool showHelp    = false;
    bool showVersion = false;
    bool allowWrite  = false;        // --allow-write
    std::string configPath;          // --config <path>
    std::string projectRoot;         // --project-root <path>
    std::string oneShotPrompt;       // --prompt <text>
    std::string promptFilePath;      // --prompt-file <path>
};

class CLFArgParser {
public:
    // 返回 true 表示参数合法，false 表示参数错误（已输出错误信息）
    static bool parse(int argc, char* argv[], CLFLaunchArgs& out);
};

} // namespace CLF::CLFCore
