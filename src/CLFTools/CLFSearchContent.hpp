// CLFSearchContent.hpp — 文件内容搜索工具
// 递归目录，逐行匹配，跳过忽略目录和超大文件

#pragma once

#include <string>

namespace CLF::CLFTools {

// 搜索文件内容，返回 file:line: content 格式
// pattern:  纯文本匹配
// directory: 搜索根目录
// fileTypes: 逗号分隔扩展名（如 ".cpp,.h"），空则不过滤
std::string searchContent(const std::string& pattern,
                          const std::string& directory,
                          const std::string& fileTypes);

} // namespace CLF::CLFTools
