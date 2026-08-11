// ============================================================================
// McConstants.h — Notepad++ Minecraft YAML 助手插件 · 全局常量
//
// 集中定义 indicator / style / 行内图标与节流参数，消除魔数。
//
// 资源分配依据（Notepad++ 源码 resource.h + ScintillaEditView::init）：
//   indicator 0-8   : Notepad++ 内部（URL_INDIC=8 等）
//   indicator 21-43 : Notepad++ 内部（IME、智能高亮等）
//   indicator 9-20  : 插件安全区 —— 本插件占用 9-14
//   marker   18-20  : Notepad++ 内部（书签=20、折叠隐藏=18/19）
//   marker   21-32  : Scintilla 折叠/Change History
// ============================================================================
#pragma once

#include <windows.h>

// ---- Indicator 号（安全区 9-20，本插件用 9-14，留 15-20 余量）----
// MC 颜色：INDIC_TEXTFORE(17) + SC_INDICFLAG_VALUEFORE(1)
//          单槽位承载全 RGB，颜色由 SCI_SETINDICATORVALUE 携带
#define INDIC_MC_COLOR     9

// MC 加粗范围元数据：INDIC_HIDDEN，不参与可见渲染
#define INDIC_MC_BOLD      10
// MC 斜体修饰：INDIC_SQUIGGLE(1)，波浪线
#define INDIC_MC_ITALIC    11
// MC 下划线修饰：INDIC_PLAIN(0)，单线
#define INDIC_MC_UNDERLINE 12
// MC 删除线修饰：INDIC_STRIKE(4)
#define INDIC_MC_STRIKE    13
// 行内图标槽位范围元数据：INDIC_HIDDEN
#define INDIC_MC_ICON_SLOT 14

// ---- 字符 Style ----
// YAML lexer 使用 0..8；230..238 是横向扩展槽位，240..248 是粗体克隆。
#define STYLE_MC_ICON_SLOT_BASE  230
#define STYLE_MC_ICON_SLOT_COUNT 9
#define STYLE_MC_BOLD_BASE       240
#define STYLE_MC_BOLD_COUNT      9
// Scintilla YAML lexer 的块标量正文 style（SCE_YAML_TEXT）。
#define STYLE_YAML_BLOCK_TEXT    7

// ---- 行内图标 ----
// 旧版本曾占用 margin 4；新版本仅把其宽度归零，不再注册 marker。
#define MC_MARGIN          4
// overlay 图标的最大边长（像素），实际尺寸受槽位宽度和行高约束。
#define MC_ICON_SIZE       16

// ---- 节流与扫描上限 ----
// SCN_MODIFIED 每次按键触发，用 SetTimer 合并此窗口内的多次为一次
#define MC_DEBOUNCE_MS           300
// 文件打开/切换会连续产生多个 NPP 通知，用短窗口合并为一次扫描。
#define MC_FILE_EVENT_DEBOUNCE_MS 25
// 超过此字节数的文件只扫描可见区域 ± 该范围，避免大文件卡顿
#define MC_MAX_SCAN_BYTES        524288  // 512 KB
#define MC_VISIBLE_PADDING_CHARS 5000    // 大文件可见区上下文缓冲

// ---- 目标文件扩展名 ----
// Notepad++ 的 L_YAML 语言已覆盖 .yml/.yaml，但用户可能用 L_TEXT 打开，
// 故同时检查扩展名与语言类型双重判定
#define MC_YAML_EXTS       L".yml;.yaml;"
