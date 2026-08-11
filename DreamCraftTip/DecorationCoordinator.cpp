// ============================================================================
// DecorationCoordinator 实现
// ============================================================================
#include "DecorationCoordinator.h"
#include "McConstants.h"
#include "McVanillaItems.h"
#include "sdk/Notepad_plus_msgs.h"
#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <string>
#include <utility>

namespace {

size_t leadingIndent(const std::string& line) {
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    return i;
}

size_t blockNodeIndent(const std::string& line) {
    size_t indent = leadingIndent(line);
    if (indent + 1 < line.size() && line[indent] == '-' &&
        (line[indent + 1] == ' ' || line[indent + 1] == '\t')) {
        indent += 2;
        while (indent < line.size() && (line[indent] == ' ' || line[indent] == '\t'))
            ++indent;
    }
    return indent;
}

int explicitBlockIndent(const std::string& value, size_t parentIndent) {
    if (value.empty() || (value[0] != '|' && value[0] != '>')) return -1;

    // Header 最多包含一个缩进数字和一个 chomping 符号（顺序可交换）。
    // 遇空白/注释即停止，绝不能从行尾注释中拾取数字。
    for (size_t i = 1, consumed = 0; i < value.size() && consumed < 2; ++i, ++consumed) {
        const char c = value[i];
        if (c >= '1' && c <= '9')
            return static_cast<int>(parentIndent) + (c - '0');
        if (c != '+' && c != '-')
            break;
    }
    return -1;
}

bool equalsIgnoreCase(const wchar_t* lhs, const wchar_t* rhs) noexcept {
    while (*lhs && *rhs) {
        if (std::towlower(*lhs) != std::towlower(*rhs)) return false;
        ++lhs;
        ++rhs;
    }
    return *lhs == *rhs;
}

struct IconSlotLocation {
    Sci_Position position = 0;
    InlineIconSlot slot = InlineIconSlot::Whitespace;
    bool valid = false;
};

IconSlotLocation locateIconSlot(const std::string& lineText, Sci_Position lineStart,
                                Sci_Position scalarStart) noexcept {
    if (scalarStart <= lineStart) return {};
    const size_t scalarOffset = static_cast<size_t>(scalarStart - lineStart);
    if (scalarOffset == 0 || scalarOffset > lineText.size()) return {};

    const char anchor = lineText[scalarOffset - 1];
    InlineIconSlot slot;
    if (anchor == ' ')
        slot = InlineIconSlot::Whitespace;
    else if (anchor == '\t')
        slot = InlineIconSlot::Tab;
    else if (anchor == ':')
        slot = InlineIconSlot::CompactColon;
    else
        return {};

    return {scalarStart - 1, slot, true};
}

} // namespace

DecorationCoordinator::DecorationCoordinator(NppData nppData, HMODULE hModule)
    : nppData_(nppData), hModule_(hModule) {
}

DecorationCoordinator::~DecorationCoordinator() = default;

void DecorationCoordinator::onReady() {
    if (ready_) return;

    const HWND scintilla = currentScintilla();
    if (!scintilla) return;

    gateway_ = std::make_unique<ScintillaGateway>(scintilla);
    colorIndicator_ = std::make_unique<ColorIndicator>(*gateway_);

    iconRegistry_.loadFromDirectory(resourcePath(L"icons"));
    auto vanillaIds = McVanillaItems::defaultSet();
    for (const auto& itemId : iconRegistry_.listIds())
        vanillaIds.insert(itemId);
    idMatcher_ = std::make_unique<McItemIdMatcher>(std::move(vanillaIds));
    inlineIcons_ = std::make_unique<InlineIconLayer>(
        *gateway_, iconRegistry_, resourcePath(L"InlineIconSlot.ttf"));

    // 使用对象地址作为窗口 timer ID，避免与 Notepad++ 的低位 timer ID 冲突。
    debouncer_ = std::make_unique<DebounceTimer>(
        nppData_._nppHandle, reinterpret_cast<UINT_PTR>(this), MC_DEBOUNCE_MS,
        [this] { if (ready_) scan(); });

    ready_ = true;
    scan();
}

void DecorationCoordinator::onFileChanged() {
    if (!ready_) return;
    if (debouncer_) debouncer_->cancel();
    scan();
}

void DecorationCoordinator::onModifiedDebounced() {
    if (ready_ && debouncer_)
        debouncer_->trigger();
}

void DecorationCoordinator::onViewportChanged() {
    if (!ready_ || !gateway_ || !debouncer_) return;
    const HWND scintilla = currentScintilla();
    if (!scintilla) return;
    gateway_->attach(scintilla);
    if (gateway_->getLength() > MC_MAX_SCAN_BYTES && isYamlFile())
        debouncer_->trigger();
}

void DecorationCoordinator::onPainted(HWND scintilla) {
    if (!ready_ || !inlineIcons_ || !scintilla) return;
    inlineIcons_->paint(scintilla);
}

HWND DecorationCoordinator::currentScintilla() const {
    int view = 0;
    ::SendMessage(nppData_._nppHandle, NPPM_GETCURRENTSCINTILLA, 0,
                  reinterpret_cast<LPARAM>(&view));
    return view == 0 ? nppData_._scintillaMainHandle
                     : nppData_._scintillaSecondHandle;
}

bool DecorationCoordinator::isYamlFile() const {
    int langType = L_TEXT;
    ::SendMessage(nppData_._nppHandle, NPPM_GETCURRENTLANGTYPE, 0,
                  reinterpret_cast<LPARAM>(&langType));
    if (langType == L_YAML)
        return true;

    wchar_t path[MAX_PATH]{};
    if (!::SendMessage(nppData_._nppHandle, NPPM_GETFULLCURRENTPATH, MAX_PATH,
                       reinterpret_cast<LPARAM>(path)))
        return false;

    const wchar_t* ext = std::wcsrchr(path, L'.');
    return ext && (equalsIgnoreCase(ext, L".yml") || equalsIgnoreCase(ext, L".yaml"));
}

std::wstring DecorationCoordinator::resourcePath(const wchar_t* name) const {
    if (!name || !*name) return {};

    wchar_t path[MAX_PATH]{};
    const DWORD len = ::GetModuleFileNameW(hModule_, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return {};

    std::wstring modulePath(path, len);
    const size_t slash = modulePath.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return {};
    return modulePath.substr(0, slash + 1) + name;
}

void DecorationCoordinator::scan() {
    if (!ready_ || !gateway_ || !colorIndicator_ || !inlineIcons_ || !idMatcher_)
        return;

    const HWND scintilla = currentScintilla();
    if (!scintilla) return;
    gateway_->attach(scintilla);

    // Style/indicator 定义属于 Scintilla 控件实例；主/副视图切换后需重同步。
    colorIndicator_->init();
    inlineIcons_->attach(scintilla);
    inlineIcons_->init();

    const Sci_Position docLen = gateway_->getLength();
    if (!isYamlFile()) {
        inlineIcons_->clearAll(docLen);
        colorIndicator_->clearAll(docLen);
        return;
    }

    inlineIcons_->clearAll(docLen);
    colorIndicator_->clearAll(docLen);
    if (docLen <= 0) return;

    const int lineCount = gateway_->getLineCount();
    if (lineCount <= 0) return;

    int startLine = 0;
    int endLine = lineCount - 1;

    // 大文件仅解析当前可见区，并在两端各保留 5000 字符上下文。
    if (docLen > MC_MAX_SCAN_BYTES) {
        const Sci_Position firstDisplay = (std::max)(0, gateway_->getFirstVisibleLine());
        const int visibleLines = (std::max)(1, gateway_->linesOnScreen());

        // GETFIRSTVISIBLELINE 返回显示行；换行/折叠开启时必须先转为文档行。
        Sci_Position firstDocLine = gateway_->docLineFromVisible(firstDisplay);
        if (firstDocLine < 0 || firstDocLine >= lineCount)
            firstDocLine = 0;

        const Sci_Position lastDisplay = firstDisplay + static_cast<Sci_Position>(visibleLines);
        Sci_Position lastDocLine = gateway_->docLineFromVisible(lastDisplay);
        if (lastDocLine < firstDocLine || lastDocLine >= lineCount) {
            lastDocLine = (std::min)(static_cast<Sci_Position>(lineCount - 1),
                                     firstDocLine + visibleLines);
        }

        const Sci_Position visibleStart = gateway_->positionFromLine(firstDocLine);
        const Sci_Position lineAfterVisible = lastDocLine + 1;
        const Sci_Position visibleEnd = lineAfterVisible < lineCount
            ? gateway_->positionFromLine(lineAfterVisible)
            : docLen;

        const Sci_Position scanStart = visibleStart > MC_VISIBLE_PADDING_CHARS
            ? visibleStart - MC_VISIBLE_PADDING_CHARS : 0;
        const Sci_Position remaining = docLen - visibleEnd;
        const Sci_Position scanEnd = remaining > MC_VISIBLE_PADDING_CHARS
            ? visibleEnd + MC_VISIBLE_PADDING_CHARS : docLen;

        startLine = static_cast<int>(gateway_->lineFromPosition(scanStart));
        endLine = static_cast<int>(gateway_->lineFromPosition(scanEnd));
        endLine = (std::min)(endLine, lineCount - 1);
    }

    int blockParentIndent = -1;
    int blockContentIndent = -1;
    for (int lineNo = startLine; lineNo <= endLine; ++lineNo) {
        const Sci_Position lineStart = gateway_->positionFromLine(lineNo);
        const Sci_Position lineEnd = gateway_->getLineEndPosition(lineNo);
        if (lineEnd <= lineStart) continue;

        const std::string lineText = gateway_->getTextRange(lineStart, lineEnd);
        const size_t indent = leadingIndent(lineText);
        const bool blankLine = indent == lineText.size();

        YamlValueRange valueRange{};
        bool blockBody = false;

        // 块标量正文：首个非空正文行确定自动缩进；后续低于该缩进即结束块。
        if (blockParentIndent >= 0) {
            if (blankLine) continue;  // 空行仍属于当前块，不终止状态
            const int currentIndent = static_cast<int>(indent);
            if (currentIndent <= blockParentIndent ||
                (blockContentIndent >= 0 && currentIndent < blockContentIndent)) {
                blockParentIndent = -1;
                blockContentIndent = -1;
            } else {
                if (blockContentIndent < 0)
                    blockContentIndent = currentIndent;
                valueRange.start = lineStart + static_cast<Sci_Position>(blockContentIndent);
                valueRange.end = lineEnd;
                valueRange.scalarStart = valueRange.start;
                valueRange.valid = valueRange.end > valueRange.start;
                blockBody = true;
            }
        }

        if (!blockBody)
            valueRange = locator_.locateValue(lineText, lineStart);
        if (!valueRange.valid || valueRange.end <= valueRange.start) continue;

        const std::string value = gateway_->getTextRange(valueRange.start, valueRange.end);
        if (!blockBody && !value.empty() && (value[0] == '|' || value[0] == '>')) {
            const size_t parentIndent = blockNodeIndent(lineText);
            blockParentIndent = static_cast<int>(parentIndent);
            blockContentIndent = explicitBlockIndent(value, parentIndent);
        }

        for (const auto& segment : colorLexer_.lex(value))
            colorIndicator_->paint(segment, valueRange.start);

        const ItemIdMatch match = idMatcher_->match(value);
        if (match.matched && iconRegistry_.hasIcon(match.itemId)) {
            // 裸 ID/namespace 格式沿用 scalarStart，确保 quoted value 仍可借用
            // opening quote 前的空白槽；仅字段对进入 value 内部并借用 ':' 槽。
            const Sci_Position iconTarget = match.sourceOffset == 0
                ? valueRange.scalarStart
                : valueRange.start + static_cast<Sci_Position>(match.sourceOffset);
            const IconSlotLocation location =
                locateIconSlot(lineText, lineStart, iconTarget);
            if (location.valid) {
                inlineIcons_->addIcon(lineNo, location.position,
                                      location.slot, match.itemId);
            }
        }
    }
    // 关键：扫描循环最后一次 applySlotStyle 会把 Scintilla 的 endStyled 游标
    // 拉回到"末个 slot position + 1"（通常落在末行中间，远小于 docLen）。
    // WM_PAINT 时 Editor 会调用 EnsureStyledTo(可见末尾)：
    //   if (endStyled < 可见末尾) Colourise(末行起始, 可见末尾)
    // 即从 endStyled 所在行起始重新运行 YAML lexer，覆盖末行的 slot style，
    // 使末行空格从 slot 宽字体退化为普通窄字体，paint() 测得的槽位宽度变小，
    // 图标随之变窄。非末行因 endStyled 已越过它们而不受影响——这正是
    // "仅末行图标变窄"的根因。
    //
    // 修复：把 endStyled 钉到 docLen，EnsureStyledTo 的条件永假，不再触发
    // 重着色，所有 slot style（含末行）原样保留。
    gateway_->startStyling(docLen);
    inlineIcons_->refresh();
}
