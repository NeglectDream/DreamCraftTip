// ============================================================================
// ColorIndicator — Minecraft 颜色与格式预览渲染层
//
// 职责（SRP）：把 McColorLexer 产出的 ColorSegment 应用到 Scintilla，
//              不修改任何文本字节。
//
// 渲染策略：
//   - 颜色：INDIC_TEXTFORE + SC_INDICFLAG_VALUEFORE，单槽承载任意 RGB。
//   - 粗体：克隆 YAML lexer 的基础 style 0..8 到 240..248，并提升字重。
//           INDIC_MC_BOLD 使用 INDIC_HIDDEN，仅记录粗体范围以便可靠恢复。
//   - 斜体/下划线/删除线：继续使用独立 indicator，可与颜色和粗体叠加。
// ============================================================================
#pragma once

#include "ScintillaGateway.h"
#include "McColorLexer.h"
#include "sdk/Sci_Position.h"

class ColorIndicator {
public:
    explicit ColorIndicator(ScintillaGateway& gateway);

    // 可重复初始化当前 Scintilla 控件的 indicator 与粗体 style。
    // 主/副视图切换后需再次调用，因为两套控件各自保存 style 定义。
    void init();

    // 恢复本插件覆盖的字符 style，并清除全文 indicator。
    void clearAll(Sci_Position docLen);

    // 把单个分段刷到 Scintilla。
    // valueStart: value 在文档中的绝对起点。
    void paint(const ColorSegment& seg, Sci_Position valueStart);

private:
    bool applyBoldStyle(Sci_Position start, Sci_Position len);
    void restoreBoldStyles(Sci_Position docLen);

    ScintillaGateway& gateway_;
};
