// ============================================================================
// McColorLexer 实现 — 单遍扫描状态机
//
// 结构：两个纯解析函数 parseAmpCode / parseTag 识别码字并返回状态变化
//      （三态：+1 开启 / -1 关闭 / 0 不变），主循环用 emit() 切段。
//      开启码字归入新状态；闭合标签归入被关闭的当前状态，再恢复外层状态。
// ============================================================================
#include "McColorLexer.h"
#include <cctype>
#include <string_view>
#include <utility>

namespace {

// 传统颜色码 0-9, a-f, A-F → COLORREF（RGB 宏生成 0x00BBGGRR）
COLORREF legacyColor(char code) {
    static const COLORREF kTable[16] = {
        RGB(0x00,0x00,0x00), RGB(0x00,0x00,0xAA), RGB(0x00,0xAA,0x00), RGB(0x00,0xAA,0xAA),
        RGB(0xAA,0x00,0x00), RGB(0xAA,0x00,0xAA), RGB(0xFF,0xAA,0x00), RGB(0xAA,0xAA,0xAA),
        RGB(0x55,0x55,0x55), RGB(0x55,0x55,0xFF), RGB(0x55,0xFF,0x55), RGB(0x55,0xFF,0xFF),
        RGB(0xFF,0x55,0x55), RGB(0xFF,0x55,0xFF), RGB(0xFF,0xFF,0x55), RGB(0xFF,0xFF,0xFF),
    };
    if (code >= '0' && code <= '9') return kTable[code - '0'];
    if (code >= 'a' && code <= 'f') return kTable[10 + code - 'a'];
    if (code >= 'A' && code <= 'F') return kTable[10 + code - 'A'];
    return CLR_INVALID;
}

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// 解析 6 位 hex（s 指向首字节）→ COLORREF；非法返回 CLR_INVALID
COLORREF parseHex6(const char* s) {
    int vals[6];
    for (int k = 0; k < 6; ++k) {
        vals[k] = hexVal(s[k]);
        if (vals[k] < 0) return CLR_INVALID;
    }
    return RGB(vals[0] * 16 + vals[1], vals[2] * 16 + vals[3], vals[4] * 16 + vals[5]);
}

char lowerChar(char c) noexcept {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool equalsIgnoreCase(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) return false;
    for (size_t i = 0; i < left.size(); ++i) {
        if (lowerChar(left[i]) != lowerChar(right[i])) return false;
    }
    return true;
}

// MiniMessage 命名颜色表。使用 string_view 避免为标签名分配字符串。
COLORREF namedColor(std::string_view name) {
    struct NamedColor {
        std::string_view name;
        COLORREF color = CLR_INVALID;
    };
    static constexpr NamedColor kTable[] = {
        {"black",RGB(0,0,0)},         {"dark_blue",RGB(0,0,170)},
        {"dark_green",RGB(0,170,0)},  {"dark_aqua",RGB(0,170,170)},
        {"dark_red",RGB(170,0,0)},    {"dark_purple",RGB(170,0,170)},
        {"gold",RGB(255,170,0)},      {"gray",RGB(170,170,170)},
        {"dark_gray",RGB(85,85,85)},  {"blue",RGB(85,85,255)},
        {"green",RGB(85,255,85)},     {"aqua",RGB(85,255,255)},
        {"red",RGB(255,85,85)},       {"light_purple",RGB(255,85,255)},
        {"yellow",RGB(255,255,85)},   {"white",RGB(255,255,255)},
    };
    for (const auto& entry : kTable)
        if (equalsIgnoreCase(name, entry.name)) return entry.color;
    if (equalsIgnoreCase(name, "grey")) return RGB(170,170,170);
    if (equalsIgnoreCase(name, "dark_grey")) return RGB(85,85,85);
    return CLR_INVALID;
}

COLORREF parseColorArgument(std::string_view argument) {
    if (argument.size() >= 2 &&
        ((argument.front() == '\'' && argument.back() == '\'') ||
         (argument.front() == '"' && argument.back() == '"'))) {
        argument.remove_prefix(1);
        argument.remove_suffix(1);
    }
    if (argument.size() == 7 && argument[0] == '#')
        return parseHex6(argument.data() + 1);
    return namedColor(argument);
}

enum class StackAction { None, Push, Pop };

// 码字解析结果：legacy &/§ 颜色码会切换颜色并清除当前修饰；
// MiniMessage 标签用 Push/Pop 恢复外层状态。
struct CodeParse {
    bool        matched = false;
    size_t      end = 0;
    bool        colorChanged = false;
    bool        clearDecorations = false;
    COLORREF    color = CLR_INVALID;
    std::string_view tagName;
    int         bold = 0, italic = 0, underline = 0, strike = 0;
    StackAction colorStack = StackAction::None;
    StackAction boldStack = StackAction::None;
    StackAction italicStack = StackAction::None;
    StackAction underlineStack = StackAction::None;
    StackAction strikeStack = StackAction::None;
    bool        reset = false;
};

bool closesScope(const CodeParse& code) noexcept {
    return code.colorStack == StackAction::Pop ||
           code.boldStack == StackAction::Pop ||
           code.italicStack == StackAction::Pop ||
           code.underlineStack == StackAction::Pop ||
           code.strikeStack == StackAction::Pop;
}

// 解析 & / § 码字。'&' 占 1 字节；'§' 在 UTF-8 占 2 字节（0xC2 0xA7）
CodeParse parseAmpCode(const std::string& v, size_t i) {
    CodeParse r;
    const size_t n = v.size();
    size_t symLen = 0;
    if (v[i] == '&') symLen = 1;
    else if (v[i] == '\xC2' && i + 1 < n && v[i + 1] == '\xA7') symLen = 2;
    else return r;

    const size_t j = i + symLen;
    if (j >= n) return r;

    // &#RRGGBB / §#RRGGBB
    if (v[j] == '#' && j + 6 < n) {
        COLORREF hex = parseHex6(v.data() + j + 1);
        if (hex != CLR_INVALID) {
            r.matched = true;
            r.end = j + 7;           // 符号 + '#' + 6 hex
            r.colorChanged = true;
            r.clearDecorations = true;
            r.color = hex;
            return r;
        }
    }

    // 传统颜色码
    COLORREF lc = legacyColor(v[j]);
    if (lc != CLR_INVALID) {
        r.matched = true; r.end = j + 1;
        r.colorChanged = true;
        r.clearDecorations = true;
        r.color = lc;
        return r;
    }

    // 修饰码（大小写不敏感）
    const char low = static_cast<char>(std::tolower(static_cast<unsigned char>(v[j])));
    r.matched = true; r.end = j + 1;
    switch (low) {
        case 'l': r.bold = 1;      break;
        case 'n': r.underline = 1; break;
        case 'o': r.italic = 1;    break;
        case 'm': r.strike = 1;    break;
        case 'r': r.reset = true;  break;
        case 'k': break;           // 随机码，忽略视觉
        default:  r.matched = false; // 未知码，& 当普通字符
    }
    return r;
}

// 解析 MiniMessage <...> 标签。遇到下一个 '<' 时立即判定当前标签未闭合，
// 使连续或大量未闭合 '<' 的总扫描量保持线性。
CodeParse parseTag(const std::string& v, size_t i) {
    CodeParse r;
    if (v[i] != '<') return r;
    const size_t close = v.find_first_of("<>", i + 1);
    if (close == std::string::npos || v[close] != '>') return r;

    std::string_view tag(v.data() + i + 1, close - i - 1);
    bool closing = false;
    if (!tag.empty() && tag.front() == '/') {
        closing = true;
        tag.remove_prefix(1);
    }
    if (tag.empty()) return r;

    r.matched = true;
    r.end = close + 1;

    const size_t argumentSeparator = tag.find(':');
    const std::string_view verboseName = argumentSeparator == std::string_view::npos
        ? tag : tag.substr(0, argumentSeparator);
    if (equalsIgnoreCase(verboseName, "color") ||
        equalsIgnoreCase(verboseName, "colour") ||
        equalsIgnoreCase(verboseName, "c")) {
        r.tagName = "color";
        if (closing) {
            r.colorStack = StackAction::Pop;
            return r;
        }
        if (argumentSeparator == std::string_view::npos ||
            argumentSeparator + 1 >= tag.size()) {
            r.matched = false;
            return r;
        }
        const COLORREF color = parseColorArgument(tag.substr(argumentSeparator + 1));
        if (color == CLR_INVALID) {
            r.matched = false;
            return r;
        }
        r.colorStack = StackAction::Push;
        r.color = color;
        return r;
    }

    if (tag.front() == '#') {
        if (tag.size() == 7) {
            const COLORREF hex = parseHex6(tag.data() + 1);
            if (hex != CLR_INVALID) {
                r.colorStack = closing ? StackAction::Pop : StackAction::Push;
                r.color = hex;
                r.tagName = tag;
                return r;
            }
        }
        r.matched = false;
        return r;
    }
    if (equalsIgnoreCase(tag, "bold") || equalsIgnoreCase(tag, "b")) {
        r.boldStack = closing ? StackAction::Pop : StackAction::Push;
        r.tagName = "bold";
        return r;
    }
    if (equalsIgnoreCase(tag, "italic") || equalsIgnoreCase(tag, "i")) {
        r.italicStack = closing ? StackAction::Pop : StackAction::Push;
        r.tagName = "italic";
        return r;
    }
    if (equalsIgnoreCase(tag, "underlined") || equalsIgnoreCase(tag, "u")) {
        r.underlineStack = closing ? StackAction::Pop : StackAction::Push;
        r.tagName = "underlined";
        return r;
    }
    if (equalsIgnoreCase(tag, "strikethrough") || equalsIgnoreCase(tag, "st")) {
        r.strikeStack = closing ? StackAction::Pop : StackAction::Push;
        r.tagName = "strikethrough";
        return r;
    }
    if (equalsIgnoreCase(tag, "reset") && !closing) {
        r.reset = true;
        return r;
    }

    const COLORREF nc = namedColor(tag);
    if (nc != CLR_INVALID) {
        r.colorStack = closing ? StackAction::Pop : StackAction::Push;
        r.color = nc;
        r.tagName = tag;
        return r;
    }
    r.matched = false;  // 未知标签当普通字符
    return r;
}

} // namespace

std::vector<ColorSegment> McColorLexer::lex(const std::string& value) const {
    std::vector<ColorSegment> segs;
    const size_t n = value.size();
    size_t i = 0, segStart = 0;

    COLORREF curColor = CLR_INVALID;
    bool bold = false, italic = false, underline = false, strike = false;
    // 标签名视图均引用本次 lex 的只读 value，栈生命周期不会越过该调用。
    std::vector<std::pair<std::string_view, COLORREF>> colorStack;
    std::vector<std::pair<std::string_view, bool>> boldStack, italicStack,
                                                   underlineStack, strikeStack;

    // 把 [from, to) 用当前状态成段
    auto emit = [&](size_t from, size_t to) {
        if (to <= from) return;
        ColorSegment s{};
        s.start  = static_cast<Sci_Position>(from);
        s.length = static_cast<Sci_Position>(to - from);
        s.color  = curColor;
        s.bold = bold; s.italic = italic; s.underline = underline; s.strike = strike;
        segs.push_back(s);
    };

    auto hasMatchingTop = [](StackAction action, std::string_view tag,
                             const auto& stack) {
        return action != StackAction::Pop ||
               (!stack.empty() && equalsIgnoreCase(stack.back().first, tag));
    };

    auto applyBoolState = [](StackAction action, int direct, std::string_view tag,
                             bool& state,
                             std::vector<std::pair<std::string_view, bool>>& stack) {
        if (action == StackAction::Push) {
            stack.emplace_back(tag, state);
            state = true;
        } else if (action == StackAction::Pop) {
            state = stack.back().second;  // 调用前已验证栈顶标签
            stack.pop_back();
        } else if (direct != 0) {
            state = direct > 0;
        }
    };

    while (i < n) {
        CodeParse cp = parseAmpCode(value, i);
        if (!cp.matched) cp = parseTag(value, i);

        if (cp.matched) {
            const bool validClose =
                hasMatchingTop(cp.colorStack, cp.tagName, colorStack) &&
                hasMatchingTop(cp.boldStack, cp.tagName, boldStack) &&
                hasMatchingTop(cp.italicStack, cp.tagName, italicStack) &&
                hasMatchingTop(cp.underlineStack, cp.tagName, underlineStack) &&
                hasMatchingTop(cp.strikeStack, cp.tagName, strikeStack);
            if (!validClose) {
                ++i;  // 错误闭合标签按普通文本处理，不改变当前状态
                continue;
            }

            // 开启码字使用切换后的状态；闭合标签先使用当前状态，再恢复外层。
            const bool closingTag = closesScope(cp);
            emit(segStart, i);
            if (closingTag)
                emit(i, cp.end);

            if (cp.reset) {
                curColor = CLR_INVALID;
                bold = italic = underline = strike = false;
                colorStack.clear();
                boldStack.clear(); italicStack.clear();
                underlineStack.clear(); strikeStack.clear();
            }

            if (cp.colorStack == StackAction::Push) {
                colorStack.emplace_back(cp.tagName, curColor);
                curColor = cp.color;
            } else if (cp.colorStack == StackAction::Pop) {
                curColor = colorStack.back().second;  // 调用前已验证栈顶标签
                colorStack.pop_back();
            } else if (cp.colorChanged) {
                if (cp.clearDecorations)
                    bold = italic = underline = strike = false;
                curColor = cp.color;
            }

            applyBoolState(cp.boldStack, cp.bold, cp.tagName, bold, boldStack);
            applyBoolState(cp.italicStack, cp.italic, cp.tagName, italic, italicStack);
            applyBoolState(cp.underlineStack, cp.underline, cp.tagName, underline, underlineStack);
            applyBoolState(cp.strikeStack, cp.strike, cp.tagName, strike, strikeStack);
            if (!closingTag)
                emit(i, cp.end);
            segStart = cp.end;
            i = cp.end;
        } else {
            ++i;  // 普通字符
        }
    }
    emit(segStart, n);
    return segs;
}
