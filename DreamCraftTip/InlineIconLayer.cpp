// ============================================================================
// InlineIconLayer 实现 — 视觉槽位、可见区筛选与 GDI+ overlay
// ============================================================================
#include "InlineIconLayer.h"
#include "McConstants.h"
#include "sdk/Scintilla.h"
#include <wtypes.h>
#include <gdiplus.h>
#include <algorithm>
#include <utility>

namespace {
constexpr char kSlotFontName[] = "NppMcInlineSlot";

class ScopedWindowDC {
public:
    explicit ScopedWindowDC(HWND window) noexcept
        : window_(window), dc_(::GetDC(window)) {
    }

    ~ScopedWindowDC() {
        if (dc_) ::ReleaseDC(window_, dc_);
    }

    HDC get() const noexcept { return dc_; }

private:
    HWND window_ = nullptr;
    HDC dc_ = nullptr;
};
}

InlineIconLayer::InlineIconLayer(ScintillaGateway& gateway, IconRegistry& registry,
                                 std::wstring slotFontPath)
    : gateway_(gateway), registry_(registry),
      slotFontPath_(std::move(slotFontPath)) {
}

InlineIconLayer::~InlineIconLayer() {
    if (slotFontLoaded_)
        ::RemoveFontResourceExW(slotFontPath_.c_str(), FR_PRIVATE | FR_NOT_ENUM, nullptr);
}

void InlineIconLayer::attach(HWND scintilla) {
    currentView_ = scintilla;
    if (currentView_)
        documentsByView_[currentView_] = gateway_.getDocumentPointer();
    if (slotFontLoaded_ && currentView_ && fontNotifiedViews_.insert(currentView_).second)
        ::SendMessage(currentView_, WM_FONTCHANGE, 0, 0);
}

void InlineIconLayer::init() {
    // 旧版本占用 margin 4；新版本必须主动收回该列。
    gateway_.setMarginWidthN(MC_MARGIN, 0);
    gateway_.indicSetStyle(INDIC_MC_ICON_SLOT, INDIC_HIDDEN);
    gateway_.indicSetFlags(INDIC_MC_ICON_SLOT, SC_INDICFLAG_NONE);

    if (!slotFontLoadAttempted_) {
        slotFontLoadAttempted_ = true;
        slotFontLoaded_ = !slotFontPath_.empty() &&
            ::AddFontResourceExW(slotFontPath_.c_str(),
                                 FR_PRIVATE | FR_NOT_ENUM, nullptr) > 0;
        if (slotFontLoaded_ && currentView_) {
            fontNotifiedViews_.insert(currentView_);
            ::SendMessage(currentView_, WM_FONTCHANGE, 0, 0);
        }
    }

    for (int baseStyle = 0; baseStyle < STYLE_MC_ICON_SLOT_COUNT; ++baseStyle) {
        const int slotStyle = STYLE_MC_ICON_SLOT_BASE + baseStyle;
        if (slotFontLoaded_)
            gateway_.cloneStyleWithFont(baseStyle, slotStyle, kSlotFontName);
        else
            gateway_.cloneStyleAsExpanded(baseStyle, slotStyle);
    }
}

void InlineIconLayer::clearRange(Sci_Position start, Sci_Position end) {
    if (end <= start) return;
    restoreSlotStyles(start, end);
}

void InlineIconLayer::clearAnchors() {
    if (currentView_)
        anchorsByView_[currentView_].clear();
}

void InlineIconLayer::clearDocumentAnchors(sptr_t document) {
    for (const auto& viewDocument : documentsByView_) {
        if (viewDocument.second != document) continue;
        const auto anchors = anchorsByView_.find(viewDocument.first);
        if (anchors != anchorsByView_.end())
            anchors->second.clear();
        ::InvalidateRect(viewDocument.first, nullptr, FALSE);
    }
}

bool InlineIconLayer::addIcon(Sci_Position line, Sci_Position slotPosition,
                              InlineIconSlot slot, const std::string& itemId) {
    if (!currentView_ || !registry_.hasIcon(itemId)) return false;
    if (!applySlotStyle(slotPosition, slot)) return false;
    anchorsByView_[currentView_].push_back({line, slotPosition, slot, itemId});
    return true;
}

void InlineIconLayer::paint(HWND scintilla) {
    const auto viewIt = anchorsByView_.find(scintilla);
    if (viewIt == anchorsByView_.end() || viewIt->second.empty()) return;

    RECT client{};
    if (!::GetClientRect(scintilla, &client)) return;

    // 复用成员 gateway_：避免每次绘制（高频，SCN_PAINTED 触发）重新通过
    // SendMessage 获取 SCI_GETDIRECTFUNCTION/POINTER。paint 仅读取坐标信息，
    // 不改写文本/style，切到 scintilla 视图不影响 Coordinator 的当前 attach 目标。
    ScintillaGateway& view = gateway_;
    view.attach(scintilla);
    const Sci_Position firstDisplay = (std::max)(0, view.getFirstVisibleLine());
    const int visibleLines = (std::max)(1, view.linesOnScreen());
    Sci_Position firstLine = view.docLineFromVisible(firstDisplay);
    Sci_Position lastLine = view.docLineFromVisible(firstDisplay + visibleLines);
    if (firstLine < 0) firstLine = 0;
    if (lastLine < firstLine)
        lastLine = firstLine + visibleLines;

    const AnchorList& anchors = viewIt->second;
    auto anchorIt = std::lower_bound(
        anchors.begin(), anchors.end(), firstLine,
        [](const InlineIconAnchor& anchor, Sci_Position line) {
            return anchor.line < line;
        });

    if (!registry_.ensureReady()) return;
    ScopedWindowDC windowDC(scintilla);
    if (!windowDC.get()) return;
    Gdiplus::Graphics graphics(windowDC.get());
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    Sci_Position cachedLine = -1;
    int lineTop = 0;
    int lineHeight = 0;
    for (; anchorIt != anchors.end() && anchorIt->line <= lastLine; ++anchorIt) {
        const int cellStart = view.pointXFromPosition(anchorIt->slotPosition);
        const int cellEnd = view.pointXFromPosition(anchorIt->slotPosition + 1);
        if (anchorIt->line != cachedLine) {
            cachedLine = anchorIt->line;
            lineTop = view.pointYFromPosition(anchorIt->slotPosition);
            lineHeight = view.textHeight(anchorIt->line);
        }
        if (cellEnd <= cellStart || lineHeight <= 0) continue;
        if (lineTop >= client.bottom || lineTop + lineHeight <= client.top) continue;

        int slotStart = cellStart;
        if (anchorIt->slot == InlineIconSlot::CompactColon) {
            const int glyphReserve = (std::min)(5, (cellEnd - cellStart) / 4);
            slotStart += glyphReserve;
        }
        const int slotWidth = cellEnd - slotStart;
        const int iconSize = (std::min)({MC_ICON_SIZE, slotWidth - 1, lineHeight - 2});
        if (iconSize < 4) continue;

        const int x = slotStart + (slotWidth - iconSize) / 2;
        const int y = lineTop + (lineHeight - iconSize) / 2;
        if (x >= client.right || x + iconSize <= client.left) continue;

        const IconImage* image = registry_.getIcon(anchorIt->itemId);
        if (!image || !image->drawable) continue;

        const Gdiplus::Rect destination(x, y, iconSize, iconSize);
        graphics.DrawImage(image->drawable.get(), destination, 0, 0,
                           image->width, image->height, Gdiplus::UnitPixel);
    }
}

void InlineIconLayer::refresh() const {
    if (currentView_)
        ::InvalidateRect(currentView_, nullptr, FALSE);
}

void InlineIconLayer::restoreSlotStyles(Sci_Position start, Sci_Position end) {
    Sci_Position pos = start;
    while (pos < end) {
        const int indicatorValue = gateway_.indicatorValueAt(INDIC_MC_ICON_SLOT, pos);
        Sci_Position rangeEnd = gateway_.indicatorEnd(INDIC_MC_ICON_SLOT, pos);
        if (rangeEnd <= pos) rangeEnd = pos + 1;
        if (rangeEnd > end) rangeEnd = end;

        if (indicatorValue != 0) {
            Sci_Position stylePos = pos;
            while (stylePos < rangeEnd) {
                const int style = gateway_.getStyleIndexAt(stylePos);
                Sci_Position styleEnd = stylePos + 1;
                while (styleEnd < rangeEnd &&
                       gateway_.getStyleIndexAt(styleEnd) == style) {
                    ++styleEnd;
                }

                if (style >= STYLE_MC_ICON_SLOT_BASE &&
                    style < STYLE_MC_ICON_SLOT_BASE + STYLE_MC_ICON_SLOT_COUNT) {
                    gateway_.startStyling(stylePos);
                    gateway_.setStyling(styleEnd - stylePos,
                                        style - STYLE_MC_ICON_SLOT_BASE);
                }
                stylePos = styleEnd;
            }
        }
        pos = rangeEnd;
    }

    gateway_.setIndicatorCurrent(INDIC_MC_ICON_SLOT);
    gateway_.indicatorClearRange(start, end - start);
}

bool InlineIconLayer::applySlotStyle(Sci_Position position, InlineIconSlot slot) {
    if (slot == InlineIconSlot::Tab)
        return true;

    int baseStyle = gateway_.getStyleIndexAt(position);
    if (baseStyle >= STYLE_MC_ICON_SLOT_BASE &&
        baseStyle < STYLE_MC_ICON_SLOT_BASE + STYLE_MC_ICON_SLOT_COUNT) {
        baseStyle -= STYLE_MC_ICON_SLOT_BASE;
    }
    if (baseStyle < 0 || baseStyle >= STYLE_MC_ICON_SLOT_COUNT)
        return false;

    gateway_.startStyling(position);
    gateway_.setStyling(1, STYLE_MC_ICON_SLOT_BASE + baseStyle);
    gateway_.setIndicatorCurrent(INDIC_MC_ICON_SLOT);
    gateway_.setIndicatorValue(1);
    gateway_.indicatorFillRange(position, 1);
    return true;
}
