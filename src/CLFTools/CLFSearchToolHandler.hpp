// CLFSearchToolHandler.hpp — search_content 工具 handler 共享实现（2.3，2026-09-21）
// 双消费者：CLFBuiltinTools（宿主静态注册）与 tools.search 插件 DLL。
// 只依赖能力（CLFSearchContent）+ clf_types + nlohmann json——不依赖 core/UI。
// example:
//   std::string out = CLF::CLFTools::searchContentToolHandler(args);
//   std::string out2 = CLF::CLFTools::searchContentToolHandler(
//       args, [] { return interrupted.load(); });

#pragma once

#include <functional>
#include <string>

namespace CLF::CLFTools {

// isCancelled 为空 = 不可取消（向后兼容）；命中 → 停止遍历、结果带中断标记
std::string searchContentToolHandler(const std::string& args,
                                     const std::function<bool()>& isCancelled = {});

} // namespace CLF::CLFTools
