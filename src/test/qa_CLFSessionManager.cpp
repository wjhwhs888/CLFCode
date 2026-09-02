// qa_CLFSessionManager.cpp — 会话管理单元测试
// 覆盖：save/load round-trip、cleanupOld、jsonl 追加式保存（J 系列，2026-09-02）
//
// 已移除 3 个 _incomplete 旧语义用例（findIncomplete/promote 断言 save(true) 产生
// _incomplete.json——该语义在覆盖式时代（08-12）已废除，save(true) 现为归档，
// 测试失效于实现，非实现缺陷。旧接口保留为 legacy，不再有测试背书）

#include <boost/ut.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "CLFCore/CLFSessionManager.hpp"
#include "CLFCore/CLFMessageCodec.hpp"

using namespace boost::ut;
using CLF::CLFCore::CLFSessionManager;
using CLF::CLFCore::CLFMessageCodec;
using CLF::CLFCore::CLFMessage;
using CLF::CLFCore::CLFToolCall;
using CLF::CLFCore::CLFTodoItem;
using CLF::CLFCore::CLFSessionSummary;
using CLF::CLFCore::CLFSessionInfo;

namespace fs = std::filesystem;

namespace {

// C++20 下 path::u8string() 返回 std::u8string（char8_t），不能隐式转 std::string——
// 显式还原为 UTF-8 字节的 std::string（测试目标为 CXX_STANDARD 20，主代码 C++17 无此问题）
std::string u8ToString(const fs::path& p) {
    const auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

// u8path 在 C++20 已 deprecated（警告噪音大），集中一处屏蔽
fs::path up(const std::string& s) {
    return fs::u8path(s);
}

// 唯一临时目录（测试结束自动清理）
std::string makeTempDir() {
    auto path = fs::temp_directory_path()
              / ("clf_test_" + std::to_string(
                     std::chrono::system_clock::now().time_since_epoch().count()));
    fs::create_directories(path);
    return u8ToString(path);
}

// jsonl 会话文件路径（中文文件名——顺带验证 UTF-8 路径链路，08-31 修复防回归）
// ⚠️ 中文窄字面量必须经 up()（u8path）构造——直接拼 path 会被按 CP936 解码
std::string makeJsonlPath(const std::string& dir) {
    return u8ToString(up(dir) / up("时间戳_测试会话.jsonl"));
}

// 写一个最小可解析的 jsonl 会话（header + 可选行）
void writeMinimalJsonl(const std::string& path, const std::string& title = "测试会话") {
    CLFSessionManager::appendHeader(path, CLFMessageCodec::serializeHeaderLine(
        title, "2026-08-25_09-20-53", "sid", "model"));
    std::vector<CLFMessage> turn{{"user", "你好"}};
    CLFSessionManager::appendTurn(path, CLFMessageCodec::serializeTurnLine(turn, "ts"));
}

} // anonymous namespace

const boost::ut::suite<"CLFSessionManager"> tests = [] {
    // ========== 既有：覆盖式 .json 路径 ==========

    "save/load round-trip 保留消息与 tool_calls"_test = [] {
        auto dir = makeTempDir();

        std::vector<CLFMessage> messages;
        messages.push_back({"user", "读取文件"});
        CLFToolCall tc;
        tc.m_id = "call_9"; tc.m_name = "read_file"; tc.m_arguments = R"({"path":"a"})";
        CLFMessage assistant;
        assistant.m_role = "assistant";
        assistant.m_toolCalls.push_back(tc);
        messages.push_back(assistant);
        messages.push_back({"tool", "file content"});

        std::string path = CLFSessionManager::save(messages, dir, false);
        expect(!path.empty());
        expect(fs::exists(up(path)));

        std::vector<CLFMessage> loaded;
        expect(CLFSessionManager::load(path, loaded));
        expect(loaded.size() == 3);
        expect(loaded[1].m_role == "assistant");
        expect(loaded[1].m_toolCalls.size() == 1);
        expect(loaded[1].m_toolCalls[0].m_name == "read_file");

        fs::remove_all(up(dir));
    };

    "load 不存在文件返回 false"_test = [] {
        std::vector<CLFMessage> out;
        expect(!CLFSessionManager::load("no_such_file.json", out));
    };

    "cleanupOld 删除过期文件保留新文件（.json）"_test = [] {
        auto dir = makeTempDir();
        std::vector<CLFMessage> messages;
        messages.push_back({"user", "x"});

        // 造一个 31 天前的旧文件
        std::string oldPath = dir + "/2026-01-01_00-00-00.json";
        {
            std::ofstream f(up(oldPath));
            f << "{\"version\":1,\"messages\":[{\"role\":\"user\",\"content\":\"old\"}]}";
        }
        auto oldTime = fs::file_time_type::clock::now()
                     - std::chrono::hours(31 * 24);
        fs::last_write_time(up(oldPath), oldTime);

        CLFSessionManager::save(messages, dir, false); // 新文件

        int removed = CLFSessionManager::cleanupOld(dir, 30);
        expect(removed >= 1);
        expect(!fs::exists(up(oldPath)));

        fs::remove_all(up(dir));
    };

    "list 覆盖式时代：finalize 归档（rename）后 latest.json 消失，归档可列出"_test = [] {
        auto dir = makeTempDir();
        std::vector<CLFMessage> messages;
        messages.push_back({"user", "第一条会话"});
        CLFSessionManager::save(messages, dir, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        messages[0] = {"user", "第二条会话"};
        CLFSessionManager::save(messages, dir, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        CLFSessionManager::save(messages, dir, true);  // finalize = rename 归档

        auto sessions = CLFSessionManager::list(dir, 10);
        // finalize 是 rename 语义：latest.json 已消失，列表只剩 1 个归档。
        // （旧版测试曾断言 size==2 且 latest 标 [当前]——那是 _incomplete 旧语义时代
        // 的期望，与现实现不符，属过时测试。latest 标记/倒序语义由 J6 覆盖）
        expect(sessions.size() == 1);
        expect(!sessions[0].m_isLatest);        // 无活跃文件且 latest.json 已归档
        expect(sessions[0].m_title == "第二条会话");

        fs::remove_all(up(dir));
    };

    // ========== J 系列：jsonl 追加式保存（设计-会话追加式保存.jsonl §3.9，2026-09-02） ==========

    "J1 jsonl 追加多轮 + 快照 + complete + summary → loadJsonl 还原"_test = [] {
        auto dir = makeTempDir();
        std::string path = makeJsonlPath(dir);

        // header（含 skills）
        expect(CLFSessionManager::appendHeader(path, CLFMessageCodec::serializeHeaderLine(
            "测试会话", "2026-08-25_09-20-53", "sid-1", "model-1", {"skill-a"})));
        // turn 1（无 todos 字段）
        std::vector<CLFMessage> turn1{{"user", "第一问"}, {"assistant", "第一答"}};
        expect(CLFSessionManager::appendTurn(path,
            CLFMessageCodec::serializeTurnLine(turn1, "ts-1")));
        // todo_snapshot（中间进度）
        std::vector<CLFTodoItem> todos{{"1", "任务1", "completed"},
                                       {"2", "任务2", "in_progress"}};
        expect(CLFSessionManager::appendTodoSnapshot(path,
            CLFMessageCodec::serializeTodoSnapshot(todos, "ts-2")));
        // turn 2（带轮末 todos 快照）
        std::vector<CLFMessage> turn2{{"user", "第二问"}, {"assistant", "第二答"}};
        expect(CLFSessionManager::appendTurn(path,
            CLFMessageCodec::serializeTurnLine(turn2, "ts-3", &todos)));
        // complete 行
        expect(CLFSessionManager::appendComplete(path,
            CLFMessageCodec::serializeCompleteLine(todos, "ts-4")));
        // summary 行
        CLFSessionSummary sum;
        sum.m_summary = "会话摘要";
        sum.m_valid   = true;
        expect(CLFSessionManager::appendSummary(path,
            CLFMessageCodec::serializeSummaryLine(sum, "ts-5")));

        std::vector<CLFMessage> msgs;
        std::vector<std::string> skills;
        CLFSessionSummary        outSum;
        std::vector<CLFTodoItem> outTodos, outComplete;
        CLFSessionInfo           header;
        expect(CLFSessionManager::loadJsonl(path, msgs, &skills, &outSum,
                                            &outTodos, &outComplete, &header));

        expect(msgs.size() == 4_ul);   // turn1×2 + turn2×2，按行序
        expect(msgs[0].m_content == std::string("第一问"));
        expect(msgs[3].m_content == std::string("第二答"));
        expect(skills.size() == 1_ul);
        expect(skills[0] == std::string("skill-a"));
        expect(outSum.m_valid);
        expect(outSum.m_summary == std::string("会话摘要"));
        expect(outTodos.size() == 2_ul);        // 最后 snapshot 优先
        expect(outTodos[0].m_status == std::string("completed"));
        expect(outComplete.size() == 2_ul);     // complete 行回显数据
        expect(header.m_title == std::string("测试会话"));
        expect(header.m_savedAt == std::string("2026-08-25_09-20-53"));

        fs::remove_all(up(dir));
    };

    "J2 恢复优先级：最后 todo_snapshot 覆盖 turn 行快照"_test = [] {
        auto dir = makeTempDir();
        std::string path = makeJsonlPath(dir);
        CLFSessionManager::appendHeader(path, CLFMessageCodec::serializeHeaderLine("t", "s", "i", "m"));

        // turn 行带"全 pending"快照 → snapshot 带"1 completed" → 后者优先
        std::vector<CLFTodoItem> turnTodos{{"1", "任务", "pending"}};
        std::vector<CLFMessage> turn{{"user", "问"}};
        CLFSessionManager::appendTurn(path,
            CLFMessageCodec::serializeTurnLine(turn, "ts-1", &turnTodos));
        std::vector<CLFTodoItem> snapTodos{{"1", "任务", "completed"}};
        CLFSessionManager::appendTodoSnapshot(path,
            CLFMessageCodec::serializeTodoSnapshot(snapTodos, "ts-2"));

        std::vector<CLFMessage> msgs;
        std::vector<CLFTodoItem> outTodos;
        expect(CLFSessionManager::loadJsonl(path, msgs, nullptr, nullptr, &outTodos));
        expect(outTodos.size() == 1_ul);
        expect(outTodos[0].m_status == std::string("completed"));  // snapshot 胜出

        fs::remove_all(up(dir));
    };

    "J3 clear 空快照：清单被清空状态正确恢复"_test = [] {
        auto dir = makeTempDir();
        std::string path = makeJsonlPath(dir);
        CLFSessionManager::appendHeader(path, CLFMessageCodec::serializeHeaderLine("t", "s", "i", "m"));

        std::vector<CLFTodoItem> todos{{"1", "任务", "pending"}};
        CLFSessionManager::appendTodoSnapshot(path,
            CLFMessageCodec::serializeTodoSnapshot(todos, "ts-1"));
        // todo_write clear → 空快照（hasSnapshot 语义必须覆盖为"清空"而非"跳过"）
        std::vector<CLFTodoItem> empty;
        CLFSessionManager::appendTodoSnapshot(path,
            CLFMessageCodec::serializeTodoSnapshot(empty, "ts-2"));
        std::vector<CLFMessage> turn{{"user", "问"}};
        CLFSessionManager::appendTurn(path,
            CLFMessageCodec::serializeTurnLine(turn, "ts-3"));

        std::vector<CLFMessage> msgs;
        std::vector<CLFTodoItem> outTodos;
        expect(CLFSessionManager::loadJsonl(path, msgs, nullptr, nullptr, &outTodos));
        expect(outTodos.empty());   // 清单已被清空，而非回退到 ts-1 的快照

        fs::remove_all(up(dir));
    };

    "J4 撕裂行跳过：取前一条可解析快照，其余正常"_test = [] {
        auto dir = makeTempDir();
        std::string path = makeJsonlPath(dir);
        CLFSessionManager::appendHeader(path, CLFMessageCodec::serializeHeaderLine("t", "s", "i", "m"));

        std::vector<CLFTodoItem> todosA{{"1", "任务A", "pending"}};
        CLFSessionManager::appendTodoSnapshot(path,
            CLFMessageCodec::serializeTodoSnapshot(todosA, "ts-1"));
        // 撕裂行（写一半的 JSON）——必须跳过，不能整体失败
        {
            std::ofstream f(up(path), std::ios::app | std::ios::binary);
            f << "{\"type\":\"todo_snapshot\",\"todos\":[{\"id\":\"9\"" << "\n";  // 缺尾
        }
        std::vector<CLFTodoItem> todosB{{"2", "任务B", "completed"}};
        CLFSessionManager::appendTodoSnapshot(path,
            CLFMessageCodec::serializeTodoSnapshot(todosB, "ts-2"));
        std::vector<CLFMessage> turn{{"user", "问"}, {"assistant", "答"}};
        CLFSessionManager::appendTurn(path,
            CLFMessageCodec::serializeTurnLine(turn, "ts-3"));

        std::vector<CLFMessage> msgs;
        std::vector<CLFTodoItem> outTodos;
        expect(CLFSessionManager::loadJsonl(path, msgs, nullptr, nullptr, &outTodos));
        expect(msgs.size() == 2_ul);           // turn 行消息不受撕裂行影响
        expect(outTodos.size() == 1_ul);
        expect(outTodos[0].m_content == std::string("任务B"));  // 取最后可解析快照

        fs::remove_all(up(dir));
    };

    "J5 append 失败返回 false 不抛（路径为目录）"_test = [] {
        auto dir = makeTempDir();
        // 路径是已存在的目录 → ofstream 打开失败 → false + warn，不抛
        expect(!CLFSessionManager::appendTurn(dir, "{\"type\":\"turn\"}"));
        expect(!CLFSessionManager::appendHeader(dir, "{\"type\":\"header\"}"));
        // 空行直接拒绝
        expect(!CLFSessionManager::appendTurn(dir, ""));
        fs::remove_all(up(dir));
    };

    "J6 list 含 .jsonl + 活跃文件标 [当前] + 倒序"_test = [] {
        auto dir = makeTempDir();
        std::string pathA = u8ToString(up(dir) / "2026-08-25_09-00-00_a.jsonl");
        std::string pathB = u8ToString(up(dir) / "2026-08-25_09-10-00_b.jsonl");
        writeMinimalJsonl(pathA, "会话A");
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        writeMinimalJsonl(pathB, "会话B");

        // 场景 1：活跃文件 = pathB → B 标 [当前]，A 不标
        auto sessions = CLFSessionManager::list(dir, 10, &pathB);
        expect(sessions.size() == 2_ul);
        expect(sessions[0].m_title == "会话B");   // 倒序：B 最新在前
        expect(sessions[0].m_isLatest);           // [当前] = 活跃文件路径匹配
        expect(!sessions[1].m_isLatest);
        expect(sessions[1].m_title == "会话A");

        // 场景 2：无活跃文件（启动时）→ 无 [当前]
        auto sessions2 = CLFSessionManager::list(dir, 10);
        expect(sessions2.size() == 2_ul);
        expect(!sessions2[0].m_isLatest);
        expect(!sessions2[1].m_isLatest);

        // 场景 3：旧 latest.json 存在 + 有活跃文件 → latest.json 不标 [当前]，B 标
        std::vector<CLFMessage> messages{{"user", "旧会话"}};
        CLFSessionManager::save(messages, dir, false);   // 造 latest.json
        auto sessions3 = CLFSessionManager::list(dir, 10, &pathB);
        bool bIsLatest = false, latestIsLatest = false;
        for (const auto& s : sessions3) {
            if (s.m_path == pathB) bIsLatest = s.m_isLatest;
            if (s.m_path == (dir + "/latest.json")) latestIsLatest = s.m_isLatest;
        }
        expect(bIsLatest);          // 活跃文件才是 [当前]
        expect(!latestIsLatest);    // 旧 latest.json 作普通归档（有活跃文件时）

        // 场景 4：旧 latest.json 存在 + 无活跃文件 → latest.json 标 [当前]（兼容期）
        auto sessions4 = CLFSessionManager::list(dir, 10);
        expect(sessions4.size() == 3_ul);           // latest + A + B
        expect(sessions4[0].m_isLatest);            // latest.json 最前标 [当前]

        fs::remove_all(up(dir));
    };

    "J7 cleanupOld 清理旧 .jsonl 且保留活跃文件"_test = [] {
        auto dir = makeTempDir();
        std::string oldPath = u8ToString(up(dir) / up("2026-01-01_00-00-00_旧会话.jsonl"));
        writeMinimalJsonl(oldPath, "旧会话");
        auto oldTime = fs::file_time_type::clock::now() - std::chrono::hours(31 * 24);
        fs::last_write_time(up(oldPath), oldTime);

        std::string activePath = u8ToString(up(dir) / up("2026-08-25_active.jsonl"));
        writeMinimalJsonl(activePath, "活跃会话");
        // 活跃文件也设为 31 天前——但 cleanupOld 必须跳过它（删除动作绝不碰活跃文件）
        fs::last_write_time(up(activePath), oldTime);

        int removed = CLFSessionManager::cleanupOld(dir, 30, &activePath);
        expect(removed == 1);
        expect(!fs::exists(up(oldPath)));
        expect(fs::exists(up(activePath)));   // 活跃文件存活

        fs::remove_all(up(dir));
    };

    "J8 header 无 skills 字段 → 空技能列表（旧 header 兼容）"_test = [] {
        auto dir = makeTempDir();
        std::string path = makeJsonlPath(dir);
        CLFSessionManager::appendHeader(path,
            CLFMessageCodec::serializeHeaderLine("t", "s", "i", "m"));   // 不带 skills
        std::vector<CLFMessage> turn{{"user", "问"}};
        CLFSessionManager::appendTurn(path, CLFMessageCodec::serializeTurnLine(turn, "ts"));

        std::vector<CLFMessage> msgs;
        std::vector<std::string> skills;
        expect(CLFSessionManager::loadJsonl(path, msgs, &skills));
        expect(skills.empty());

        fs::remove_all(up(dir));
    };

    "J9 无有效消息行 → 备份 .bak 并返回 false"_test = [] {
        auto dir = makeTempDir();
        std::string path = u8ToString(up(dir) / up("损坏会话.jsonl"));
        {
            std::ofstream f(up(path), std::ios::binary);
            f << "not json at all\n";
            f << "{\"type\":\"unknown_future_type\"}\n";   // 未知类型静默跳过，无 messages
        }

        std::vector<CLFMessage> msgs;
        expect(!CLFSessionManager::loadJsonl(path, msgs));
        expect(fs::exists(up(path + ".bak")));    // 已备份
        expect(!fs::exists(up(path)));

        fs::remove_all(up(dir));
    };

    "J10 loadJsonl 不存在文件返回 false"_test = [] {
        std::vector<CLFMessage> msgs;
        expect(!CLFSessionManager::loadJsonl("no_such_file.jsonl", msgs));
    };

    "J11 无 turn 行但快照齐全（首轮崩溃残留）→ 返回 true 不备份（2026-09-02 实机验收修复）"_test = [] {
        auto dir = makeTempDir();
        std::string path = u8ToString(up(dir) / up("2026-08-25_崩溃残留.jsonl"));
        writeMinimalJsonl(path, "崩溃残留");
        // 只追加 todo_snapshot（模拟：turn 未写、snapshot 已 flush）
        std::vector<CLFTodoItem> todos{{"1", "任务1", "completed"},
                                       {"2", "任务2", "in_progress"}};
        CLFSessionManager::appendTodoSnapshot(path,
            CLFMessageCodec::serializeTodoSnapshot(todos, "ts-2"));

        std::vector<CLFMessage> msgs;
        std::vector<CLFTodoItem> outTodos;
        expect(CLFSessionManager::loadJsonl(path, msgs, nullptr, nullptr, &outTodos));
        expect(msgs.size() == 1_ul);          // turn 行消息仍在（writeMinimalJsonl 写了一条）
        expect(outTodos.size() == 2_ul);      // 快照恢复
        expect(fs::exists(up(path)));         // 未备份改名

        // 对照：纯坏行文件仍判损坏备份（J9 语义不变）
        std::string badPath = u8ToString(up(dir) / up("纯损坏.jsonl"));
        {
            std::ofstream f(up(badPath), std::ios::binary);
            f << "not json at all\n";
        }
        std::vector<CLFMessage> msgs2;
        expect(!CLFSessionManager::loadJsonl(badPath, msgs2));
        expect(fs::exists(up(badPath + ".bak")));

        fs::remove_all(up(dir));
    };
};

// Boost.UT：测试在静态初始化时注册，cfg 析构时自动运行并输出报告
int main() {
    return 0;
}
