# 设计-FTXUI事件队列竞态修复

> **状态**：已实施（2026-08-18，Debug/Release 构建通过 + 现场验证通过）
> **创建**：2026-08-18
> **实施**：2026-08-18
> **关联**：笔记本安装版输入必崩（08-18 现场 4 次复现）；08-14"流式随机崩溃"、08-11"首次运行崩溃"为同一 bug 家族
> **依据**：08-18 现场取证（监控时间线 + 退出码 3 + stderr 空 + 零 WER 记录）+ FTXUI v7 源码审查

---

## 一、Context（背景）

### 问题描述

CLFCode 交互模式下，**任意触发 LLM 流式请求的回合可能静默退出**：进程消失、无报错、无 WER 事件、无崩溃转储，日志在 `[API] streaming request` 后戛然而止。已确认与工作区无关（ZKRF / E:\deepseek-harness / 安装目录均复现），与工具链无关（非交互模式同一提示词完整跑通），与发布包无关（桌面同包正常）。

### 崩溃特征（08-18 现场取证）

| 特征 | 详情 |
|------|------|
| 触发条件 | 交互式 + 流式回合（工具回合在 iter≥2，简单回合可能 iter=0） |
| 崩溃形式 | **静默退出**：无 [Fatal]、无弹窗、无 WER Application Error、无转储 |
| **退出码** | **3**（`"EXIT=$LASTEXITCODE"` 实测，18:07 复现） |
| stderr 捕获 | **0 字节**（`2> err.txt` 实测） |
| 日志截断 | `CLFLogger` 每行 flush，截断真实；崩于 `[API] streaming request` 之后、`streaming done` 之前 |
| 复现率 | 非确定性（同提示词 13:48 成功、18:07 崩溃）——微秒级竞态窗口 |

### 根因分析

**核心机制：FTXUI v7 事件缓冲 `MultiReceiverBuffer`（event_buffer）无任何同步，CLFCode 多线程并发访问导致数据竞争（UB）→ 内存破坏。**

FTXUI v7 内部两个队列的线程安全现状（`3rdparty/ftxui/src/ftxui/component/`）：

| 队列 | 用途 | 线程安全 |
|------|------|----------|
| `TaskQueue`（`task_queue.hpp`） | `App::Post()` 的任务队列 | ✅ 有 `std::mutex` |
| `MultiReceiverBuffer`（`multi_receiver_buffer.hpp`） | `App::PostEvent()` 的事件缓冲 | ❌ **完全无锁** |

`MultiReceiverBuffer` 关键实现（无锁）：
```cpp
void Push(T value) { values_.push_back(std::move(value)); next_index_++; }
// Pop()/Get()/Prune()/RemoveReceiver()/Has() 同样无锁
```

**线程全景核查**（`grep std::thread src/` + FTXUI 源码）：

| 线程 | 位置 | 是否调 `PostEvent`/`requestRefresh` | 异常兜底 |
|------|------|------|------|
| 提交线程 | `CLFAsyncSubmit.cpp:15` | ✅（emitContent → requestRefresh） | ✅ 已有 catch（08-11 L1） |
| **turnTimer** | `CLFAgentLoop.cpp:83` | ✅ `requestRefresh()` **1Hz 整个回合** | ❌ → **B1** |
| thinkingTimer | `CLFAgentLoop.cpp:115` | 否（仅 atomic fetch_add） | 无法抛异常，无需 |
| 思考指示器 | `CLFThinkingIndicator.cpp:15` | ✅ 结束时 `setStatus("")` → refresh | ❌ → **B2** |
| 粘贴定时器 | `CLFPasteCoalescer.cpp:11` | ✅ `wakeCb` → `PostEvent(Custom)` | ❌ → **B3** |
| m_escTimer | `CLFRepl.hpp:55` | — | **死字段**：仅声明+join，从未启动（Alt+Enter 已废弃），无线程 |
| FTXUI TaskRunner | `task_runner.cpp` | — | **不自己起线程**（任务在 main loop RunUntilIdle 执行），无隐患 |

**并发冲突**：上述线程的 `Push`（写 `values_`/`next_index_`）与 UI 主循环每帧的 `Pop`/`Prune`/`Get`（读改写同一 `std::deque`）**无锁并发** → UB → 两种崩溃形态：

1. **内存破坏 → UI 线程访问违例（0xc0000005）**：WER 归档中 08-12 及更早 **36 条 CLFCode 硬崩溃**（故障偏移集中在 0x5D000-0x5E000，exe 内 deque/STL 代码区域）——疑同一竞态的另一形态（待修复后观察确认）
2. **内存破坏 → turnTimer 线程 `push_back` 抛异常**：线程体无 try/catch → `std::terminate` → `abort()` → **退出码 3**，无 WER、无 stderr、无日志——08-18 现场形态

**异常逃逸路径**：
```
std::thread (turnTimer lambda)            ← 无 catch → std::terminate()
  └─ m_output->requestRefresh()
       └─ screen.PostEvent(Event::Custom)
            └─ event_buffer.Push()        ← deque 已破坏 → push_back 抛异常
```

**历史对照（同一 latent bug 家族）**：
- 08-11"首次运行崩溃"（静默退出）→ 当时 L1 兜底只保护 async submit 线程，未覆盖定时器线程 → 未根治
- 08-14 19:16"流式随机崩溃"（iter=1 流式，非交互正常）→ 同签名
- 08-18"笔记本必崩"（本报告全部取证）→ 同签名，退出码 3 实测坐实

### 次要竞态（顺带确认，可选加固）

`App::RequestAnimationFrame()`（`app.cpp`）中 `previous_animation_time_`（`steady_clock::time_point`）被多线程读写（worker 线程 PostEvent 与 UI 线程 Draw 并发），**非原子**——数据竞争但影响轻微（torn read 只影响动画节流判断，不崩溃）。纳入 A+ 可选加固（见下）。

---

## 二、修复方案

三层，按根治 → 防御 → 可观测排列：

| 层级 | 修复 | 类型 | 文件 |
|------|------|------|------|
| A | `MultiReceiverBuffer` 加锁 | **根治**（消除数据竞争） | `3rdparty/ftxui/src/ftxui/component/multi_receiver_buffer.hpp` |
| A+ | `RequestAnimationFrame` 时间戳原子化 | 加固（消除次要竞态） | `3rdparty/ftxui/src/ftxui/component/app.cpp` |
| B | 定时器线程体异常兜底 | 防御（防未来同类 terminate） | `CLFAgentLoop.cpp` / `CLFThinkingIndicator.cpp` / `CLFPasteCoalescer.cpp` |
| C | `std::set_terminate` 全局兜底 | 可观测（任何 terminate 留痕） | `main.cpp` |

### A：MultiReceiverBuffer 加锁（根治）

**修改文件**：`3rdparty/ftxui/src/ftxui/component/multi_receiver_buffer.hpp`

**锁选型：`std::recursive_mutex`（主选）**。理由：`Pop()` → `Get()`/`Prune()` 存在重入调用；recursive 锁可**逐方法直接加锁**（改动最小、最不易出错），低争用队列下性能可忽略。备选：非递归 mutex + 拆分加锁/未加锁辅助（代码更"纯"但改动面大，易错）。

**修改内容**（12 处加锁 + 2 行声明）：

```cpp
#include <mutex>                                  // 新增

template <typename T>
class MultiReceiverBuffer {
 public:
  class Receiver {
   public:
    explicit Receiver(MultiReceiverBuffer* buffer)
        : buffer_(buffer), index_(buffer->next_index_) {
      std::lock_guard<std::recursive_mutex> lock(buffer_->mutex_);  // 加锁
      buffer_->receivers_.push_back(this);
    }
    Receiver(MultiReceiverBuffer* buffer, size_t index)
        : buffer_(buffer), index_(index) {
      std::lock_guard<std::recursive_mutex> lock(buffer_->mutex_);  // 加锁
      buffer_->receivers_.push_back(this);
    }
    ~Receiver() {
      if (buffer_) { buffer_->RemoveReceiver(this); }   // RemoveReceiver 内加锁
    }
    Receiver(Receiver&& other) noexcept
        : buffer_(other.buffer_), index_(other.index_) {
      other.buffer_ = nullptr;
      if (buffer_) {
        std::lock_guard<std::recursive_mutex> lock(buffer_->mutex_);  // 加锁
        std::replace(buffer_->receivers_.begin(), buffer_->receivers_.end(),
                     &other, this);
      }
    }
    Receiver& operator=(Receiver&& other) noexcept {
      if (this != &other) {
        if (buffer_) { buffer_->RemoveReceiver(this); }
        buffer_ = other.buffer_;
        index_ = other.index_;
        other.buffer_ = nullptr;
        if (buffer_) {
          std::lock_guard<std::recursive_mutex> lock(buffer_->mutex_);  // 加锁
          std::replace(buffer_->receivers_.begin(), buffer_->receivers_.end(),
                       &other, this);
        }
      }
      return *this;
    }
    bool Has() const {
      std::lock_guard<std::recursive_mutex> lock(buffer_->mutex_);  // 加锁（buffer_ 空时先短路）
      return buffer_ && index_ < buffer_->next_index_;
    }
    T Pop() {
      std::lock_guard<std::recursive_mutex> lock(buffer_->mutex_);  // 加锁
      if (!Has()) return {};
      T value = buffer_->Get(index_);
      index_++;
      buffer_->Prune();
      return value;
    }
    size_t index() const { return index_; }
   private:
    MultiReceiverBuffer* buffer_;
    size_t index_;
  };

  std::unique_ptr<Receiver> CreateReceiver() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);  // 加锁
    return std::make_unique<Receiver>(this);
  }
  std::unique_ptr<Receiver> CreateReceiverAt(size_t index) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);  // 加锁
    return std::make_unique<Receiver>(this, index);
  }
  void Push(T value) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);  // 加锁
    values_.push_back(std::move(value));
    next_index_++;
  }

 private:
  void RemoveReceiver(Receiver* receiver) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);  // 加锁
    receivers_.erase(std::remove(...), receivers_.end());
    Prune();
  }
  void Prune() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);  // 加锁
    /* 原实现不动 */
  }
  T Get(size_t index) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);  // 加锁
    return values_[index - start_index_];
  }
  mutable std::recursive_mutex mutex_;                  // 新增

  std::deque<T> values_;
  std::vector<Receiver*> receivers_;
  size_t start_index_ = 0;
  size_t next_index_ = 0;
};
```

**注意点**：
- `Has()` 先判 `buffer_` 空再取锁（move 后 buffer_ 可能为 nullptr）
- 锁全部在方法入口，recursive 锁天然防重入死锁（Pop→Get/Prune 双锁安全）
- **补丁头注释**（类上方）：
  `// CLFCode patch: MultiReceiverBuffer was not thread-safe (PostEvent from worker threads vs Pop in UI loop) — added recursive_mutex. Re-check when upgrading FTXUI.`
- 不改任何公共 API 签名、不改行为；性能影响可忽略（低争用）

### A+：RequestAnimationFrame 时间戳原子化（加固，可选）

**修改文件**：`3rdparty/ftxui/src/ftxui/component/app.cpp`

**修改内容**：`Internal::previous_animation_time_` 改为原子读（写入点仅两处——`PreMain` 初始化 + `RequestAnimationFrame`）：
```cpp
// Internal 声明
std::atomic<animation::TimePoint> previous_animation_time_;
// RequestAnimationFrame 内
auto prev = internal_->previous_animation_time_.load();
if (now - prev >= time_histeresis) internal_->previous_animation_time_.store(now);
// PreMain 内
internal_->previous_animation_time_.store(animation::Clock::now());
```
（`animation::TimePoint` 需确认可原子化——若为 `steady_clock::time_point` 的 typedef，64 位整型可；若不可，用 mutex 保护该字段，或跳过 A+。）

### B：定时器线程异常兜底（防御）

**B1 `src/CLFCore/CLFAgentLoop.cpp`**（turnTimer，行 83-96，lambda 整体包 try/catch）：
```cpp
std::thread turnTimer([&]() {
    try {
        while (turnTimerOn.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (!turnTimerOn.load(std::memory_order_relaxed)) break;
            if (!m_output) continue;
            auto s = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - turnStart).count();
            if (s >= 15)
                m_output->setStatusTextOnly(m_labels.working + " for "
                    + std::to_string(static_cast<int>(s)) + "s…");
            m_output->requestRefresh();
        }
    } catch (const std::exception& e) {
        CLFLogger::instance().error(
            std::string("[TurnTimer] exception: ") + e.what());
    } catch (...) {
        CLFLogger::instance().error("[TurnTimer] unknown exception");
    }
});
```
（`CLFAgentLoop.cpp` 已 include `CLFLogger.hpp`，文件在 `CLF::CLFCore` 命名空间内，直接用 `CLFLogger`。）

**B2 `src/CLFNetwork/CLFThinkingIndicator.cpp`**（线程尾的 `setStatus("")`）：
```cpp
std::thread([this]() {
    while (!m_done.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    if (m_output) {
        try { m_output->setStatus(""); }
        catch (const std::exception& e) {
            std::cerr << "[ThinkingIndicator] exception: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[ThinkingIndicator] unknown exception" << std::endl;
        }
    }
});
```
**⚠ 分层约束**：`clf_network` 只依赖 `clf_types`（CMake 链接序），**不得 include `CLFCore/CLFLogger.hpp`**（会形成 clf_network→clf_core 反向依赖，破坏依赖方向 `clf_core → clf_network`）。因此 B2 用 `std::cerr`（文件已 include `<iostream>`），不用 CLFLogger。

**B3 `src/CLFUI/CLFPasteCoalescer.cpp`**（定时线程 wakeCb 调用点）：
```cpp
m_pendingActive = false;
m_pendingConfirmed.store(true);
auto cb = m_wakeCb;
lock.unlock();
try {                                            // ← 新增
    if (cb) cb();
} catch (const std::exception& e) {
    CLF::CLFCore::CLFLogger::instance().error(
        std::string("[PasteTimer] wakeCb exception: ") + e.what());
} catch (...) {
    CLF::CLFCore::CLFLogger::instance().error("[PasteTimer] wakeCb unknown exception");
}
lock.lock();                                     // ← 必须重新加锁（异常也不能跳过）
```
**⚠ 关键**：`cb()` 前已 `lock.unlock()`，catch 后**必须** `lock.lock()` 恢复，否则后续 `m_cv.notify_one()`（析构时）在未持锁状态下调用 = UB/死锁。需新增 `#include "CLFCore/CLFLogger.hpp"`（`clf_ui` 依赖 `clf_core`，允许）。

### C：全局 terminate 兜底（可观测）

**修改文件**：`src/main.cpp`（新增 `#include <exception>`；`CLFLogger.hpp` 已 include）

**修改内容**：main 起始处（`SetConsoleCP/SetConsoleOutputCP` 之后）：
```cpp
// 全局兜底：任何线程未处理异常 → 留痕后按原语义终止（保持退出码 3 可辨识）
std::set_terminate([]() {
    try {
        CLF::CLFCore::CLFLogger::instance().error(
            "[Terminate] unhandled exception in thread — process aborting");
    } catch (...) {}
    std::abort();
});
```
**要点**：handler 保持极简（运行在异常线程上）；`CLFLogger` 每行 flush，abort 前日志已落盘；`std::abort()` 保留原退出码 3 语义（且 FTXUI 的 SIGABRT 处理器会先恢复终端再转默认处理）。

---

## 三、实施顺序与构建

1. **A** → `multi_receiver_buffer.hpp`（锁）
2. **A+**（可选）→ `app.cpp`（时间戳原子化，先确认 `animation::TimePoint` 可原子化）
3. **B1** → `CLFAgentLoop.cpp`；**B2** → `CLFThinkingIndicator.cpp`；**B3** → `CLFPasteCoalescer.cpp`
4. **C** → `main.cpp`
5. 构建：`cmake --build build -j6`（Debug）→ `ctest --test-dir build --output-on-failure -j6`
6. 交互验证：`bin/Debug/CLFCode.exe` 从 `E:\deepseek-harness` 启动，重复工具型提示词 ×N 轮 + 10 分钟压力观察

## 四、验证方案

1. **回归**：ctest 基线（12/13，SessionManager 既有环境失败除外）
2. **现场复现验证**：`E:\deepseek-harness` 交互运行，重复"查看一下这个项目的整体信息"类提示词 → **不再静默退出**
3. **压力测试**：连续多轮触发工具的回合（每轮 >10s 流式），10 分钟稳定性；`CLF_DEBUG_EVENTS=1` 事件日志正常
4. **退出码回归**：若再异常退出，`EXIT=` 应为 1（[Fatal]）而非 3（terminate），且日志有 `[TurnTimer]`/`[Terminate]` 记录
5. **历史对照**：修复后观察，WER 归档不再新增 0xc0000005 硬崩溃（验证 08-12 家族同根因假设）

## 五、风险与缓解

| 风险 | 概率 | 影响 | 缓解 |
|------|------|------|------|
| 改 3rdparty 影响 FTXUI 其他用法 | 低 | 中 | 仅加锁不改语义；补丁头注释标记，升级时复查 |
| recursive_mutex 重入/死锁 | 极低 | 高 | recursive 锁天然防重入；B3 的 catch 后强制重新加锁 |
| `animation::TimePoint` 不可原子化（A+） | 中 | 低 | 跳过 A+ 或改 mutex 保护；A+ 为可选加固 |
| B/C 掩盖真实 bug | 低 | 低 | 异常全部留痕（`[TurnTimer]`/`[PasteTimer]`/`[Terminate]`），不会无声消失 |
| 08-12 AV 家族并非同一根因 | 中 | 低 | A 实施后继续观察 WER；若仍现 AV 再单独排查 |
| 锁引入性能开销 | 极低 | 低 | 低争用队列，每操作一次递归锁，微秒级 |

## 六、顺带发现（不在本次范围）

- **search_content 无扩展名过滤**：模型无 `fileTypes` 参数时扫描 `.dll/.exe` 等二进制，GBK 内容抛 `invalid UTF-8 byte`（08-18 日志可见）。建议后续加默认文本扩展名过滤 + 跳过 `bin/lib/cmake-build-*` 目录（独立小任务）
- **测试建议**（可选）：可新增一个队列并发压力单测（N 线程 Push × 主线程 Pop，验证加锁后无崩溃）——竞态类测试非确定性，定位为压力而非回归

---

## 七、实施记录（2026-08-18）

### 改动清单（6 文件）

| 文件 | 改动 |
|------|------|
| `3rdparty/ftxui/src/ftxui/component/multi_receiver_buffer.hpp` | A：全部成员方法加 `std::recursive_mutex`（12 处锁 + 声明），类头补丁注释；`Receiver` 首构造器 `next_index_` 读取移入锁内 |
| `3rdparty/ftxui/src/ftxui/component/app.cpp` | A+：`previous_animation_time_` 改 `std::atomic<TimePoint>`（4 处 load/store） |
| `src/CLFCore/CLFAgentLoop.cpp` | B1：turnTimer lambda 整体包 try/catch，异常记 `[TurnTimer]` |
| `src/CLFNetwork/CLFThinkingIndicator.cpp` | B2：线程尾 `setStatus("")` 包 try/catch，用 `std::cerr`（clf_network 不依赖 clf_core 的分层约束） |
| `src/CLFUI/CLFPasteCoalescer.cpp` | B3：wakeCb 调用包 try/catch（catch 后强制 `lock.lock()` 恢复）+ `#include CLFLogger.hpp`，异常记 `[PasteTimer]` |
| `src/main.cpp` | C：`std::set_terminate` 全局兜底（cerr 先行 + CLFLogger + `std::abort()`）+ `#include <exception>` |

### 构建与测试

- 构建目录：**cmake-build-debug / cmake-build-release（MSVC，需先导入 vcvars64 环境）**；`build/` 目录为废弃的 MinGW 配置（multichar 测试文件从未编译通过，勿用）
- Debug / Release 构建均通过（27 目标）；ctest 均 **11/12**（唯一失败 `qa_CLFSessionManager` 为既有环境失败，与改动无关）

### 现场验证（08-18 18:30-18:36，E:\deepseek-harness）

- 修复前必崩场景：工具回合 iter≥2 流式时静默退出（退出码 3）
- 修复后实测：**11 轮工具迭代完整跑完**（list_directory / execute_command ×9 / read_file ×2，ctx 40 条消息 88KB），`[Turn] done` → `[Submit] exit` 正常收尾
- B/C 防御层零触发（无 `[TurnTimer]`/`[Terminate]` 记录）——A 根治生效
- 无 WER、无转储、无静默退出

### 遗留

- 08-12 AV 家族（0xc0000005）是否为同一竞态：**待观察**（修复后 WER 归档若不再新增即坐实）
- `build/` MinGW 目录的多字符字面量问题（`qa_CLFContext.cpp:21`）未处理（废弃配置，不影响）
- 安装版（`C:\Users\wjhwh\CLFCode`）尚未更新——需重新发布或手动替换 exe
