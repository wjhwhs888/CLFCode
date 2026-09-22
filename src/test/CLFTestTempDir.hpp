#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// CLFTestTempDir.hpp —— 统一 RAII 临时物设施（F1/F2/F3/F4，2026-09-22）
// 功能说明：
//   测试套件临时目录/文件的统一创建与清理设施——一个维护点取代各套件
//   自写的 TempDir/VSetup/makeTempDir/cleanupDir（此前 5+ 份各写各的，
//   ec 处处被忽略，曾累积 43 个空壳残留目录跨三周无人发现）。
//   析构清理自带退避重试（F2）；重试仍失败 → stderr 可见（F3）；
//   哨兵登记制在测试进程收尾时把残留变成 ctest 红灯（F4）。
// ⚠ 使用约束（必须遵守）：
//   本头文件必须 include 在 qa 文件的顶部——boost/ut.hpp 之后、
//   suite/test 对象定义之前。哨兵依赖同 TU 静态初始化顺序：
//   构造最早（先于任何测试）、析构最晚（后于全部测试）。
//   若 include 位置下移，哨兵析构将早于测试 → 检测静默失效。
// 线程约束：设施对象在测试主线程创建/析构；哨兵注册表不加锁
//   （boost::ut 测试单线程执行，线程用例只读共享状态）。
// example:
//   #include <boost/ut.hpp>
//   #include "CLFTestTempDir.hpp"
//   static ut::suite s = [] {
//     using namespace boost::ut;
//     s.test("case") = [] {
//       CLFTest::CLFTestTempDir dir("clf_qa_xxx_");
//       // dir 可隐式当 std::string / std::filesystem::path 使用
//       std::ofstream(dir.string() + "/a.txt") << "x";
//     };  // 离开作用域自动清理；失败会重试；仍失败 → 哨兵判红
//   };
// ─────────────────────────────────────────────────────────────────────────────

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace CLFTest {

// ── F4 哨兵：登记制 ──────────────────────────────────────────────────────────
// 设施对象构造时 track、清理成功时 untrack；析构时（全部测试已跑完）
// 仍存在的登记项 → stderr 打印 + std::exit(1)（ctest 红灯）。
// 机制与 boost::ut 自身 ~runner() 的 std::exit(-1) 同款（ut.hpp:2016-2030）。
class CLFTestTempSentry {
public:
    CLFTestTempSentry() = default;
    ~CLFTestTempSentry();

    void track(const std::filesystem::path& p) { m_tracked.push_back(p); }
    void untrack(const std::filesystem::path& p);

private:
    std::vector<std::filesystem::path> m_tracked;  // 单线程，无锁（见头注释）
};

// 每 TU 一份（static 内部链接）：include 在顶部 → 同 TU 构造最早、析构最晚
static CLFTestTempSentry clf_test_temp_sentry;

// ── F2/F3 核心：删除带退避重试，错误码不吞 ───────────────────────────────────
// remove_all 带 ec（不抛）；失败退避 1ms/5ms/20ms 重试（Windows delete-pending
// 有界，重试收敛）；耗尽仍失败 → stderr 打印路径 + ec.message()，返回 false。
// 对文件同样适用（remove_all 内部对文件走 remove），目录/文件统一入口。
inline bool RemoveWithRetry(const std::filesystem::path& p) {
    constexpr int kDelaysMs[] = {1, 5, 20};
    std::error_code ec;
    for (int attempt = 0; attempt <= 3; ++attempt) {
        ec.clear();
        std::filesystem::remove_all(p, ec);
        std::error_code probeEc;
        if (!std::filesystem::exists(p, probeEc) && !probeEc) return true;  // 幂等：已消失即成功
        if (attempt < 3) std::this_thread::sleep_for(std::chrono::milliseconds(kDelaysMs[attempt]));
    }
#ifdef _WIN32
    // %ls：宽字符路径，避免 CP936 乱码（项目 MSVC 编码教训族）
    std::fprintf(stderr, "[CLFTestTemp] 清理失败: %ls : %s\n", p.c_str(), ec.message().c_str());
#else
    std::fprintf(stderr, "[CLFTestTemp] 清理失败: %s : %s\n", p.u8string().c_str(), ec.message().c_str());
#endif
    return false;
}

// ── 命名辅助：前缀 + 时间戳 + 原子序号 ───────────────────────────────────────
// 序号取代各套件自带的 g_dirCounter——单点防撞名（boost::ut 静态期 + 快速
// 连续运行下时间戳可能相同，序号兜底）
inline std::filesystem::path makeTempPath(const std::string& prefix,
                                          const std::filesystem::path& parent) {
    static std::atomic<unsigned long long> s_seq{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return parent / (prefix + std::to_string(stamp) + "_" + std::to_string(s_seq.fetch_add(1)));
}

// ── F1 RAII 目录 ─────────────────────────────────────────────────────────────
// 构造在 temp_directory_path() 下创建；析构清理（早退/异常路径同样析构，
// 根除"早退跳过清理"第二机制）；移动语义支持工厂函数按值返回。
class CLFTestTempDir {
public:
    explicit CLFTestTempDir(const std::string& prefix = "clf_test_") {
        m_path = makeTempPath(prefix, std::filesystem::temp_directory_path());
        std::error_code ec;
        std::filesystem::create_directories(m_path, ec);
        if (ec) {
#ifdef _WIN32
            std::fprintf(stderr, "[CLFTestTemp] 目录创建失败: %ls : %s\n", m_path.c_str(), ec.message().c_str());
#else
            std::fprintf(stderr, "[CLFTestTemp] 目录创建失败: %s : %s\n", m_path.u8string().c_str(), ec.message().c_str());
#endif
            std::exit(1);  // 创建失败是致命环境错误，直接判红
        }
        clf_test_temp_sentry.track(m_path);
    }

    CLFTestTempDir(const CLFTestTempDir&) = delete;             // 唯一所有权
    CLFTestTempDir& operator=(const CLFTestTempDir&) = delete;
    CLFTestTempDir(CLFTestTempDir&& other) noexcept { *this = std::move(other); }
    CLFTestTempDir& operator=(CLFTestTempDir&& other) noexcept {
        if (this != &other) {
            cleanupNow();
            m_path = std::exchange(other.m_path, {});
            m_removed = other.m_removed;
            other.m_removed = true;  // 被移走源的析构空转
        }
        return *this;
    }

    ~CLFTestTempDir() { cleanupNow(); }

    const std::filesystem::path& path() const noexcept { return m_path; }
    // UTF-8 字节（与各套件 u8ToString 同模式；本设施路径均无中文，
    // 与旧 fs::path::string() 的 CP_ACP 解码字节级等价）
    std::string string() const {
        const auto s = m_path.u8string();
        return std::string(reinterpret_cast<const char*>(s.data()), s.size());
    }
    // 隐式转换：qa 套件大量 up(dir)/dir + "/x.json"/save(..., dir, ...) 式
    // 调用零改动编译。若某调用点同时匹配 string/path 重载出现二义性 =
    // 编译错——改该点为显式 .path()/.string()，不要删 operator。
    operator const std::filesystem::path&() const noexcept { return m_path; }
    operator std::string() const { return string(); }

private:
    void cleanupNow() {
        if (!m_path.empty() && !m_removed) {
            m_removed = RemoveWithRetry(m_path);
            if (m_removed) clf_test_temp_sentry.untrack(m_path);  // 失败保持登记 → 哨兵判红
        }
    }
    std::filesystem::path m_path;
    bool m_removed = false;
};

// ── F1 RAII 文件 ─────────────────────────────────────────────────────────────
// 构造创建空文件；parent 默认 temp_directory_path()，CWD 场景显式传
// fs::current_path()（如插件边界校验以 cwd 为 workspace_root 的用例，
// 该语义不可破坏）。
class CLFTestTempFile {
public:
    explicit CLFTestTempFile(const std::string& prefix, const std::string& ext,
                             const std::filesystem::path& parent = {}) {
        m_path = makeTempPath(prefix, parent.empty() ? std::filesystem::temp_directory_path() : parent);
        m_path += ext;
        std::ofstream f(m_path.c_str(), std::ios::out | std::ios::binary);  // c_str()：Win 宽 / POSIX 窄
        if (!f) {
#ifdef _WIN32
            std::fprintf(stderr, "[CLFTestTemp] 文件创建失败: %ls\n", m_path.c_str());
#else
            std::fprintf(stderr, "[CLFTestTemp] 文件创建失败: %s\n", m_path.u8string().c_str());
#endif
            std::exit(1);
        }
        f.close();
        clf_test_temp_sentry.track(m_path);
    }

    CLFTestTempFile(const CLFTestTempFile&) = delete;
    CLFTestTempFile& operator=(const CLFTestTempFile&) = delete;
    CLFTestTempFile(CLFTestTempFile&& other) noexcept { *this = std::move(other); }
    CLFTestTempFile& operator=(CLFTestTempFile&& other) noexcept {
        if (this != &other) {
            cleanupNow();
            m_path = std::exchange(other.m_path, {});
            m_removed = other.m_removed;
            other.m_removed = true;
        }
        return *this;
    }

    ~CLFTestTempFile() { cleanupNow(); }

    const std::filesystem::path& path() const noexcept { return m_path; }
    std::string string() const {
        const auto s = m_path.u8string();
        return std::string(reinterpret_cast<const char*>(s.data()), s.size());
    }
    operator const std::filesystem::path&() const noexcept { return m_path; }
    operator std::string() const { return string(); }

private:
    void cleanupNow() {
        if (!m_path.empty() && !m_removed) {
            m_removed = RemoveWithRetry(m_path);
            if (m_removed) clf_test_temp_sentry.untrack(m_path);
        }
    }
    std::filesystem::path m_path;
    bool m_removed = false;
};

// ── 哨兵析构：比对 + 判红（全部测试跑完后执行） ──────────────────────────────
inline CLFTestTempSentry::~CLFTestTempSentry() {
    std::size_t leaks = 0;
    for (const auto& p : m_tracked) {
        std::error_code ec;
        if (std::filesystem::exists(p, ec) || ec) {
            ++leaks;
#ifdef _WIN32
            std::fprintf(stderr, "[CLFTestTemp] 残留: %ls\n", p.c_str());
#else
            std::fprintf(stderr, "[CLFTestTemp] 残留: %s\n", p.u8string().c_str());
#endif
        }
    }
    if (leaks > 0) {
        std::fprintf(stderr, "[CLFTestTemp] %zu 处临时物残留，测试判定失败\n", leaks);
        std::exit(1);  // boost::ut 已打完汇总；退出码改变让 ctest 判红（同 ut ~runner 机制）
    }
}

inline void CLFTestTempSentry::untrack(const std::filesystem::path& p) {
    for (auto it = m_tracked.begin(); it != m_tracked.end(); ++it) {
        if (*it == p) {
            m_tracked.erase(it);
            return;
        }
    }
}

}  // namespace CLFTest
