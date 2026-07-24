#include <iostream>
#include <string>

#include "CLFCore/CLFAgentLoop.hpp"

int main(int argc, char* argv[]) {
    std::cout << "CLFCode — CLI Agent Framework for Code" << std::endl;
    std::cout << "Type /exit to quit, /help for commands" << std::endl;
    std::cout << std::endl;

    CLF::CLFCore::CLFAgentConfig config;
    // 从 agent_settings.json 加载配置
    // TODO: 实现配置加载器

    CLF::CLFCore::CLFAgentLoop agent(config);

    std::string input;
    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, input)) {
            break;
        }

        if (input.empty()) {
            continue;
        }

        if (input == "/exit") {
            std::cout << "Goodbye." << std::endl;
            break;
        }

        if (input == "/help") {
            std::cout << "Commands:" << std::endl;
            std::cout << "  /exit  - quit the agent" << std::endl;
            std::cout << "  /help  - show this help" << std::endl;
            std::cout << "  /clear - clear context" << std::endl;
            continue;
        }

        if (input == "/clear") {
            agent.clearContext();
            std::cout << "Context cleared." << std::endl;
            continue;
        }

        std::string response = agent.runTurn(input);
        std::cout << response << std::endl;
        std::cout << std::endl;
    }

    return 0;
}
