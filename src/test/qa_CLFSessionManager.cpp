// qa_CLFSessionManager.cpp — 会话管理单元测试
// 覆盖：save/load round-trip、incomplete 命名/查找/promote、cleanupOld

#include <boost/ut.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "CLFCore/CLFSessionManager.hpp"

using namespace boost::ut;
using CLF::CLFCore::CLFSessionManager;
using CLF::CLFCore::CLFMessage;
using CLF::CLFCore::CLFToolCall;

namespace {

// 唯一临时目录（测试结束自动清理）
std::string makeTempDir() {
    auto path = std::filesystem::temp_directory_path()
              / ("clf_test_" + std::to_string(
                     std::chrono::system_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(path);
    return path.string();
}

} // anonymous namespace

const boost::ut::suite<"CLFSessionManager"> tests = [] {
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
        expect(std::filesystem::exists(path));

        std::vector<CLFMessage> loaded;
        expect(CLFSessionManager::load(path, loaded));
        expect(loaded.size() == 3);
        expect(loaded[1].m_role == "assistant");
        expect(loaded[1].m_toolCalls.size() == 1);
        expect(loaded[1].m_toolCalls[0].m_name == "read_file");

        std::filesystem::remove_all(dir);
    };

    "incomplete 保存命名带后缀 + findIncomplete 找到"_test = [] {
        auto dir = makeTempDir();
        std::vector<CLFMessage> messages;
        messages.push_back({"user", "hello"});

        std::string path = CLFSessionManager::save(messages, dir, true);
        expect(!path.empty());
        expect(path.find("_incomplete.json") != std::string::npos);

        std::string found = CLFSessionManager::findIncomplete(dir);
        expect(!found.empty());
        expect(found.find("_incomplete.json") != std::string::npos);

        std::filesystem::remove_all(dir);
    };

    "promote 去掉 _incomplete 后缀"_test = [] {
        auto dir = makeTempDir();
        std::vector<CLFMessage> messages;
        messages.push_back({"user", "hi"});

        std::string inc = CLFSessionManager::save(messages, dir, true);
        std::string formal = CLFSessionManager::promote(inc);
        expect(!formal.empty());
        expect(formal.find("_incomplete") == std::string::npos);
        expect(std::filesystem::exists(formal));
        expect(!std::filesystem::exists(inc)); // 原文件已改名

        std::filesystem::remove_all(dir);
    };

    "list 返回按时间倒序的会话（不含 incomplete）"_test = [] {
        auto dir = makeTempDir();
        std::vector<CLFMessage> messages;
        messages.push_back({"user", "第一条会话"});
        CLFSessionManager::save(messages, dir, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        messages[0] = {"user", "第二条会话"};
        CLFSessionManager::save(messages, dir, false);
        CLFSessionManager::save(messages, dir, true); // incomplete 不入列表

        auto sessions = CLFSessionManager::list(dir, 10);
        expect(sessions.size() == 2);
        expect(sessions[0].m_title == "第二条会话"); // 最新在前

        std::filesystem::remove_all(dir);
    };

    "cleanupOld 删除过期文件保留新文件"_test = [] {
        auto dir = makeTempDir();
        std::vector<CLFMessage> messages;
        messages.push_back({"user", "x"});

        // 造一个 31 天前的旧文件
        std::string oldPath = dir + "/2026-01-01_00-00-00.json";
        {
            std::ofstream f(oldPath);
            f << "{\"version\":1,\"messages\":[{\"role\":\"user\",\"content\":\"old\"}]}";
        }
        auto oldTime = std::filesystem::file_time_type::clock::now()
                     - std::chrono::hours(31 * 24);
        std::filesystem::last_write_time(oldPath, oldTime);

        CLFSessionManager::save(messages, dir, false); // 新文件

        int removed = CLFSessionManager::cleanupOld(dir, 30);
        expect(removed >= 1);
        expect(!std::filesystem::exists(oldPath));

        std::filesystem::remove_all(dir);
    };

    "load 不存在文件返回 false"_test = [] {
        std::vector<CLFMessage> out;
        expect(!CLFSessionManager::load("no_such_file.json", out));
    };
};

// Boost.UT：测试在静态初始化时注册，cfg 析构时自动运行并输出报告
int main() {
    return 0;
}
