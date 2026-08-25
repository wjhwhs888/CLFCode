// qa_CLFMessageCodec.cpp — 会话编解码单元测试（设计 S2-6 todos 字段）
// M1: todos 序列化 / 反序列化往返
// M2: 向后兼容——旧会话文件无 todos 字段时视为空清单
// M3: 空清单不写字段，保持文件干净
// M4: 与既有字段（skills / summary）共存不互相干扰

#include <boost/ut.hpp>

#include <string>
#include <vector>

#include "CLFCore/CLFMessageCodec.hpp"

using namespace boost::ut;
using CLF::CLFCore::CLFMessage;
using CLF::CLFCore::CLFMessageCodec;
using CLF::CLFCore::CLFSessionSummary;
using CLF::CLFCore::CLFTodoItem;

namespace {

std::vector<CLFMessage> sampleMessages() {
    std::vector<CLFMessage> msgs;
    CLFMessage user;
    user.m_role    = "user";
    user.m_content = "hello";
    msgs.push_back(user);
    return msgs;
}

std::vector<CLFTodoItem> sampleTodos() {
    return {
        {"1", "写设计文档", "completed"},
        {"2", "实现 todo_write", "in_progress"},
        {"3", "补测试", "pending"},
    };
}

} // anonymous namespace

const boost::ut::suite<"CLFMessageCodec"> tests = [] {
    // ========== M1: 往返 ==========

    "M1 todos 序列化后可完整解析回来"_test = [] {
        const auto json = CLFMessageCodec::serialize(
            sampleMessages(), "2026-08-25", "t", {}, nullptr, sampleTodos());

        std::vector<CLFTodoItem> out;
        auto msgs = CLFMessageCodec::parseFull(json, nullptr, nullptr, nullptr,
                                               nullptr, nullptr, &out);
        expect(msgs.size() == 1_ul);
        expect(out.size() == 3_ul);
        expect(out[0].m_id == std::string("1"));
        expect(out[0].m_content == std::string("写设计文档"));
        expect(out[0].m_status == std::string("completed"));
        expect(out[1].m_status == std::string("in_progress"));
        expect(out[2].m_status == std::string("pending"));
    };

    // ========== M2: 向后兼容（关键） ==========

    // 旧版本写出的会话文件没有 todos 字段，加载必须照常成功且得到空清单，
    // 否则升级后所有历史会话都会读不出来
    "M2a 旧格式（无 todos 字段）加载为空清单且不报错"_test = [] {
        const auto legacyJson = CLFMessageCodec::serialize(
            sampleMessages(), "2026-08-25", "t");   // 不传 todos

        expect(legacyJson.find("todos") == std::string::npos);

        std::vector<CLFTodoItem> out{{"stale", "残留项", "pending"}};
        auto msgs = CLFMessageCodec::parseFull(legacyJson, nullptr, nullptr, nullptr,
                                               nullptr, nullptr, &out);
        expect(msgs.size() == 1_ul);
        expect(out.empty());   // 必须被清空，不能残留上次的内容
    };

    "M2b outTodos 传 nullptr 时正常解析（不读取该字段）"_test = [] {
        const auto json = CLFMessageCodec::serialize(
            sampleMessages(), "", "", {}, nullptr, sampleTodos());
        auto msgs = CLFMessageCodec::parseFull(json);
        expect(msgs.size() == 1_ul);
    };

    "M2c version 维持 1（新增字段不提升版本）"_test = [] {
        const auto json = CLFMessageCodec::serialize(
            sampleMessages(), "", "", {}, nullptr, sampleTodos());
        int version = 0;
        CLFMessageCodec::parseFull(json, &version);
        expect(version == 1_i);
    };

    // ========== M3: 空清单不写字段 ==========

    "M3 空 todos 不产生 todos 字段"_test = [] {
        const auto json = CLFMessageCodec::serialize(
            sampleMessages(), "", "", {}, nullptr, {});
        expect(json.find("todos") == std::string::npos);
    };

    // ========== M4: 与既有字段共存 ==========

    "M4 todos 与 skills / summary 互不干扰"_test = [] {
        CLFSessionSummary summary;
        summary.m_summary = "会话摘要文本";
        summary.m_method  = "api";
        summary.m_valid   = true;

        const std::vector<std::string> skills{"skill-a", "skill-b"};
        const auto json = CLFMessageCodec::serialize(
            sampleMessages(), "2026-08-25", "标题", skills, &summary, sampleTodos());

        std::vector<std::string> outSkills;
        CLFSessionSummary        outSummary;
        std::vector<CLFTodoItem> outTodos;
        std::string              outTitle;
        auto msgs = CLFMessageCodec::parseFull(json, nullptr, nullptr, &outTitle,
                                               &outSkills, &outSummary, &outTodos);
        expect(msgs.size() == 1_ul);
        expect(outSkills.size() == 2_ul);
        expect(outSummary.m_valid);
        expect(outSummary.m_summary == std::string("会话摘要文本"));
        expect(outTodos.size() == 3_ul);
        expect(outTitle == std::string("标题"));
    };

    "M4b 缺 content 的待办项被丢弃"_test = [] {
        std::vector<CLFTodoItem> todos{{"1", "", "pending"}, {"2", "有内容", "pending"}};
        const auto json = CLFMessageCodec::serialize(
            sampleMessages(), "", "", {}, nullptr, todos);
        std::vector<CLFTodoItem> out;
        CLFMessageCodec::parseFull(json, nullptr, nullptr, nullptr, nullptr, nullptr, &out);
        expect(out.size() == 1_ul);
        expect(out[0].m_id == std::string("2"));
    };
};

int main() {}
