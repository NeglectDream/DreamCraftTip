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

    iconRegistry_.indexDirectory(resourcePath(L"icons"));
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
    pendingViews_.insert(scintilla);
    debouncer_->triggerAfter(MC_FILE_EVENT_DEBOUNCE_MS);
}

void DecorationCoordinator::onFileChanged() {
    if (!ready_ || !gateway_ || !inlineIcons_ || !debouncer_) return;
    const HWND scintilla = currentScintilla();
    if (!scintilla) return;

    // 立即移除新 buffer 上可能属于旧文档的视图锚点，实际扫描由通知合并器执行。
    gateway_->attach(scintilla);
    inlineIcons_->attach(scintilla);
    inlineIcons_->clearAnchors();
    inlineIcons_->refresh();
    pendingViews_.clear();
    pendingViews_.insert(scintilla);
    debouncer_->triggerAfter(MC_FILE_EVENT_DEBOUNCE_MS);
}

void DecorationCoordinator::onStylesChanged() {
    if (!ready_ || !gateway_ || !colorIndicator_ || !inlineIcons_ || !debouncer_) return;

    // 主题/样式变更：两视图的 indicator/slot 定义都必须重建，且两视图都要重扫。
    // 原实现把排队委托给 onFileChanged()，但后者只排队 currentScintilla，
    // 导致副视图虽被 init() 却不会进入 pendingViews_，scan() 遗漏它，旧装饰残留。
    initializedViews_.clear();
    pendingViews_.clear();
    const HWND views[] = {
        nppData_._scintillaMainHandle,
        nppData_._scintillaSecondHandle,
    };
    for (const HWND view : views) {
        if (!view) continue;
        gateway_->attach(view);
        inlineIcons_->attach(view);
        colorIndicator_->init();
        inlineIcons_->init();
        initializedViews_.insert(view);
        pendingViews_.insert(view);
    }
    debouncer_->triggerAfter(MC_FILE_EVENT_DEBOUNCE_MS);
}

void DecorationCoordinator::onModifiedDebounced(
    HWND scintilla, Sci_Position position, Sci_Position length, int modificationType) {
    if (!ready_ || !gateway_ || !inlineIcons_ || !debouncer_ || !scintilla) return;
    gateway_->attach(scintilla);
    inlineIcons_->attach(scintilla);
    const sptr_t document = gateway_->getDocumentPointer();
    inlineIcons_->clearDocumentAnchors(document);

    // 克隆视图共享文档内容但拥有独立 style/锚点；一次编辑必须同步重建两边。
    pendingViews_.clear();
    pendingViews_.insert(scintilla);
    const HWND views[] = {
        nppData_._scintillaMainHandle,
        nppData_._scintillaSecondHandle,
    };
    for (const HWND view : views) {
        if (!view || view == scintilla) continue;
        gateway_->attach(view);
        if (gateway_->getDocumentPointer() == document)
            pendingViews_.insert(view);
    }

    // Indicator 会随编辑自动移动，但缓存的数字范围不会；按同一编辑变换范围，
    // 防止防抖期间在旧窗口边界外遗留宽槽 style 或颜色 indicator。
    if (length > 0) {
        for (const HWND view : pendingViews_) {
            const auto range = decoratedRanges_.find({view, document});
            if (range == decoratedRanges_.end()) continue;

            DecoratedRange& decorated = range->second;
            if (modificationType & SC_MOD_INSERTTEXT) {
                if (position <= decorated.start) {
                    decorated.start += length;
                    decorated.end += length;
                } else if (position < decorated.end) {
                    decorated.end += length;
                }
            } else if (modificationType & SC_MOD_DELETETEXT) {
                const Sci_Position deletionEnd = position + length;
                const auto translate = [position, deletionEnd, length](Sci_Position value) {
                    if (value <= position) return value;
                    if (value < deletionEnd) return position;
                    return value - length;
                };
                decorated.start = translate(decorated.start);
                decorated.end = translate(decorated.end);
                if (decorated.end < decorated.start)
                    decorated.end = decorated.start;
            }
        }
    }

    gateway_->attach(scintilla);
    debouncer_->trigger();
}

void DecorationCoordinator::onViewportChanged(HWND scintilla) {
    if (!ready_ || !gateway_ || !debouncer_ || !scintilla) return;
    gateway_->attach(scintilla);
    if (gateway_->getLength() > MC_MAX_SCAN_BYTES && isYamlFile()) {
        pendingViews_.clear();
        pendingViews_.insert(scintilla);
        debouncer_->trigger();
    }
}

void DecorationCoordinator::onPainted(HWND scintilla) {
    if (!ready_ || !inlineIcons_ || !scintilla) return;
    // InlineIconLayer::paint 复用 Coordinator 注入的 gateway_，而 gateway_ 的
    // 当前 attach 目标由 Coordinator 控制（扫描/通知路径上已切到对应视图）。
    // 但 SCN_PAINTED 可能在 Coordinator 未重定向 gateway_ 的视图上触发；
    // 防御性地在 paint 内自行 attach 到目标 scintilla，避免读到错误视图的坐标。
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

    if (pendingViews_.empty()) {
        const HWND scintilla = currentScintilla();
        if (scintilla)
            pendingViews_.insert(scintilla);
    }

    const auto views = pendingViews_;
    pendingViews_.clear();
    const bool yamlFile = isYamlFile();
    for (const HWND scintilla : views)
        scanView(scintilla, yamlFile);
}

void DecorationCoordinator::scanView(HWND scintilla, bool yamlFile) {
    if (!scintilla) return;
    gateway_->attach(scintilla);

    // Style/indicator 定义属于 Scintilla 控件实例；每个视图只初始化一次。
    // NPPN_WORDSTYLESUPDATED 会清空 initializedViews_，届时按需重建。
    inlineIcons_->attach(scintilla);
    if (initializedViews_.insert(scintilla).second) {
        colorIndicator_->init();
        inlineIcons_->init();
    }

    const Sci_Position docLen = gateway_->getLength();
    const sptr_t document = gateway_->getDocumentPointer();
    // 每个 buffer 保留独立旧范围；切走后再返回时仍能清除上次可见窗口的装饰。
    // 扁平化键替代原二级 map，避免 operator[] 在早退路径上留下空内层条目。
    auto previousIt = decoratedRanges_.find({scintilla, document});
    inlineIcons_->clearAnchors();
    if (!yamlFile) {
        // 只恢复本插件确实装饰过的范围；首次看到普通文件时绝不改写其 style。
        if (previousIt != decoratedRanges_.end()) {
            clearDecorationRange(previousIt->second.start, previousIt->second.end, docLen, false);
            decoratedRanges_.erase(previousIt);
        }
        inlineIcons_->refresh();
        return;
    }
    if (docLen <= 0) {
        if (previousIt != decoratedRanges_.end())
            decoratedRanges_.erase(previousIt);
        inlineIcons_->refresh();
        return;
    }

    const int lineCount = gateway_->getLineCount();
    if (lineCount <= 0) {
        inlineIcons_->refresh();
        return;
    }

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

        const Sci_Position paddedStart = visibleStart > MC_VISIBLE_PADDING_CHARS
            ? visibleStart - MC_VISIBLE_PADDING_CHARS : 0;
        const Sci_Position remaining = docLen - visibleEnd;
        const Sci_Position paddedEnd = remaining > MC_VISIBLE_PADDING_CHARS
            ? visibleEnd + MC_VISIBLE_PADDING_CHARS : docLen;

        startLine = static_cast<int>(gateway_->lineFromPosition(paddedStart));
        endLine = static_cast<int>(gateway_->lineFromPosition(paddedEnd));
        endLine = (std::min)(endLine, lineCount - 1);
    }

    const Sci_Position scanStart = gateway_->positionFromLine(startLine);
    const Sci_Position lineAfterScan = static_cast<Sci_Position>(endLine) + 1;
    const Sci_Position scanEnd = lineAfterScan < lineCount
        ? gateway_->positionFromLine(lineAfterScan)
        : docLen;

    // 先清理该视图/文档离开窗口的旧范围，避免残留扩宽槽位和颜色 indicator。
    // 旧 [old.start, old.end) 与新 [scanStart, scanEnd) 的差集最多两段：左差与右差。
    // 重叠部分随后由新范围整体清除，这里只清理"不再覆盖"的边缘，避免重复 SendMessage。
    if (previousIt != decoratedRanges_.end() &&
        (previousIt->second.start != scanStart || previousIt->second.end != scanEnd)) {
        const Sci_Position oldStart = previousIt->second.start;
        const Sci_Position oldEnd = previousIt->second.end;
        const Sci_Position leftEnd = (std::min)(oldEnd, scanStart);
        const Sci_Position rightStart = (std::max)(oldStart, scanEnd);
        clearDecorationRange(oldStart, leftEnd, docLen, false);
        clearDecorationRange(rightStart, oldEnd, docLen, false);
    }
    clearDecorationRange(scanStart, scanEnd, docLen, true);
    decoratedRanges_[{scintilla, document}] = {scanStart, scanEnd};

    // 整个扫描窗口只跨一次 Scintilla 边界，后续拆行和 value 提取均在内存完成。
    const std::string scanText = gateway_->getTextRange(scanStart, scanEnd);
    // 扫描窗口内的 style 一次取回：原循环每行发一次 SCI_GETSTYLEINDEXAT，
    // 大文件可视区约 50-200 行 → 50-200 次 SendMessage；改为单次 SCI_GETSTYLEDTEXTFULL
    // 后 style 读取降为 O(1) 数组索引，与 ColorIndicator 已有优化同构。
    const std::vector<unsigned char> styleIndices =
        gateway_->getStyleIndices(scanStart, scanEnd);
    int blockParentIndent = -1;
    int blockContentIndent = -1;
    size_t lineOffset = 0;
    int lineNo = startLine;
    while (lineOffset < scanText.size() && lineNo <= endLine) {
        size_t contentEnd = scanText.find_first_of("\r\n", lineOffset);
        if (contentEnd == std::string::npos)
            contentEnd = scanText.size();

        size_t nextLineOffset = contentEnd;
        if (nextLineOffset < scanText.size() && scanText[nextLineOffset] == '\r')
            ++nextLineOffset;
        if (nextLineOffset < scanText.size() && scanText[nextLineOffset] == '\n')
            ++nextLineOffset;

        const int currentLine = lineNo++;
        const Sci_Position lineStart = scanStart + static_cast<Sci_Position>(lineOffset);
        const Sci_Position lineEnd = scanStart + static_cast<Sci_Position>(contentEnd);
        const std::string lineText = scanText.substr(lineOffset, contentEnd - lineOffset);
        lineOffset = nextLineOffset;
        if (lineText.empty()) continue;

        const size_t indent = leadingIndent(lineText);
        const bool blankLine = indent == lineText.size();

        YamlValueRange valueRange{};
        bool blockBody = false;

        // YAML lexer 已知该行是块标量正文时直接使用；即使扫描窗口从超长
        // 块正文中部开始，也无需依赖 padding 内能找到原始 |/> 块头。
        const Sci_Position firstContent = lineStart + static_cast<Sci_Position>(indent);
        const size_t firstContentOffset = static_cast<size_t>(firstContent - scanStart);
        const bool blockTextStyled =
            !blankLine
            && firstContentOffset < styleIndices.size()
            && styleIndices[firstContentOffset] == STYLE_YAML_BLOCK_TEXT;
        if (blockTextStyled) {
            valueRange.start = firstContent;
            valueRange.end = lineEnd;
            valueRange.scalarStart = valueRange.start;
            valueRange.valid = valueRange.end > valueRange.start;
            blockBody = true;
        } else if (blockParentIndent >= 0) {
            // 编辑中的未完成 YAML 可能尚无稳定 lexer style，保留缩进状态机兜底。
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

        const size_t valueOffset = static_cast<size_t>(valueRange.start - lineStart);
        const size_t valueLength = static_cast<size_t>(valueRange.end - valueRange.start);
        if (valueOffset > lineText.size() || valueLength > lineText.size() - valueOffset)
            continue;
        const std::string value = lineText.substr(valueOffset, valueLength);
        if (!blockBody && !value.empty() && (value[0] == '|' || value[0] == '>')) {
            const size_t parentIndent = blockNodeIndent(lineText);
            blockParentIndent = static_cast<int>(parentIndent);
            blockContentIndent = explicitBlockIndent(value, parentIndent);
        }

        for (const auto& segment : colorLexer_.lex(value))
            colorIndicator_->paint(segment, valueRange.start);

        const ItemIdMatch match = idMatcher_->match(value);
        if (match.matched) {
            // 裸 ID/namespace 格式沿用 scalarStart，确保 quoted value 仍可借用
            // opening quote 前的空白槽；仅字段对进入 value 内部并借用 ':' 槽。
            const Sci_Position iconTarget = match.sourceOffset == 0
                ? valueRange.scalarStart
                : valueRange.start + static_cast<Sci_Position>(match.sourceOffset);
            const IconSlotLocation location =
                locateIconSlot(lineText, lineStart, iconTarget);
            if (location.valid) {
                inlineIcons_->addIcon(currentLine, location.position,
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

void DecorationCoordinator::clearDecorationRange(
    Sci_Position start, Sci_Position end, Sci_Position docLen, bool recolourise) {
    // 统一 clamp 到 [0, docLen]；空区间直接返回，与 InlineIconLayer/ColorIndicator
    // 内部对 (end <= start) 的容忍保持一致。recolourise 仅在即将重扫时启用，
    // 让 ColorIndicator 借机触发 SCI_COLOURISE / StardDGuard 收尾。
    const Sci_Position clampedStart = (std::max)(static_cast<Sci_Position>(0), start);
    const Sci_Position clampedEnd = (std::min)(docLen, end);
    if (clampedEnd <= clampedStart) return;
    inlineIcons_->clearRange(clampedStart, clampedEnd);
    colorIndicator_->clearRange(clampedStart, clampedEnd, recolourise);
}
