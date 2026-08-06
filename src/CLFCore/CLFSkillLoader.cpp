// CLFSkillLoader.cpp — 知识库加载器实现

#include "CLFCore/CLFSkillLoader.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

namespace CLF::CLFCore {

std::map<std::string, std::string> CLFSkillLoader::s_skills;

int CLFSkillLoader::loadFromDir(const std::string& dirPath) {
    s_skills.clear();

    std::error_code ec;
    if (!fs::exists(dirPath, ec) || !fs::is_directory(dirPath, ec)) {
        return 0;
    }

    int count = 0;
    for (const auto& entry : fs::directory_iterator(dirPath, ec)) {
        if (!entry.is_regular_file()) continue;

        std::string path = entry.path().string();
        std::string name = entry.path().stem().string(); // 不含 .md 后缀

        std::ifstream file(path);
        if (!file.is_open()) continue;

        std::ostringstream oss;
        oss << file.rdbuf();
        s_skills[name] = oss.str();
        ++count;
    }

    return count;
}

std::string CLFSkillLoader::getContent(const std::string& name) {
    auto it = s_skills.find(name);
    return (it != s_skills.end()) ? it->second : std::string();
}

std::vector<std::string> CLFSkillLoader::listNames() {
    std::vector<std::string> names;
    names.reserve(s_skills.size());
    for (const auto& [name, _] : s_skills) {
        names.push_back(name);
    }
    return names;
}

} // namespace CLF::CLFCore
