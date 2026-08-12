# 测试-P0第一批：CLI参数、非交互模式、search_content

> 测试日期：2026-08-12
> 测试项目目录：`E:\wjh_work\Project\ProjectClion\testCLFCode`

---

## 准备

```powershell
cd E:\wjh_work\Project\ProjectClion\testCLFCode

# 准备测试文件
mkdir test_search -Force | Out-Null
@"
hello world
this is a test
hello again
goodbye
"@ | Out-File test_search\readme.txt -Encoding UTF8

@"
int main() {
    return 0;
}
"@ | Out-File test_search\main.cpp -Encoding UTF8

# 准备 prompt 文件
"查看当前目录下有哪些文件" | Out-File test_prompt.txt -Encoding UTF8
```

---

## 测试 1：--help

```powershell
CLFCode --help
```

**预期**：输出参数列表，包含 `--help` / `--version` / `--config` / `--project-root` / `--prompt` / `--prompt-file` / `--allow-write`。**不进入 REPL**，直接退回到终端提示符。

---

## 测试 2：--version

```powershell
CLFCode --version
```

**预期**：输出 `v0.1.6`（或当前版本号），**不进入 REPL**。

---

## 测试 3：-h 和 -v 短参数

```powershell
CLFCode -h
CLFCode -v
```

**预期**：同上，`-h` 等价 `--help`，`-v` 等价 `--version`。

---

## 测试 4：--prompt 非交互模式（只读）

```powershell
CLFCode --prompt "列出当前目录下的文件"
```

**预期**：
1. 不显示 FTXUI 终端 UI
2. 模型回应，列出文件（调用 `list_directory` 工具）
3. 最终结果输出到终端后自动退出
4. **不执行任何写操作**（安全策略 block 生效）

---

## 测试 5：--prompt 非交互模式 + 写操作被拒绝

```powershell
CLFCode --prompt "在 test_search 目录下创建一个 hello.txt 文件，内容为 hello"
```

**预期**：
1. 模型尝试调用 `write_file` 工具
2. 被安全策略拒绝（block 模式下 Write 工具不可用）
3. 输出拒绝信息，退出

---

## 测试 6：--prompt + --allow-write

```powershell
CLFCode --prompt "在 test_search 目录下创建一个 hello.txt 文件，内容为 hello" --allow-write
```

**预期**：
1. 安全策略为 auto
2. 模型成功创建文件
3. 验证：`Get-Content test_search\hello.txt` 输出 `hello`

```powershell
# 清理
Remove-Item test_search\hello.txt -Force -ErrorAction SilentlyContinue
```

---

## 测试 7：--prompt-file 从文件读取

```powershell
CLFCode --prompt-file test_prompt.txt
```

**预期**：读取 `test_prompt.txt` 的内容（"查看当前目录下有哪些文件"），非交互模式执行后退出。

---

## 测试 8：--prompt 和 --prompt-file 互斥

```powershell
CLFCode --prompt "hello" --prompt-file test_prompt.txt
```

**预期**：输出 `[ERROR] Cannot use --prompt and --prompt-file together`，不执行，退出。

---

## 测试 9：search_content 工具（交互模式）

```powershell
# 进入交互模式（正常启动，不带参数）
CLFCode
```

然后在 REPL 中输入：

```
搜索 test_search 目录下所有文件，找包含 "hello" 的行
```

**预期**：
1. 模型调用 `search_content` 工具
2. 参数：`pattern: "hello"`, `directory: "test_search"`, `fileTypes: ""`
3. 返回匹配行（`readme.txt:1: hello world` / `readme.txt:3: hello again`）
4. 不扫描 `.git` 等忽略目录

---

## 测试 10：search_content 扩展名过滤

```
搜索 test_search 目录下所有 .cpp 文件，找包含 "main" 的行
```

**预期**：
1. 模型调用 `search_content`，参数 `fileTypes: ".cpp"`
2. 仅返回 `main.cpp` 中的匹配结果
3. **不返回** `readme.txt` 中的内容

---

## 测试 11：未知参数

```powershell
CLFCode --unknown-flag
```

**预期**：输出 `[ERROR] Unknown option: --unknown-flag` + 提示 `Use --help`，退出。

---

## 测试 12：--prompt 缺值

```powershell
CLFCode --prompt
```

**预期**：输出 `[ERROR] --prompt requires a value`，退出。

---

## 测试 13：--prompt-file 文件不存在

```powershell
CLFCode --prompt-file nonexistent.txt
```

**预期**：输出 `[ERROR] Cannot open prompt file: nonexistent.txt`，退出。

---

## 清理

```powershell
Remove-Item test_search -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item test_prompt.txt -Force -ErrorAction SilentlyContinue
Remove-Item hello.txt -Force -ErrorAction SilentlyContinue
```

---

## 通过标准

| # | 测试项 | 通过条件 |
|:---:|------|------|
| 1 | --help | 输出参数列表后退出 |
| 2 | --version | 输出版本号后退出 |
| 3 | -h / -v | 等价于长参数 |
| 4 | --prompt 只读 | 非交互，仅 Read 工具可用 |
| 5 | --prompt 写被拒 | 拒绝 Write，不创建文件 |
| 6 | --prompt + --allow-write | 写操作成功 |
| 7 | --prompt-file | 从文件读取 prompt 执行 |
| 8 | 互斥检查 | 报错退出 |
| 9 | search_content | 返回匹配行 |
| 10 | fileTypes 过滤 | 仅匹配指定扩展名 |
| 11 | 未知参数 | 报错退出 |
| 12 | 缺值 | 报错退出 |
| 13 | 文件不存在 | 报错退出 |
