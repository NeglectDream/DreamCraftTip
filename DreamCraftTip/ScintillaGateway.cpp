// ============================================================================
// ScintillaGateway 实现
//
// 全部方法为对 Scintilla 消息的直接转发，参数与消息常量一一对应。
// 实现极简，无额外逻辑——这是封装层的本意：让上层不接触 Win32 细节。
// ============================================================================
#include "ScintillaGateway.h"

ScintillaGateway::ScintillaGateway(HWND scintilla) noexcept
    : scintilla_(scintilla) {
}

void ScintillaGateway::attach(HWND scintilla) noexcept {
    scintilla_ = scintilla;
}

LRESULT ScintillaGateway::send(UINT msg, WPARAM wParam, LPARAM lParam) const noexcept {
    return ::SendMessage(scintilla_, msg, wParam, lParam);
}

// ---- 文本与位置信息 ----
Sci_Position ScintillaGateway::getLength() const noexcept {
    return static_cast<Sci_Position>(send(SCI_GETLENGTH));
}

int ScintillaGateway::getLineCount() const noexcept {
    return static_cast<int>(send(SCI_GETLINECOUNT));
}

Sci_Position ScintillaGateway::lineFromPosition(Sci_Position pos) const noexcept {
    return static_cast<Sci_Position>(send(SCI_LINEFROMPOSITION, static_cast<WPARAM>(pos)));
}

Sci_Position ScintillaGateway::positionFromLine(Sci_Position line) const noexcept {
    return static_cast<Sci_Position>(send(SCI_POSITIONFROMLINE, static_cast<WPARAM>(line)));
}

Sci_Position ScintillaGateway::getLineEndPosition(Sci_Position line) const noexcept {
    return static_cast<Sci_Position>(send(SCI_GETLINEENDPOSITION, static_cast<WPARAM>(line)));
}

int ScintillaGateway::getFirstVisibleLine() const noexcept {
    return static_cast<int>(send(SCI_GETFIRSTVISIBLELINE));
}

Sci_Position ScintillaGateway::docLineFromVisible(Sci_Position displayLine) const noexcept {
    return static_cast<Sci_Position>(send(SCI_DOCLINEFROMVISIBLE,
                                          static_cast<WPARAM>(displayLine)));
}

int ScintillaGateway::linesOnScreen() const noexcept {
    return static_cast<int>(send(SCI_LINESONSCREEN));
}

int ScintillaGateway::pointXFromPosition(Sci_Position pos) const noexcept {
    return static_cast<int>(send(SCI_POINTXFROMPOSITION, 0, static_cast<LPARAM>(pos)));
}

int ScintillaGateway::pointYFromPosition(Sci_Position pos) const noexcept {
    return static_cast<int>(send(SCI_POINTYFROMPOSITION, 0, static_cast<LPARAM>(pos)));
}

int ScintillaGateway::textHeight(Sci_Position line) const noexcept {
    return static_cast<int>(send(SCI_TEXTHEIGHT, static_cast<WPARAM>(line)));
}

std::string ScintillaGateway::getTextRange(Sci_Position start, Sci_Position end) const {
    if (end < 0)
        end = getLength();
    if (start < 0)
        start = 0;
    if (end <= start)
        return {};

    const size_t len = static_cast<size_t>(end - start);
    std::string buf(len + 1, '\0');           // +1 预留给 NUL
    Sci_TextRangeFull tr{};
    tr.chrg.cpMin = start;
    tr.chrg.cpMax = end;
    tr.lpstrText  = &buf[0];
    send(SCI_GETTEXTRANGEFULL, 0, reinterpret_cast<LPARAM>(&tr));
    buf.resize(len);                            // 按请求长度截断，丢弃尾部 NUL
    return buf;
}

// ---- 字符 Style ----
int ScintillaGateway::getStyleIndexAt(Sci_Position pos) const noexcept {
    return static_cast<int>(send(SCI_GETSTYLEINDEXAT, static_cast<WPARAM>(pos)));
}

void ScintillaGateway::colourise(Sci_Position start, Sci_Position end) const noexcept {
    send(SCI_COLOURISE, static_cast<WPARAM>(start), static_cast<LPARAM>(end));
}

void ScintillaGateway::startStyling(Sci_Position start) const noexcept {
    send(SCI_STARTSTYLING, static_cast<WPARAM>(start));
}

void ScintillaGateway::setStyling(Sci_Position len, int style) const noexcept {
    send(SCI_SETSTYLING, static_cast<WPARAM>(len), static_cast<LPARAM>(style));
}

void ScintillaGateway::copyStyle(int sourceStyle, int targetStyle) const noexcept {
    auto copyProperty = [this, sourceStyle, targetStyle](UINT getMessage, UINT setMessage) {
        send(setMessage, targetStyle, send(getMessage, sourceStyle));
    };

    copyProperty(SCI_STYLEGETFORE,         SCI_STYLESETFORE);
    copyProperty(SCI_STYLEGETBACK,         SCI_STYLESETBACK);
    copyProperty(SCI_STYLEGETITALIC,       SCI_STYLESETITALIC);
    copyProperty(SCI_STYLEGETEOLFILLED,    SCI_STYLESETEOLFILLED);
    copyProperty(SCI_STYLEGETUNDERLINE,    SCI_STYLESETUNDERLINE);
    copyProperty(SCI_STYLEGETCASE,         SCI_STYLESETCASE);
    copyProperty(SCI_STYLEGETSIZEFRACTIONAL, SCI_STYLESETSIZEFRACTIONAL);
    copyProperty(SCI_STYLEGETCHARACTERSET, SCI_STYLESETCHARACTERSET);
    copyProperty(SCI_STYLEGETVISIBLE,        SCI_STYLESETVISIBLE);
    copyProperty(SCI_STYLEGETCHANGEABLE,     SCI_STYLESETCHANGEABLE);
    copyProperty(SCI_STYLEGETHOTSPOT,        SCI_STYLESETHOTSPOT);
    copyProperty(SCI_STYLEGETCHECKMONOSPACED, SCI_STYLESETCHECKMONOSPACED);
    copyProperty(SCI_STYLEGETSTRETCH,        SCI_STYLESETSTRETCH);

    char fontName[128]{};
    send(SCI_STYLEGETFONT, sourceStyle, reinterpret_cast<LPARAM>(fontName));
    send(SCI_STYLESETFONT, targetStyle, reinterpret_cast<LPARAM>(fontName));

    char invisibleRepresentation[256]{};
    send(SCI_STYLEGETINVISIBLEREPRESENTATION, sourceStyle,
         reinterpret_cast<LPARAM>(invisibleRepresentation));
    send(SCI_STYLESETINVISIBLEREPRESENTATION, targetStyle,
         reinterpret_cast<LPARAM>(invisibleRepresentation));

    send(SCI_STYLESETWEIGHT, targetStyle, send(SCI_STYLEGETWEIGHT, sourceStyle));
}

void ScintillaGateway::cloneStyleAsBold(int sourceStyle, int targetStyle) const noexcept {
    copyStyle(sourceStyle, targetStyle);
    int weight = static_cast<int>(send(SCI_STYLEGETWEIGHT, sourceStyle));
    if (weight < SC_WEIGHT_BOLD) weight = SC_WEIGHT_BOLD;
    send(SCI_STYLESETWEIGHT, targetStyle, weight);
}

void ScintillaGateway::cloneStyleAsExpanded(int sourceStyle, int targetStyle) const noexcept {
    copyStyle(sourceStyle, targetStyle);
    send(SCI_STYLESETSTRETCH, targetStyle, SC_STRETCH_ULTRA_EXPANDED);
}

void ScintillaGateway::cloneStyleWithFont(int sourceStyle, int targetStyle,
                                          const char* fontName) const noexcept {
    copyStyle(sourceStyle, targetStyle);
    send(SCI_STYLESETFONT, targetStyle, reinterpret_cast<LPARAM>(fontName));
    send(SCI_STYLESETSTRETCH, targetStyle, SC_STRETCH_NORMAL);
}

// ---- Indicator 配置与操作 ----
void ScintillaGateway::indicSetStyle(int indicator, int style) const noexcept {
    send(SCI_INDICSETSTYLE, indicator, style);
}
void ScintillaGateway::indicSetFlags(int indicator, int flags) const noexcept {
    send(SCI_INDICSETFLAGS, indicator, flags);
}
void ScintillaGateway::indicSetFore(int indicator, COLORREF color) const noexcept {
    send(SCI_INDICSETFORE, indicator, static_cast<LPARAM>(color));
}
void ScintillaGateway::indicSetAlpha(int indicator, int alpha) const noexcept {
    send(SCI_INDICSETALPHA, indicator, alpha);
}
void ScintillaGateway::indicSetUnder(int indicator, bool under) const noexcept {
    send(SCI_INDICSETUNDER, indicator, under ? 1 : 0);
}
void ScintillaGateway::setIndicatorCurrent(int indicator) const noexcept {
    send(SCI_SETINDICATORCURRENT, indicator);
}
void ScintillaGateway::setIndicatorValue(int value) const noexcept {
    send(SCI_SETINDICATORVALUE, value);
}
void ScintillaGateway::indicatorFillRange(Sci_Position start, Sci_Position len) const noexcept {
    send(SCI_INDICATORFILLRANGE, static_cast<WPARAM>(start), static_cast<LPARAM>(len));
}
void ScintillaGateway::indicatorClearRange(Sci_Position start, Sci_Position len) const noexcept {
    send(SCI_INDICATORCLEARRANGE, static_cast<WPARAM>(start), static_cast<LPARAM>(len));
}
int ScintillaGateway::indicatorValueAt(int indicator, Sci_Position pos) const noexcept {
    return static_cast<int>(send(SCI_INDICATORVALUEAT, indicator, static_cast<LPARAM>(pos)));
}
Sci_Position ScintillaGateway::indicatorStart(int indicator, Sci_Position pos) const noexcept {
    return static_cast<Sci_Position>(send(SCI_INDICATORSTART, indicator, static_cast<LPARAM>(pos)));
}
Sci_Position ScintillaGateway::indicatorEnd(int indicator, Sci_Position pos) const noexcept {
    return static_cast<Sci_Position>(send(SCI_INDICATOREND, indicator, static_cast<LPARAM>(pos)));
}

// ---- Margin ----
void ScintillaGateway::setMarginWidthN(int margin, int pixelWidth) const noexcept {
    send(SCI_SETMARGINWIDTHN, margin, pixelWidth);
}
