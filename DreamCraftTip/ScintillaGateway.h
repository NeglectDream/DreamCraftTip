// ============================================================================
// ScintillaGateway — Scintilla 消息的薄封装层
//
// 职责（SRP）：隔离 Win32 SendMessage 细节，向其余模块提供类型安全的
//              文本 / style / indicator / 坐标 / margin 操作接口。
//
// 设计依据：首次 attach 通过 SendMessage 获取 Scintilla direct function，后续
//          高频调用在同一 UI 线程直接转发；若宿主不支持则自动退回 SendMessage。
//          业务模块仍不接触 wParam/lParam 细节。
//
// 线程模型：Scintilla 非线程安全，全部调用必须在 UI 线程。
// ============================================================================
#pragma once

#include <windows.h>
#include "sdk/Scintilla.h"
#include "sdk/Sci_Position.h"
#include <string>
#include <vector>

class ScintillaGateway {
public:
    explicit ScintillaGateway(HWND scintilla) noexcept;

    // 切换当前 Scintilla 句柄。
    //   buffer/视图切换时 Notepad++ 的活跃 Scintilla 可能从主切到副，
    //   Coordinator 在 scan 前调用以重定向全部后续消息。
    void attach(HWND scintilla) noexcept;

    // ---- 基础消息转发 ----
    LRESULT send(UINT msg, WPARAM wParam = 0, LPARAM lParam = 0) const noexcept;

    // ---- 文本与位置信息 ----
    Sci_Position getLength() const noexcept;            // SCI_GETLENGTH(2006)
    sptr_t       getDocumentPointer() const noexcept;    // SCI_GETDOCPOINTER(2357)
    int          getLineCount() const noexcept;         // SCI_GETLINECOUNT(2154)
    Sci_Position lineFromPosition(Sci_Position pos) const noexcept;   // 2166
    Sci_Position positionFromLine(Sci_Position line) const noexcept;  // 2167
    Sci_Position getLineEndPosition(Sci_Position line) const noexcept;// 2136
    int          getFirstVisibleLine() const noexcept;               // 2152（显示行）
    Sci_Position docLineFromVisible(Sci_Position displayLine) const noexcept; // 2221
    int          linesOnScreen() const noexcept;                     // 2370
    int          pointXFromPosition(Sci_Position pos) const noexcept;// 2164
    int          pointYFromPosition(Sci_Position pos) const noexcept;// 2165
    int          textHeight(Sci_Position line) const noexcept;       // 2279
    // 取 [start, end) 区间文本（UTF-8）。end=-1 表示到文档尾
    std::string  getTextRange(Sci_Position start, Sci_Position end) const;

    // ---- 字符 Style ----
    int  getStyleIndexAt(Sci_Position pos) const noexcept;              // 2038
    // 单次读取 [start, end) 的 style 索引，避免逐字符消息调用。
    std::vector<unsigned char> getStyleIndices(Sci_Position start, Sci_Position end) const;
    void colourise(Sci_Position start, Sci_Position end) const noexcept;// 4003
    void startStyling(Sci_Position start) const noexcept;               // 2032
    void setStyling(Sci_Position len, int style) const noexcept;        // 2033
    // 完整复制基础 style，并只调整目标属性。
    void cloneStyleAsBold(int sourceStyle, int targetStyle) const noexcept;
    void cloneStyleAsExpanded(int sourceStyle, int targetStyle) const noexcept;
    void cloneStyleWithFont(int sourceStyle, int targetStyle,
                            const char* fontName) const noexcept;

    // ---- Indicator 配置与操作 ----
    void indicSetStyle(int indicator, int style) const noexcept;       // 2080
    void indicSetFlags(int indicator, int flags) const noexcept;       // 2684
    void indicSetFore(int indicator, COLORREF color) const noexcept;   // 2082
    void indicSetAlpha(int indicator, int alpha) const noexcept;       // 2523
    void indicSetUnder(int indicator, bool under) const noexcept;      // 2510
    void setIndicatorCurrent(int indicator) const noexcept;            // 2500
    void setIndicatorValue(int value) const noexcept;                  // 2502
    void indicatorFillRange(Sci_Position start, Sci_Position len) const noexcept;   // 2504
    void indicatorClearRange(Sci_Position start, Sci_Position len) const noexcept;  // 2505
    int  indicatorValueAt(int indicator, Sci_Position pos) const noexcept;           // 2507
    Sci_Position indicatorStart(int indicator, Sci_Position pos) const noexcept;     // 2508
    Sci_Position indicatorEnd(int indicator, Sci_Position pos) const noexcept;       // 2509

    // ---- Margin ----
    // 仅用于关闭旧版本曾占用的图标 margin。
    void setMarginWidthN(int margin, int pixelWidth) const noexcept;   // 2242

private:
    void copyStyle(int sourceStyle, int targetStyle) const noexcept;

    HWND scintilla_ = nullptr;
    SciFnDirect directFunction_ = nullptr;
    sptr_t directPointer_ = 0;
};
