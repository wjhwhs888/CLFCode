# 设计：文件修改 Diff 渲染

> 状态：设计中 | 创建：2026-08-10 | 修订 v2：2026-08-10（评审修订 + 确认流程重构）

## 〇、设计目标

`write_file` / `edit_file` 执行时，以 `+`/`-` 行格式展示具体改动。**diff 的展示时机取决于安全模式**：

| 安全模式 | 写操作行为 | diff 展示 |
|----------|-----------|----------|
| **Auto** | 自动通过 | 事前显示（自动放行） |
| **Analyze** | 阻断 | 不展示 |
| **Edit / Manual** | 需确认 | 事前显示 → y/n 确认后落盘 |

| 操作 | 现状 | 目标 |
|------|------|------|
| write_file | 仅显示 "✓ write_file(path) — N lines, M chars" | Edit/Manual 模式先展示 diff 再确认 → 写入 → 事后摘要；Auto 模式直接写入 → 事后 diff |
| edit_file | **未实现** | 同上 |

---

## 一、现状分析

（同 v1，略）

---

## 二、设计方案

### 2.1 核心流程（预览先行，统一路径）

**Auto 和 Edit/Manual 共享同一套前置流程**——差异仅在第 4 步是否有 y/n。

```
CLFToolExecutor::execute() 收到 write_file / edit_file 调用：

  Step 1 — prepareWritePreview()
     ├─ 解析 args + 读旧文件 → 记录 FileSnapshot {content, mtime, size}
     │    ├─ 文件存在 → 正常读取
     │    └─ 文件不存在 → oldSnapshot.content=""，size=0，mtime=0，valid=true
     │       （新文件：diff 全为 + 行）
     ├─ 超限检查
     ├─ 计算 diff（超限时 truncated=true）
     ├─ 对 edit_file：在内存中模拟 oldStr→newStr
     │    ├─ 匹配成功 → 继续
     │    └─ 匹配失败（0次/多次）→ valid=false，错误信息写入 preview
     └─ 返回 WritePreview {valid, ...}

  Step 1.5 — 若 !valid → renderErrorAnsi(errorMsg) → emitContent → return
     （edit_file 匹配失败在此处暴露，用户/Agent 立即看到诊断信息）

  Step 2 — 超限阻断检查（仅 Edit/Manual 模式）
     └─ 若 truncated==true → 直接报错阻断
        "File too large to preview diff. Use Auto mode or set force=true."

  Step 3 — renderDiffAnsi → emitContent（事前预览）

  Step 4 — 模式分流
     ├─ Auto ───────────────────── 自动放行
     └─ Edit/Manual ────────────── confirm("批准？")
           ├─ 拒绝 → return denied，不落盘
           └─ 批准 → 继续

  Step 5 — TOCTOU 乐观锁校验
     └─ 当前 mtime/size vs snapshot.mtime/size
        mtime 或 size 任一变化 → 报错 "File modified after preview. Please review again."
        通过 → 继续
        （size 辅助覆盖 FAT 文件系统 mtime 精度不足的边界情况）

  Step 6 — 执行 handler（实际写入，复用 WritePreview.newContent）
     └─ handleWriteWithPreview(preview) → 原子写入落盘

  Step 7 — 显示结果 ✓ 摘要（复用 preview.diffStats，不重复计算）
```

**关键原则**：
- diff 永远在落盘前计算，全模式统一
- 超限 + Edit/Manual = 阻断，不强迫盲批
- handler 复用预览阶段准备好的 `newContent`，避免重复读文件 + 重复替换计算
- 落盘前 mtime 校验，防止 TOCTOU 竞态

### 2.2 新增组件

#### 2.2.1 `CLFDiff` — 行级 diff（纯数据，无渲染，无文件 IO）

新增文件 `src/CLFTools/CLFDiff.hpp` / `.cpp`。**只做算法**，不碰文件和终端。

```cpp
// CLFDiff.hpp
namespace CLF::CLFTools {

enum class CLFDiffOp { Keep, Add, Remove };

struct CLFDiffLine {
    CLFDiffOp op;
    int oldLineNo = 0;
    int newLineNo = 0;
    std::string text;
};

struct CLFDiffStats {
    int added   = 0;
    int removed = 0;
    int hunks   = 0;
    bool truncated = false;
    std::string truncReason;
};

std::vector<CLFDiffLine> computeDiff(const std::string& oldText,
                                     const std::string& newText,
                                     CLFDiffStats& stats,
                                     int contextLines = 5);

std::string normalizeLineEndings(const std::string& text);

} // namespace CLF::CLFTools
```

**超限策略**：

| 条件 | 行为 |
|------|------|
| 行数 > 3000 | 跳过 diff，truncated=true |
| 字节 > 500KB | 跳过 diff，truncated=true |
| 渲染行 > 200 | 折叠超出的 hunk |

#### 2.2.2 原子写入（writeFile + editFile 共用，CLFFileOps 改造）

写入不可分割——要么原文件完整，要么新文件完整，不存在半写状态。

```
原子写入路径（writeFile & editFile 统一走）：
  1. 在同目录创建 path + ".clf_tmp" 临时文件
  2. 写入完整内容（原始 content，不做换行符转换）
  3. flush + close
  4. 原子替换：
       Windows: MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING)
       Linux:   rename(tmp, path)   // 同文件系统下原子操作
  5. 替换失败 → 自动删除临时文件，原文件不受影响
```

> **Windows 关键坑**：C 标准库 `rename()` 在 Windows 下不允许覆盖已存在文件（返回错误）。必须用 `MoveFileExW` + `MOVEFILE_REPLACE_EXISTING`，行为等价于 POSIX `rename` 的原子替换。
>
> **Linux EXDEV 降级**：临时文件与目标文件不在同一挂载点时 `rename` 返回 EXDEV（跨设备错误）。降级策略：`CopyFile + unlink(tmp)`，此时非原子但能完成写入，内部记录警告日志。

#### 2.2.3 `editFile` — Edit/Replace 工具

```cpp
// 精确字符串替换：找到 oldStr → 替换为 newStr（唯一匹配）
// 内部走原子写入路径（临时文件 + MoveFileEx/rename）
CLFFileResult editFile(const std::string& path,
                       const std::string& oldStr,
                       const std::string& newStr);
```

- 0 次匹配 → 报错 + 提示用 read_file 确认准确内容
- >1 次匹配 → 报错 + 提示加更多上下文使其唯一

#### 2.2.4 `normalizeLineEndings` 实现约束（关键）

**diff 计算的换行符标准化绝不可修改原始写入内容**。

```cpp
// computeDiff 内部 normalize 仅作用于深拷贝副本
// 外部传入的 content 不受影响，writeFile/editFile 写入原始字符串
std::vector<CLFDiffLine> computeDiff(const std::string& oldText,
                                     const std::string& newText,
                                     CLFDiffStats& stats,
                                     int contextLines = 5,
                                     bool normalizeEOL = true);
```

- `normalizeEOL=true` 时：对 oldText / newText 的**深拷贝**做 `\r\n → \n`，仅用于 diff 比较
- 写入磁盘的永远是 Agent 传入的原始 content（未经换行符篡改）

### 2.3 CLFToolExecutor 改造（核心变更）

#### 2.3.1 新增 diff 预览函数

```cpp
// CLFToolExecutor.cpp
struct FileSnapshot {
    std::string content;
    uint64_t mtime = 0;      // 文件最后修改时间
    uint64_t size = 0;       // 文件大小（辅助校验，应对 FAT 低精度 mtime）
};

// 从 tool args 中提取 path 和新内容，读取旧文件，计算 diff
// 对 edit_file：在内存中模拟 oldStr→newStr，产出 newContent
// 写入时 handler 直接取用 newContent，避免重复读文件 + 重复替换
struct WritePreview {
    bool valid = false;
    std::string filePath;
    FileSnapshot oldSnapshot;          // 旧文件快照（mtime + content）
    std::string newContent;            // 预先计算的新内容（handler 复用）
    std::vector<CLFDiffLine> diffLines;
    CLFDiffStats diffStats;
};
WritePreview prepareWritePreview(const CLFToolCall& call);
```

- write_file：从 args 提取 `path` + `content` → 读旧文件 + 记录 mtime → computeDiff → `newContent = content`
- edit_file：从 args 提取 `path` + `oldStr` + `newStr` → 读旧文件 + 记录 mtime → 在内存中模拟 oldStr→newStr → computeDiff(old, newContent)
- 若 edit_file 的 oldStr 匹配失败 → `valid=false`，返回错误信息（不落盘）

#### 2.3.2 execute() 流程改造

> 具体流程以 **2.1 核心流程 Step 1–7** 为准。此处仅说明与现有代码的差异点。

```
安全策略检查（现有逻辑不变）
  └─ 是 Write 工具：
       进入 2.1 统一流程（预览先行，Step 1–7）
       Auto / Edit/Manual 差异仅在 Step 4 是否有 confirm
  └─ 非 Write 工具：
       现有流程不变
```

#### 2.3.3 渲染函数（放在 CLFToolExecutor）

```cpp
std::string renderDiffAnsi(const std::vector<CLFDiffLine>& diff,
                           const CLFDiffStats& stats,
                           const std::string& filePath);
```

### 2.4 显示效果

**统一的事前 diff 预览**（Auto / Edit / Manual 模式通用）：

```
● write_file(src/foo.cpp)

  @@ -3,3 +3,4 @@
   3     #include <string>
   4  -  int main() {
    4 +  int main(int argc, char** argv) {
    5 +      // 新增注释
   5       return 0;
  ⎿  +2 -1 in 1 hunk
```

**Edit/Manual 模式** — 预览后追加确认栏：

```
  ⎿  以上改动是否批准？(y/n)
```

- y → 写入 → `✓ write_file(src/foo.cpp) — +2 -1 lines, 1 hunk`
- n → 不写入 → `✗ write_file(src/foo.cpp) — denied`

**Auto 模式** — 预览后自动放行：

```
  ✓ write_file(src/foo.cpp) — +2 -1 lines, 1 hunk
```

**超限情况**（truncated 时不输出 hunks 数，避免虚假统计）：

```
● write_file(src/large.cpp)
  ⎿  file too large (>3000 lines), diff skipped
  ✓ write_file(src/large.cpp) — 3120 lines, 98KB (diff truncated)
```

**多 hunk 折叠**（省略区间用 `@@ ... @@` 占位，行号保持连续）：

```
● edit_file(src/config.yaml)
  ⎿  +15 -12 in 4 hunks
  @@ -10,5 +10,6 @@
   10    port: 8080
  @@ ... @@
  @@ -45,3 +46,5 @@
   45    timeout: 30
  ... (8 lines omitted in 2 hunks)
  ✓ edit_file(src/config.yaml) — +15 -12 lines, 4 hunks
```

### 2.5 工具注册

在 `registerBuiltinTools()` 中新增 `edit_file`，risk=Write。

---

## 三、实施步骤

| 步骤 | 内容 | 涉及文件 |
|------|------|----------|
| **Step 1** | 实现 `CLFDiff`（LCS + normalizeLineEndings 深拷贝，纯算法） | 新增 `CLFDiff.hpp/.cpp` |
| **Step 2** | CLFFileOps 原子写入改造 + 实现 `editFile()` | `CLFFileOps.hpp/.cpp` |
| **Step 3** | 改造 `writeFileHandler`（返回 diff 数据） | `CLFBuiltinTools.cpp` |
| **Step 4** | 新增 `editFileHandler` + 注册 | `CLFBuiltinTools.cpp` |
| **Step 5** | `prepareWritePreview` + `renderDiffAnsi` + execute 流程改造 | `CLFToolExecutor.hpp/.cpp` |
| **Step 6** | CMakeLists 注册新文件 | `CMakeLists.txt` |
| **Step 7** | 构建 + 测试 | — |

---

## 四、风险与约束

| 约束 | 措施 |
|------|------|
| TOCTOU 竞态 | FileSnapshot{mtime, size}；双字段校验，覆盖 FAT 低精度 mtime 边界 |
| 截断盲批（Edit/Manual） | truncated + 需确认 → 直接阻断，提示切 Auto 或加 force |
| 原子写入 | 临时文件 + flush + MoveFileEx/rename；失败删除 tmp |
| Windows rename 陷阱 | 必须用 `MoveFileExW` + `MOVEFILE_REPLACE_EXISTING` |
| diff 必须在落盘前 | 预览先行，全模式统一；拒绝不调 handler |
| 换行符不篡改写入 | normalizeLineEndings 仅作用于深拷贝副本 |
| truncated 不输出虚假 hunks | 超限显示 "(diff truncated)" |
| 预览/写入重复计算 | WritePreview.newContent 由 handler 复用，避免二次读文件+替换 |
| 不引入第三方库 | LCS 自实现 ~80 行 |
| 大文件性能 | 硬截断 3000 行 / 500KB |
| 数据/渲染分层 | CLFDiff 纯数据；ANSI 渲染在 CLFToolExecutor |

---

## 五、后续迭代（v1.1）

| 条目 | 说明 |
|------|------|
| edit_file 模糊匹配 | `allow_fuzzy_match` 开关：strip + 忽略首尾空格，覆盖缩进差异场景 |
| UTF-8 BOM 处理 | 读取时检测并剥离 BOM，写入时原样加回 |

