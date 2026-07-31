// CLFSkillLoader.hpp — 知识库（Skills）加载器
// 从 data/skills/ 目录读取 .md 规则文件，提供按名查询
//
// example:
//   CLF::CLFCore::CLFSkillLoader::loadFromDir("data/skills/");
//   auto names = CLF::CLFCore::CLFSkillLoader::listNames();
//   std::string content = CLF::CLFCore::CLFSkillLoader::getContent("architecture");

#pragma once

#include <map>
#include <string>
#include <vector>

namespace CLF::CLFCore {

class CLFSkillLoader {
public:
    // 从指定目录加载所有 .md 文件，返回加载数量
    // 目录不存在或为空时返回 0
    static int loadFromDir(const std::string& dirPath);

    // 获取 skill 内容（按文件名，不带 .md 后缀），不存在返回空字符串
    static std::string getContent(const std::string& name);

    // 列出所有已加载的 skill 名称
    static std::vector<std::string> listNames();

    // 清空已加载内容
    static void clear();

private:
    static std::map<std::string, std::string> s_skills;
};

} // namespace CLF::CLFCore
