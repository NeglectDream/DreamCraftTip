// ============================================================================
// InlineIconLayer — 物品图标的 Scintilla 行内绘制层
//
// 职责（SRP）：管理各 Scintilla 视图中的物品图标文档锚点，预留不修改
//              文本的视觉槽位，并在编辑器完成绘制后叠加 PNG 图标。
// ============================================================================
#pragma once

#include "ScintillaGateway.h"
#include "IconRegistry.h"
#include "sdk/Sci_Position.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

enum class InlineIconSlot {
    Whitespace,
    CompactColon,
    Tab
};

struct InlineIconAnchor {
    Sci_Position line = 0;
    Sci_Position slotPosition = 0;
    InlineIconSlot slot = InlineIconSlot::Whitespace;
    std::string itemId;
};

class InlineIconLayer {
public:
    InlineIconLayer(ScintillaGateway& gateway, IconRegistry& registry,
                    std::wstring slotFontPath);
    ~InlineIconLayer();

    // 选择后续扫描写入的 Scintilla 视图，并保留其他视图的独立锚点。
    void attach(HWND scintilla);

    // 初始化隐藏 indicator、私有宽槽字体 style，并关闭旧 margin。
    void init();

    // 恢复 [start, end) 内的视觉槽位 style 与 indicator。
    void clearRange(Sci_Position start, Sci_Position end);
    // 清除当前视图全部锚点；扫描完成后统一 refresh，避免重复重绘。
    void clearAnchors();
    // 清除所有显示同一 Scintilla 文档的视图锚点，并立即请求这些视图重绘。
    void clearDocumentAnchors(sptr_t document);

    // 应用视觉槽位 style，并记录一个物品图标锚点。
    bool addIcon(Sci_Position line, Sci_Position slotPosition,
                 InlineIconSlot slot, const std::string& itemId);

    // 在 SCN_PAINTED 后为指定视图绘制可见图标。
    void paint(HWND scintilla) const;

    // 请求当前视图重新绘制。
    void refresh() const;

private:
    using AnchorList = std::vector<InlineIconAnchor>;

    void restoreSlotStyles(Sci_Position start, Sci_Position end);
    bool applySlotStyle(Sci_Position position, InlineIconSlot slot);

    ScintillaGateway& gateway_;
    IconRegistry& registry_;
    std::wstring slotFontPath_;
    bool slotFontLoadAttempted_ = false;
    bool slotFontLoaded_ = false;
    HWND currentView_ = nullptr;
    std::unordered_set<HWND> fontNotifiedViews_;
    std::unordered_map<HWND, sptr_t> documentsByView_;
    std::unordered_map<HWND, AnchorList> anchorsByView_;
};
