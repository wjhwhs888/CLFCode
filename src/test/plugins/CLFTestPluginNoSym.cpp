// CLFTestPluginNoSym.cpp — 合法 DLL 但无插件工厂符号的变体（P10a 缺符号用：
// GetProcAddress("CLFPluginCreate") 失败 → 管理器跳过，不崩）。
// include ABI 头仅为取 CLF_PLUGIN_EXPORT 宏；本文件故意不导出工厂符号。

#include "CLFPluginApi/CLFPluginApi.hpp"

extern "C" CLF_PLUGIN_EXPORT void clfTestNoSymMarker() {}
