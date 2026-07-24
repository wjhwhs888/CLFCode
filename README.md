# CLFCode — CLI Agent Framework for Code

本地运行的 AI Coding Agent，具备文件操作、命令执行、网络 API 调用等工具能力。

## 依赖

- **编译器**：GCC 15+ / Clang 20+（C++17）
- **构建工具**：CMake 3.20+ / Ninja
- **第三方库**（Header-Only，已包含在 `3rdparty/`）：
  - [cpp-httplib](https://github.com/yhirose/cpp-httplib) — HTTP/HTTPS 客户端
  - [nlohmann/json](https://github.com/nlohmann/json) — JSON 解析

## 快速开始

```bash
# 配置
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -G Ninja

# 编译
cmake --build build -j6

# 运行
./bin/Debug/CLFCode

# 测试
ctest --test-dir build --output-on-failure -j6
```

## 项目结构

详见 `ProjectSetting.md`。
