// ============================================================================
// PluginEntry — Notepad++ 插件 6 个导出函数 + 通知分发
//
// 职责（SRP）：实现 Notepad++ 插件接口契约（PluginInterface.h），把通知
//              转发给 DecorationCoordinator。本文件不含业务逻辑，只做胶水。
//
// 导出契约（PluginInterface.h:65-72）：
//   setInfo / getName / getFuncsArray / beNotified / messageProc / isUnicode
// ============================================================================
#pragma once

#include "sdk/PluginInterface.h"

extern "C" __declspec(dllexport) void setInfo(NppData nppData);
extern "C" __declspec(dllexport) const wchar_t* getName();
extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* nbFuncItem);
extern "C" __declspec(dllexport) void beNotified(SCNotification* notify);
extern "C" __declspec(dllexport) LRESULT messageProc(UINT msg, WPARAM wp, LPARAM lp);
extern "C" __declspec(dllexport) BOOL isUnicode();
