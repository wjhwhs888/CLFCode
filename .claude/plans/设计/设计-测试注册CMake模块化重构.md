# 设计-测试注册 CMake 模块化重构

> **状态**：定稿待实施（pro 照抄即可，无需调查/决策）
> **创建**：2026-09-11
> **目标**：把 `src/CMakeLists.txt` 里的测试注册段（159 行）迁出为独立的 `src/test/CMakeLists.txt`；顶层提供 `CLF_BUILD_TESTS` 开关；`CLF_TEST_TARGETS` 双维护消除
> **原则**：零行为差异（测试名/链接/产物位置/工作目录全部不变）、零源码改动、pro 零调查
> **取证**：2026-09-11 全量实读（顶层 CMakeLists 67 行 + src/CMakeLists 272 行 + src/test 31 个 cpp + 构建目录 CTestTestfile + release.ps1 + .idea + 测试源码文件操作 grep）

---

## 一、现状事实（取证清单）

| 项 | 事实 | 出处 |
|---|---|---|
| 顶层 | `cmake_minimum_required(VERSION 3.20)`；`enable_testing()` **无条件**（注释"顶层启用，src/ 下 add_test 注册"） | `CMakeLists.txt:1-5` |
| 顶层 | C++17 全局；编译选项 `/W4 /utf-8`（MSVC）或 `-Wall -Wextra -Werror` | `CMakeLists.txt:12-21` |
| 顶层 | boost-ut 经 `include_directories(SYSTEM ...)`（:28）→ 对子目录自动继承 | `CMakeLists.txt:26-28` |
| 顶层 | OpenSSL 检测产出 `CLF_OPENSSL_LIBS`（手动路径分支已含 `ws2_32`） | `CMakeLists.txt:32-60` |
| 顶层 | 产物输出目录 `bin/$<CONFIG>` / `lib/$<CONFIG>`（对子目录生效） | `CMakeLists.txt:63-65` |
| src | 库定义 `:1-109`（clf_types → clf_capabilities → clf_network → clf_core → clf_ui → clf_tools → CLFCode） | `src/CMakeLists.txt:1-109` |
| src | 测试段 `:111-272` = `CLF_TEST_TARGETS` 名单（:114-125）+ 31 组 `add_executable/link/add_test`（:127-268）+ `foreach` 设 CXX20（:270-272） | `src/CMakeLists.txt:111-272` |
| test 目录 | 31 个 `qa_*.cpp` **平铺**、无 `CMakeLists.txt`、无子目录；源文件名与 target 名一一对应 | 实读 |
| 引用面 | `CLF_TEST_TARGETS` 全仓仅 `src/CMakeLists.txt` 一处定义+使用；`*.ps1` 零命中；**无 CI 配置**（无 .github/.gitlab-ci） | grep 实证 |
| 发布脚本 | `release.ps1:61` 用 `cmake --build ... --target CLFCode`——**只构建主程序，不受本改动影响** | `release.ps1:40-61` |
| IDE | `.idea` 存在但**无 runConfigurations 目录**（CLion 靠自动识别 target）→ target 名不变即零影响 | 实读 |
| ⚠ 工作目录 | 现状 `CTestTestfile.cmake` **无 WORKING_DIRECTORY 属性** → ctest 默认工作目录 = `${CMAKE_BINARY_DIR}/src`（即 `cmake-build-debug/src`）。**注册位置搬到子目录后会默认变成 `.../src/test`——必须显式固定回原值** | 构建目录实读 |
| 测试文件操作 | 31 个测试的文件读写**全部走 `temp_directory_path()`**（系统临时目录）；唯一相对路径字符串 `"./sub/b.txt"`（qa_CLFBuiltinTools:27）是纯路径判定、不触文件系统 → cwd 敏感度低，但按上一条仍显式固定 | grep 实证 |

---

## 二、目标结构

```
CMakeLists.txt（顶层，67 → 70 行）
    + option(CLF_BUILD_TESTS ...)  + enable_testing() 收进条件

src/CMakeLists.txt（272 → 113 行）
    - 删除 111-272 整段（测试注册）
    + 末尾追加 4 行：if(CLF_BUILD_TESTS) add_subdirectory(test) endif()

src/test/CMakeLists.txt（新建，约 80 行）
    set(CMAKE_CXX_STANDARD 20)          # 子目录作用域，不影响 src 的 C++17
    function(clf_add_test ...)          # 一行注册一个测试
    31 行测试定义（链接关系与现状逐条一致）
```

**不做/不动**（防越界）：测试源码 `.cpp` 零改动；target 名零改动；顶层 C++17/编译选项/输出目录/OpenSSL 检测零改动；`release.ps1`/`install.ps1`/`upgrade.ps1` 零改动；`ftxui` 子目录零改动。

---

## 三、改动 1：顶层 `CMakeLists.txt`（唯一一处，替换 :4-5）

**现状（:4-5）**：
```cmake
# 测试支持（顶层启用，src/ 下 add_test 注册）
enable_testing()
```

**改为**：
```cmake
# 测试支持（顶层启用 + 开关；src/CMakeLists.txt 末尾 add_subdirectory(test) 注册）
# 默认 ON 保持 CLion/日常体验不变；发布/纯程序构建可 -DCLF_BUILD_TESTS=OFF
option(CLF_BUILD_TESTS "构建并注册单元测试（qa_*，Boost.UT，C++20）" ON)
if(CLF_BUILD_TESTS)
    enable_testing()
endif()
```

> 说明：`enable_testing()` 必须在**顶层**调用（CMake 语义），故保留在本文件、仅收进条件；`OFF` 时 ctest 无测试但主程序照常构建。

---

## 四、改动 2：`src/CMakeLists.txt`（删一段、加一段）

**删除**：从 `:111` 的分隔注释开始到文件末尾（:272）——即：
```
# ============================================================================
# 测试（Boost.UT，L1 单元 + L2 Mock 集成）
# ============================================================================
set(CLF_TEST_TARGETS ...)
...（31 组 add_executable/target_link_libraries/add_test）...
foreach(target IN LISTS CLF_TEST_TARGETS)
    set_target_properties(${target} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
endforeach()
```
（删除量 162 行；删完后文件以 `CLFCode` 主程序的 `endif()`（:109）结尾）

**在文件末尾追加**：
```cmake

# ============================================================================
# 测试（迁至 test/CMakeLists.txt 统一管理；Boost.UT，L1 单元 + L2 Mock 集成）
# 注意：必须位于全部库 target 定义之后（target_link_libraries 引用未定义
# target 会被当作 -l<name> 字符串传递给链接器）
# ============================================================================
if(CLF_BUILD_TESTS)
    add_subdirectory(test)
endif()
```

---

## 五、改动 3：新建 `src/test/CMakeLists.txt`（全文照抄落盘）

```cmake
# ============================================================================
# 单元/集成测试注册（Boost.UT；从 src/CMakeLists.txt 迁入，2026-09-11）
# 由顶层 option(CLF_BUILD_TESTS) 控制是否 add_subdirectory 本目录。
# 约定：
#   - 一个测试 = 一个 target = 同名 .cpp（qa_XXX → qa_XXX.cpp）
#   - 产物输出沿用顶层设定：${CMAKE_SOURCE_DIR}/bin/$<CONFIG>/
#   - 测试专用 C++20（Boost.UT 要求）；子目录作用域，不影响 src/ 的 C++17
#   - WORKING_DIRECTORY 显式固定为 ${CMAKE_BINARY_DIR}/src——与迁移前
#     add_test 位于 src/CMakeLists.txt 时的默认值完全一致（消除行为差异）
# ============================================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 一行注册一个测试：clf_add_test(<target 名> <链接库...>)
function(clf_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE ${ARGN})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src")
endfunction()

# —— 仅 clf_types ——
clf_add_test(qa_CLFPeriodicTimer    clf_types)
clf_add_test(qa_CLFSessionUsage     clf_types)
clf_add_test(qa_CLFTextUtil         clf_types)

# —— clf_capabilities ——
clf_add_test(qa_CLFFileOps          clf_capabilities)

# —— clf_core ——
clf_add_test(qa_CLFToolExecutor     clf_core)
clf_add_test(qa_CLFRetryPolicy      clf_core)
clf_add_test(qa_CLFMessageCodec     clf_core)
clf_add_test(qa_CLFPasserby         clf_core)
clf_add_test(qa_CLFTodoStore        clf_core)
clf_add_test(qa_CLFSessionFileCtx   clf_core)
clf_add_test(qa_CLFSummaryCache     clf_core)
clf_add_test(qa_CLFContextWindow    clf_core)
clf_add_test(qa_CLFSystemComponents clf_core)
clf_add_test(qa_CLFConfigLoader     clf_core)

# —— clf_core + clf_ui ——
clf_add_test(qa_CLFContext          clf_core clf_ui)
clf_add_test(qa_CLFProtocolAdapter  clf_core clf_ui)
clf_add_test(qa_CLFSecurityPolicy   clf_core clf_ui)
clf_add_test(qa_CLFSessionManager   clf_core clf_ui)
clf_add_test(qa_CLFStreamAccumulator clf_core clf_ui)
clf_add_test(qa_CLFHeadTail         clf_core clf_ui)
clf_add_test(qa_CLFPasteCoalescer   clf_core clf_ui)
clf_add_test(qa_CLFSelectionModel   clf_core clf_ui)
clf_add_test(qa_CLFInputRender      clf_core clf_ui)
clf_add_test(qa_CLFTipsBar          clf_core clf_ui)

# —— clf_core + clf_capabilities ——
clf_add_test(qa_CLFCapabilities     clf_core clf_capabilities)

# —— clf_ui ——
clf_add_test(qa_CLFAnsiParser       clf_ui)

# —— clf_tools ——
clf_add_test(qa_CLFSearchContent    clf_tools)

# —— clf_core + clf_network + clf_ui + OpenSSL ——
clf_add_test(qa_CLFAgentLoop  clf_core clf_network clf_ui ${CLF_OPENSSL_LIBS})
clf_add_test(qa_CLFTodoPanel  clf_core clf_network clf_ui ${CLF_OPENSSL_LIBS})

# —— clf_tools + OpenSSL（Windows 另需 ws2_32/crypt32）——
if(WIN32)
    clf_add_test(qa_CLFWebFetch     clf_tools ${CLF_OPENSSL_LIBS} ws2_32 crypt32)
    clf_add_test(qa_CLFBuiltinTools clf_tools clf_core clf_network ${CLF_OPENSSL_LIBS} ws2_32 crypt32)
else()
    clf_add_test(qa_CLFWebFetch     clf_tools ${CLF_OPENSSL_LIBS})
    clf_add_test(qa_CLFBuiltinTools clf_tools clf_core clf_network ${CLF_OPENSSL_LIBS})
endif()
```

---

> **自查补充（2026-09-11，pro 无需再查）**：
> - **变量可见性**：`CLF_BUILD_TESTS`（顶层 `option` → cache 变量）与 `CLF_OPENSSL_LIBS`（顶层 `set` 普通变量）在子目录内**均可直接读取**（CMake 父→子作用域继承）；`CMAKE_BINARY_DIR`（构建根）子目录可见 ✓
> - **引号约定**：`WORKING_DIRECTORY` 用引号包裹（防构建路径含空格）；`${CLF_OPENSSL_LIBS}` **不加**引号（须按列表展开为多个库参数）
> - **`function` 而非 `macro`**：`add_executable` / `add_test` / `set_tests_properties` 作用于**当前目录**，不受函数作用域影响，语义与现状一致 ✓
> - **`add_executable(${name} ${name}.cpp)`** 的相对路径基于 `CMAKE_CURRENT_SOURCE_DIR`（= `src/test/`）解析 ✓
> - **boost-ut 头可见性**：顶层 `include_directories(SYSTEM .../boost-ut)`（:28）对子目录自动继承，无需在 test/CMakeLists.txt 重复 ✓

---

## 六、逐行等价对照表（31 个测试，链接关系逐条核对）

| # | target | 原位置（src/CMakeLists.txt） | 迁移后链接参数（与原文完全一致） |
|---|---|---|---|
| 1 | qa_CLFContext | :127-129 | `clf_core clf_ui` |
| 2 | qa_CLFProtocolAdapter | :131-133 | `clf_core clf_ui` |
| 3 | qa_CLFSecurityPolicy | :135-137 | `clf_core clf_ui` |
| 4 | qa_CLFSessionManager | :139-141 | `clf_core clf_ui` |
| 5 | qa_CLFAgentLoop | :143-145 | `clf_core clf_network clf_ui ${CLF_OPENSSL_LIBS}` |
| 6 | qa_CLFStreamAccumulator | :147-149 | `clf_core clf_ui` |
| 7 | qa_CLFHeadTail | :151-153 | `clf_core clf_ui` |
| 8 | qa_CLFSearchContent | :155-157 | `clf_tools` |
| 9 | qa_CLFToolExecutor | :159-161 | `clf_core` |
| 10 | qa_CLFRetryPolicy | :163-165 | `clf_core` |
| 11 | qa_CLFFileOps | :167-169 | `clf_capabilities` |
| 12 | qa_CLFBuiltinTools | :173-179 | `clf_tools clf_core clf_network ${CLF_OPENSSL_LIBS}` + WIN32 `ws2_32 crypt32` |
| 13 | qa_CLFWebFetch | :181-186 | `clf_tools ${CLF_OPENSSL_LIBS}` + WIN32 `ws2_32 crypt32` |
| 14 | qa_CLFMessageCodec | :188-190 | `clf_core` |
| 15 | qa_CLFPeriodicTimer | :192-194 | `clf_types` |
| 16 | qa_CLFPasteCoalescer | :196-198 | `clf_core clf_ui` |
| 17 | qa_CLFSelectionModel | :200-202 | `clf_core clf_ui` |
| 18 | qa_CLFInputRender | :204-206 | `clf_core clf_ui` |
| 19 | qa_CLFPasserby | :208-210 | `clf_core` |
| 20 | qa_CLFTodoPanel | :213-215 | `clf_core clf_network clf_ui ${CLF_OPENSSL_LIBS}` |
| 21 | qa_CLFTipsBar | :218-220 | `clf_core clf_ui` |
| 22 | qa_CLFCapabilities | :223-225 | `clf_core clf_capabilities` |
| 23 | qa_CLFTodoStore | :228-230 | `clf_core` |
| 24 | qa_CLFSessionFileCtx | :232-234 | `clf_core` |
| 25 | qa_CLFSummaryCache | :236-238 | `clf_core` |
| 26 | qa_CLFContextWindow | :241-243 | `clf_core` |
| 27 | qa_CLFSystemComponents | :246-248 | `clf_core` |
| 28 | qa_CLFConfigLoader | :251-253 | `clf_core` |
| 29 | qa_CLFSessionUsage | :256-258 | `clf_types` |
| 30 | qa_CLFTextUtil | :261-263 | `clf_types` |
| 31 | qa_CLFAnsiParser | :266-268 | `clf_ui` |

> 合计 31 个，与 `ctest -N` 现状（Total Tests: 31）一致。

---

## 七、验证清单（pro 按序执行，全部通过才算完成）

```powershell
# 1) 重新 configure（CLion 用户：Reload CMake Project 等效）
cmake -S . -B cmake-build-debug

# 2) 测试清单应与现状一致（31 个、名字不变）
ctest --test-dir cmake-build-debug -N            # 期望：Total Tests: 31

# 3) 全量跑（基线：迁移前实测 31/31 passed，6.53s）
ctest --test-dir cmake-build-debug --output-on-failure -j6 --timeout 90

# 4) 抽查工作目录未变（应显示 .../cmake-build-debug/src）
ctest --test-dir cmake-build-debug -R qa_CLFTextUtil -V 2>&1 | Select-String "Test project|Working Directory|Working directory"

# 5) 开关验证：OFF 时无测试 target、主程序可构建
cmake -S . -B cmake-build-off -G Ninja -DCLF_BUILD_TESTS=OFF
cmake --build cmake-build-off --target CLFCode -j6
cmake --build cmake-build-off --target help | Select-String "qa_"   # 期望：无输出

# 6) 主程序冒烟
.\bin\Debug\CLFCode.exe --version                # 期望 exit=0
```

CLion 侧：Reload 后 31 个 qa 运行配置应仍在（target 名未变）；若 CLion 缓存了旧 target 列表，Reload 即刷新。

---

## 八、风险与回退

| 项 | 评估 |
|---|---|
| 行为差异 | **唯一差异点 = ctest 默认工作目录**（注册位置变化引起）→ 已用 `set_tests_properties(... WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/src)` 显式固定回原值，差异为零 |
| 编译环境 | 测试 target 仍在 C++20（子目录 `CMAKE_CXX_STANDARD`）；`/W4 /utf-8` 等经目录继承不变 |
| include 继承 | 测试所需头路径由所链库的 `PUBLIC include`（`src` 目录）+ 顶层 `3rdparty` SYSTEM include 提供，均不变 |
| 发布链路 | `release.ps1` 用 `--target CLFCode`，不受影响 |
| 回退 | 纯构建文件改动：`git checkout CMakeLists.txt src/CMakeLists.txt && rm src/test/CMakeLists.txt` 即完全回退 |

---

## 九、可选后续（不属本次范围，不要顺手做）

1. 阶段 2 插件测试（`clf_plugin_teststub` / `qa_CLFPluginManager` / 变体 DLL，见 `设计-阶段2-2.1-插件ABI与管理器骨架.md` §4.2）届时归本文件管理（可 `add_subdirectory(plugins)` 或直接在此定义 SHARED target）
2. `CLAUDE.md` 的构建说明可选同步（非必需——现有命令 `ctest --test-dir build ...` 语义不变）
3. 将来可把 `clf_add_test` 扩展支持 `WIN32_LIBS` 关键字（当前仅 2 处特例，`if(WIN32)` 分支已足够清晰）
