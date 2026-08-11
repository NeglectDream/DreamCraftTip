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

void ColorIndicator::clearRange(Sci_Position start, Sci_Position end, bool recolourise) {
    if (end <= start) return;

    // 隐藏 indicator 会随文本编辑自动移动，因此可用于准确恢复旧粗体区间。
    restoreBoldStyles(start, end);
    if (recolourise)
        gateway_.colourise(start, end);

    const Sci_Position length = end - start;
    const int indicators[] = {INDIC_MC_COLOR, INDIC_MC_BOLD, INDIC_MC_ITALIC,
                              INDIC_MC_UNDERLINE, INDIC_MC_STRIKE};
    for (int indicator : indicators) {
        gateway_.setIndicatorCurrent(indicator);
        gateway_.indicatorClearRange(start, length);
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
    const std::vector<unsigned char> styles = gateway_.getStyleIndices(start, end);
    bool applied = false;

    size_t offset = 0;
    while (offset < styles.size()) {
        const int rawStyle = styles[offset];
        size_t runEnd = offset + 1;
        while (runEnd < styles.size() && styles[runEnd] == rawStyle)
            ++runEnd;

        const int baseStyle = baseStyleOf(rawStyle);
        if (isSupportedBaseStyle(baseStyle)) {
            gateway_.startStyling(start + static_cast<Sci_Position>(offset));
            gateway_.setStyling(static_cast<Sci_Position>(runEnd - offset),
                                STYLE_MC_BOLD_BASE + baseStyle);
            applied = true;
        }
        offset = runEnd;
    }

    if (applied) {
        gateway_.setIndicatorCurrent(INDIC_MC_BOLD);
        gateway_.setIndicatorValue(1); // 0 表示无 indicator，必须使用非零元数据值。
        gateway_.indicatorFillRange(start, len);
    }
    return applied;
}

void ColorIndicator::restoreBoldStyles(Sci_Position start, Sci_Position end) {
    Sci_Position pos = start;
    while (pos < end) {
        const int indicatorValue = gateway_.indicatorValueAt(INDIC_MC_BOLD, pos);
        Sci_Position rangeEnd = gateway_.indicatorEnd(INDIC_MC_BOLD, pos);
        if (rangeEnd <= pos) rangeEnd = pos + 1; // 防御异常返回，保证循环前进。
        if (rangeEnd > end) rangeEnd = end;

        if (indicatorValue != 0) {
            const std::vector<unsigned char> styles =
                gateway_.getStyleIndices(pos, rangeEnd);
            size_t offset = 0;
            while (offset < styles.size()) {
                const int rawStyle = styles[offset];
                size_t runEnd = offset + 1;
                while (runEnd < styles.size() && styles[runEnd] == rawStyle)
                    ++runEnd;

                if (isPluginBoldStyle(rawStyle)) {
                    gateway_.startStyling(pos + static_cast<Sci_Position>(offset));
                    gateway_.setStyling(static_cast<Sci_Position>(runEnd - offset),
                                        baseStyleOf(rawStyle));
                }
                offset = runEnd;
            }
        }
        pos = rangeEnd;
    }
}
