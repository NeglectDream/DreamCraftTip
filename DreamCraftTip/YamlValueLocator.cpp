// ============================================================================
// YamlValueLocator 实现 — 行级状态机
// ============================================================================
#include "YamlValueLocator.h"

namespace {

bool isCompactMappingSeparator(const std::string& line, size_t keyStart, size_t colon) noexcept {
    if (colon <= keyStart || colon + 1 >= line.size() || line[colon + 1] == '/')
        return false;

    const char first = line[keyStart];
    if (!((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z') || first == '_'))
        return false;

    for (size_t i = keyStart + 1; i < colon; ++i) {
        const char c = line[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return false;
    }
    return true;
}

} // namespace

YamlValueRange YamlValueLocator::locateValue(const std::string& lineText, Sci_Position lineStart) const {
    YamlValueRange result{};
    const size_t n = lineText.size();
    size_t i = 0;

    while (i < n && (lineText[i] == ' ' || lineText[i] == '\t')) ++i;
    if (i >= n || lineText[i] == '#') return result;

    // YAML 列表可包含标量（- diamond_sword）或映射（- material: diamond_sword）。
    bool listItem = false;
    if (lineText[i] == '-' && i + 1 < n && (lineText[i + 1] == ' ' || lineText[i + 1] == '\t')) {
        listItem = true;
        i += 2;
        while (i < n && (lineText[i] == ' ' || lineText[i] == '\t')) ++i;
        if (i >= n || lineText[i] == '#') return result;
    }
    const size_t contentStart = i;

    // 扫描映射分隔符，跳过引号内的 ':'；处理双引号反斜杠与单引号双写转义。
    char quote = 0;
    size_t separator = n;
    for (; i < n; ++i) {
        const char c = lineText[i];
        if (quote) {
            if (quote == '"' && c == '\\' && i + 1 < n) {
                ++i;
                continue;
            }
            if (c == quote) {
                if (quote == '\'' && i + 1 < n && lineText[i + 1] == '\'') {
                    ++i;
                    continue;
                }
                quote = 0;
            }
            continue;
        }
        if (c == '\'' || c == '"') {
            quote = c;
            continue;
        }
        if (c == ':') {
            const bool yamlSeparator = i + 1 == n || lineText[i + 1] == ' ' || lineText[i + 1] == '\t';
            const bool compactSeparator = isCompactMappingSeparator(lineText, contentStart, i);
            if (yamlSeparator || compactSeparator) {
                separator = i;
                break;
            }
        }
    }

    if (separator == n) {
        if (!listItem) return result;
        // 无映射分隔符的列表标量："- item" 中 item 自身就是 value。
        i = contentStart;
    } else {
        i = separator + 1;
        while (i < n && (lineText[i] == ' ' || lineText[i] == '\t')) ++i;
        if (i >= n) return result;  // "key:" 无行内 value
    }

    const size_t valueStart = i;
    const char first = lineText[i];
    result.scalarStart = lineStart + static_cast<Sci_Position>(valueStart);

    // 块标量首行；正文由 DecorationCoordinator 按缩进状态继续处理。
    if (first == '|' || first == '>') {
        result.start = lineStart + static_cast<Sci_Position>(valueStart);
        result.end = lineStart + static_cast<Sci_Position>(n);
        result.valid = true;
        return result;
    }

    // 引号 value：返回外层引号内部的内容，并正确跳过 YAML 转义。
    // 未闭合引号常见于编辑过程，此时仍返回从开引号后到行尾的内容。
    if (first == '\'' || first == '"') {
        const char q = first;
        const size_t quotedContentStart = valueStart + 1;
        size_t quotedContentEnd = n;
        ++i;
        while (i < n) {
            if (q == '"' && lineText[i] == '\\' && i + 1 < n) {
                i += 2;
                continue;
            }
            if (lineText[i] == q) {
                if (q == '\'' && i + 1 < n && lineText[i + 1] == '\'') {
                    i += 2;
                    continue;
                }
                quotedContentEnd = i;
                break;
            }
            ++i;
        }
        result.start = lineStart + static_cast<Sci_Position>(quotedContentStart);
        result.end = lineStart + static_cast<Sci_Position>(quotedContentEnd);
        result.valid = true;
        return result;
    }

    // 普通 value：到行尾或空白后的 '#'（行内注释）截断。
    size_t valueEnd = n;
    for (size_t j = valueStart + 1; j < n; ++j) {
        if (lineText[j] == '#' && (lineText[j - 1] == ' ' || lineText[j - 1] == '\t')) {
            valueEnd = j;
            break;
        }
    }
    while (valueEnd > valueStart && (lineText[valueEnd - 1] == ' ' || lineText[valueEnd - 1] == '\t'))
        --valueEnd;

    result.start = lineStart + static_cast<Sci_Position>(valueStart);
    result.end = lineStart + static_cast<Sci_Position>(valueEnd);
    result.valid = (result.end > result.start);
    return result;
}
