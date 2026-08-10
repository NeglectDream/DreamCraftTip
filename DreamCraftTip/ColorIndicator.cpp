// ============================================================================
// ColorIndicator 实现
// ============================================================================
#include "ColorIndicator.h"
#include "McConstants.h"
#include "sdk/Scintilla.h"

namespace {

bool isPluginBoldStyle(int style) noexcept {
    return style >= STYLE_MC_BOLD_BASE &&
           style < STYLE_MC_BOLD_BASE + STYLE_MC_BOLD_COUNT;
}

int baseStyleOf(int style) noexcept {
    return isPluginBoldStyle(style) ? style - STYLE_MC_BOLD_BASE : style;
}

bool isSupportedBaseStyle(int style) noexcept {
    return style >= 0 && style < STYLE_MC_BOLD_COUNT;
}

} // namespace

ColorIndicator::ColorIndicator(ScintillaGateway& gateway)
    : gateway_(gateway) {
}

void ColorIndicator::init() {
    // 颜色槽：INDIC_TEXTFORE + VALUEFORE → 单槽承载全 RGB。
    gateway_.indicSetStyle(INDIC_MC_COLOR, INDIC_TEXTFORE);
    gateway_.indicSetFlags(INDIC_MC_COLOR, SC_INDICFLAG_VALUEFORE);

    // 粗体 indicator 仅作为范围元数据；真实字重由字符 style 实现。
    gateway_.indicSetStyle(INDIC_MC_BOLD, INDIC_HIDDEN);
    gateway_.indicSetFlags(INDIC_MC_BOLD, SC_INDICFLAG_NONE);

    gateway_.indicSetStyle(INDIC_MC_ITALIC,    INDIC_SQUIGGLE);
    gateway_.indicSetStyle(INDIC_MC_UNDERLINE, INDIC_PLAIN);
    gateway_.indicSetStyle(INDIC_MC_STRIKE,    INDIC_STRIKE);
    gateway_.indicSetFore(INDIC_MC_ITALIC,    RGB(0x55, 0xFF, 0xFF));
    gateway_.indicSetFore(INDIC_MC_UNDERLINE, RGB(0x55, 0xFF, 0x55));
    gateway_.indicSetFore(INDIC_MC_STRIKE,    RGB(0xFF, 0x55, 0x55));
    gateway_.indicSetAlpha(INDIC_MC_ITALIC,    140);
    gateway_.indicSetAlpha(INDIC_MC_UNDERLINE, 140);
    gateway_.indicSetAlpha(INDIC_MC_STRIKE,    140);

    // YAML lexer 的 style 0..8 分别克隆到 240..248，仅提升字重。
    for (int baseStyle = 0; baseStyle < STYLE_MC_BOLD_COUNT; ++baseStyle) {
        gateway_.cloneStyleAsBold(baseStyle, STYLE_MC_BOLD_BASE + baseStyle);
    }
}

void ColorIndicator::clearAll(Sci_Position docLen) {
    // 隐藏 indicator 会随文本编辑自动移动，因此可用于准确恢复旧粗体区间。
    restoreBoldStyles(docLen);
    gateway_.colourise(0, docLen);

    const int indicators[] = {INDIC_MC_COLOR, INDIC_MC_BOLD, INDIC_MC_ITALIC,
                              INDIC_MC_UNDERLINE, INDIC_MC_STRIKE};
    for (int indicator : indicators) {
        gateway_.setIndicatorCurrent(indicator);
        gateway_.indicatorClearRange(0, docLen);
    }
}

void ColorIndicator::paint(const ColorSegment& seg, Sci_Position valueStart) {
    const Sci_Position absStart = valueStart + seg.start;
    const Sci_Position len = seg.length;
    if (len <= 0) return;

    if (seg.color != CLR_INVALID) {
        gateway_.setIndicatorCurrent(INDIC_MC_COLOR);
        const int value = SC_INDICVALUEBIT |
                          (static_cast<int>(seg.color) & SC_INDICVALUEMASK);
        gateway_.setIndicatorValue(value);
        gateway_.indicatorFillRange(absStart, len);
    }

    if (seg.bold)
        applyBoldStyle(absStart, len);

    if (seg.italic) {
        gateway_.setIndicatorCurrent(INDIC_MC_ITALIC);
        gateway_.indicatorFillRange(absStart, len);
    }
    if (seg.underline) {
        gateway_.setIndicatorCurrent(INDIC_MC_UNDERLINE);
        gateway_.indicatorFillRange(absStart, len);
    }
    if (seg.strike) {
        gateway_.setIndicatorCurrent(INDIC_MC_STRIKE);
        gateway_.indicatorFillRange(absStart, len);
    }
}

bool ColorIndicator::applyBoldStyle(Sci_Position start, Sci_Position len) {
    const Sci_Position end = start + len;
    Sci_Position pos = start;
    bool applied = false;

    while (pos < end) {
        const int rawStyle = gateway_.getStyleIndexAt(pos);
        Sci_Position runEnd = pos + 1;
        while (runEnd < end && gateway_.getStyleIndexAt(runEnd) == rawStyle)
            ++runEnd;

        const int baseStyle = baseStyleOf(rawStyle);
        if (isSupportedBaseStyle(baseStyle)) {
            gateway_.startStyling(pos);
            gateway_.setStyling(runEnd - pos, STYLE_MC_BOLD_BASE + baseStyle);
            applied = true;
        }
        pos = runEnd;
    }

    if (applied) {
        gateway_.setIndicatorCurrent(INDIC_MC_BOLD);
        gateway_.setIndicatorValue(1); // 0 表示无 indicator，必须使用非零元数据值。
        gateway_.indicatorFillRange(start, len);
    }
    return applied;
}

void ColorIndicator::restoreBoldStyles(Sci_Position docLen) {
    Sci_Position pos = 0;
    while (pos < docLen) {
        const int indicatorValue = gateway_.indicatorValueAt(INDIC_MC_BOLD, pos);
        Sci_Position rangeEnd = gateway_.indicatorEnd(INDIC_MC_BOLD, pos);
        if (rangeEnd <= pos) rangeEnd = pos + 1; // 防御异常返回，保证循环前进。
        if (rangeEnd > docLen) rangeEnd = docLen;

        if (indicatorValue != 0) {
            Sci_Position stylePos = pos;
            while (stylePos < rangeEnd) {
                const int rawStyle = gateway_.getStyleIndexAt(stylePos);
                Sci_Position styleEnd = stylePos + 1;
                while (styleEnd < rangeEnd && gateway_.getStyleIndexAt(styleEnd) == rawStyle)
                    ++styleEnd;

                if (isPluginBoldStyle(rawStyle)) {
                    gateway_.startStyling(stylePos);
                    gateway_.setStyling(styleEnd - stylePos, baseStyleOf(rawStyle));
                }
                stylePos = styleEnd;
            }
        }
        pos = rangeEnd;
    }
}
