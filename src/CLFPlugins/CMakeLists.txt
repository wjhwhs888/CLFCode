# ============================================================================
# 生产插件注册（2.3 收尾重构，2026-09-21 用户定调：插件经自己的 CMake 管理）
# 新增插件 = 新目录 + 自己的 CMakeLists.txt + 本文件加一行 add_subdirectory；
# 主 CMake 不再随插件增删而改动。clf_add_plugin 模板继承自主 CMake（父作用域）。
# ============================================================================
add_subdirectory(FileOps)
add_subdirectory(Command)
add_subdirectory(Search)
add_subdirectory(Web)
