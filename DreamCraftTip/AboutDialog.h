// ============================================================================
// AboutDialog — 自定义关于面板
//
// 职责（SRP）：以无资源（内存对话框模板）方式呈现作者/版本/官网，并
//              提供官网一键跳转。不依赖 .rc，保持构建链零资源（KISS）。
// ============================================================================
#pragma once

#include <windows.h>

namespace AboutDialog {

// 弹出模态关于面板；parent 为 Notepad++ 主窗口，用于禁用父窗口与居中归属。
// 内部自建 DLGTEMPLATE，无需任何资源文件。
void show(HWND parent);

} // namespace AboutDialog
