// qa_CLFTipsBar.cpp — Tips 行测试（A5，设计-工具调用循环上限机制改造 §五）
// P1 加载正常 / P2 兜底 / P3 轮播切换 / P4 静默阈值变红 / P5 恢复回灰 / P6 空闲隐藏

#include <boost/ut.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "CLFTUI/CLFTipsBar.hpp"

using namespace boost::ut;
using CLF::CLFTUI::CLFTipsBar;
using CLF::CLFTypes::ICLFProgressOutput;

namespace {

// C3 mock 分化：TipsBar 仅依赖 ICLFProgressOutput（activityCount 基类非虚实现），
// mock 只实现进度通道 2 个纯虚
class MockOutput : public ICLFProgressOutput {
public:
    void setStatus(const std::string&, int, int) override {}
    void setStatusTextOnly(const std::string&) override {}
    void showProgress(const std::vector<std::string>&) override {}
    void finishProgress(const std::string&) override {}
};

// 写临时 tips 文件（UTF-8 行 + 注释 + 空行），返回路径
std::string writeTipsFile(const std::vector<std::string>& entries) {
    const std::string path = (std::filesystem::temp_directory_path()
        / ("clf_tips_" + std::to_string(
               std::chrono::steady_clock::now().time_since_epoch().count())
           + ".txt")).string();
    std::ofstream f(path, std::ios::binary);
    for (const auto& e : entries) f << e << "\n";
    return path;
}

} // anonymous namespace

const boost::ut::suite<"CLFTipsBar"> tests = [] {
    "P1 加载正常：文件条目逐行读取（跳过注释/空行）"_test = [] {
        const std::string path = writeTipsFile({
            "# 注释行",
            "条目一",
            "",
            "条目二",
        });
        MockOutput out;
        CLFTipsBar bar(&out, 5, 300, path, /*startTimer=*/false);

        expect(bar.entryCount() == 2_ul);
        const std::string line = bar.currentLine(true);
        expect(line == std::string("Tips: 条目一"));   // 轮播从首条开始 + 前缀

        std::filesystem::remove(path);
    };

    "P2 文件缺失：内置兜底列表（永远有内容）"_test = [] {
        MockOutput out;
        CLFTipsBar bar(&out, 5, 300,
            "Z:/no_such_dir/no_such_tips.txt", /*startTimer=*/false);

        expect(bar.entryCount() > 0_ul);
        expect(!bar.currentLine(true).empty());
    };

    "P3 轮播切换：tick 后条目变化"_test = [] {
        const std::string path = writeTipsFile({"条目一", "条目二"});
        MockOutput out;
        CLFTipsBar bar(&out, 5, 300, path, /*startTimer=*/false);

        const std::string first = bar.currentLine(true);
        bar.tick();
        const std::string second = bar.currentLine(true);
        expect(first == std::string("Tips: 条目一"));
        expect(second == std::string("Tips: 条目二"));   // 索引 +1 → 第二条

        std::filesystem::remove(path);
    };

    "P4 静默超阈值：busy 中无活动累计 → 浅红异常文案"_test = [] {
        const std::string path = writeTipsFile({"条目一"});
        MockOutput out;
        // 间隔 1s / 阈值 3s：手动 tick 3 次（busy 中无活动）→ 触发
        CLFTipsBar bar(&out, 1, 3, path, /*startTimer=*/false);

        bar.setBusy(true);                    // 回合内（render 每帧写入的 busy 快照）
        bar.tick();  // silence=1
        bar.tick();  // silence=2
        expect(bar.currentLine(true).find("无响应") == std::string::npos);
        bar.tick();  // silence=3 → 触发
        expect(bar.currentLine(true).find("无响应") != std::string::npos);

        std::filesystem::remove(path);
    };

    "P5 输出恢复：notifyActivity 后回灰轮播"_test = [] {
        const std::string path = writeTipsFile({"条目一", "条目二"});
        MockOutput out;
        CLFTipsBar bar(&out, 1, 3, path, /*startTimer=*/false);

        bar.setBusy(true);
        bar.tick(); bar.tick(); bar.tick();   // 触发异常态
        expect(bar.currentLine(true).find("无响应") != std::string::npos);

        out.notifyActivity();                 // 模型有输出 → 活动计数 +1
        bar.tick();                           // 检测到增量 → 静默清零
        expect(bar.currentLine(true).find("无响应") == std::string::npos);
        expect(bar.currentLine(true).find("条目") != std::string::npos);

        std::filesystem::remove(path);
    };

    "P7 回合外静默清零：闲置后新回合不误报（2026-10-12 实机取证回归）"_test = [] {
        const std::string path = writeTipsFile({"条目一"});
        MockOutput out;
        CLFTipsBar bar(&out, 1, 3, path, /*startTimer=*/false);

        // 回合内触发异常态
        bar.setBusy(true);
        bar.tick(); bar.tick(); bar.tick();
        expect(bar.currentLine(true).find("无响应") != std::string::npos);

        // 回合结束（render(false) 更新快照）→ 下一 tick 静默清零，
        // 用户闲置期间不再累积（原缺陷：闲置 1 小时 → 新回合误报"已 3600s"）
        bar.setBusy(false);
        bar.tick();
        bar.tick();

        // 闲置许久后新提问：从 0 开始计——不误报历史闲置时长
        bar.setBusy(true);
        bar.tick();   // silence=1 < 3
        bar.tick();   // silence=2
        expect(bar.currentLine(true).find("无响应") == std::string::npos);
        bar.tick();   // silence=3 → 新回合内真无输出才触发
        expect(bar.currentLine(true).find("无响应") != std::string::npos);

        std::filesystem::remove(path);
    };

    "P6 空闲隐藏：busy=false 返回空串"_test = [] {
        const std::string path = writeTipsFile({"条目一"});
        MockOutput out;
        CLFTipsBar bar(&out, 5, 300, path, /*startTimer=*/false);

        expect(bar.currentLine(false).empty());   // 空闲不打扰
        expect(!bar.currentLine(true).empty());   // 请求中显示

        std::filesystem::remove(path);
    };
};

int main() {}
