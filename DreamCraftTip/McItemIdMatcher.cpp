// ============================================================================
// McItemIdMatcher 实现
// ============================================================================
#include "McItemIdMatcher.h"

#include <utility>

namespace {

struct TrimmedValue {
    std::string_view text;
    std::size_t sourceOffset = 0;
};

constexpr unsigned char asciiLower(unsigned char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<unsigned char>(c + ('a' - 'A')) : c;
}

TrimmedValue trim(std::string_view value) noexcept {
    const auto isSpaceOrQuote = [](char c) noexcept {
        return c == ' ' || c == '\t' || c == '\'' || c == '"';
    };

    std::size_t begin = 0;
    std::size_t end = value.size();
    while (begin < end && isSpaceOrQuote(value[begin]))
        ++begin;
    while (end > begin && isSpaceOrQuote(value[end - 1]))
        --end;
    return {value.substr(begin, end - begin), begin};
}

bool equalsIgnoreAsciiCase(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size())
        return false;

    for (std::size_t i = 0; i < left.size(); ++i) {
        if (asciiLower(static_cast<unsigned char>(left[i])) !=
            asciiLower(static_cast<unsigned char>(right[i]))) {
            return false;
        }
    }
    return true;
}

// match() 归一化后的合法 MC 物品 ID 字符集：[a-z0-9_/]。
// 查询阶段同时接受 ASCII 大写，避免先创建 lowercase 临时字符串。
bool validIdChars(std::string_view value) noexcept {
    if (value.empty())
        return false;

    for (const unsigned char c : value) {
        const unsigned char lower = asciiLower(c);
        if (!((lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9') ||
              lower == '_' || lower == '/')) {
            return false;
        }
    }
    return true;
}

// 注入集合的键必须已是 match() 最终查找的规范形式；其他键在旧实现中
// 也永远无法命中，因此不进入 string_view 哈希索引可保持原有语义。
bool isCanonicalVanillaId(std::string_view value) noexcept {
    if (value.empty())
        return false;

    for (const unsigned char c : value) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '/'))
            return false;
    }
    return true;
}

bool isNumericSuffix(std::string_view value, std::size_t start) noexcept {
    if (start >= value.size())
        return false;

    for (std::size_t i = start; i < value.size(); ++i) {
        if (value[i] < '0' || value[i] > '9')
            return false;
    }
    return true;
}

std::string_view stripNumericDataSuffix(std::string_view path) noexcept {
    const std::size_t colon = path.rfind(':');
    if (colon != std::string_view::npos && isNumericSuffix(path, colon + 1))
        return path.substr(0, colon);
    return path;
}

// 固定 7 个 MenuItem 字段直接比较，避免函数内静态 unordered_set 的初始化、
// 哈希和节点访问开销。大小写比较语义与旧实现的 toLower(prefix) 一致。
bool isMenuItemField(std::string_view prefix) noexcept {
    switch (prefix.size()) {
    case 2:
        return equalsIgnoreAsciiCase(prefix, "id");
    case 4:
        return equalsIgnoreAsciiCase(prefix, "type") ||
               equalsIgnoreAsciiCase(prefix, "item");
    case 6:
        return equalsIgnoreAsciiCase(prefix, "itemid");
    case 8:
        return equalsIgnoreAsciiCase(prefix, "material") ||
               equalsIgnoreAsciiCase(prefix, "itemtype");
    case 12:
        return equalsIgnoreAsciiCase(prefix, "materialtype");
    default:
        return false;
    }
}

} // namespace

std::size_t McItemIdMatcher::LowercaseAsciiHash::operator()(std::string_view value) const noexcept {
    // FNV-1a：对短 ASCII ID 成本低，并与 LowercaseAsciiEqual 使用相同的大小写折叠。
    std::size_t hash;
    std::size_t prime;
    if constexpr (sizeof(std::size_t) >= 8) {
        hash = static_cast<std::size_t>(14695981039346656037ull);
        prime = static_cast<std::size_t>(1099511628211ull);
    } else {
        hash = static_cast<std::size_t>(2166136261u);
        prime = static_cast<std::size_t>(16777619u);
    }

    for (const unsigned char c : value) {
        hash ^= asciiLower(c);
        hash *= prime;
    }
    return hash;
}

bool McItemIdMatcher::LowercaseAsciiEqual::operator()(
    std::string_view left, std::string_view right) const noexcept {
    return equalsIgnoreAsciiCase(left, right);
}

McItemIdMatcher::McItemIdMatcher(std::set<std::string> vanillaIds) {
    vanillaIdStorage_.reserve(vanillaIds.size());
    for (auto it = vanillaIds.begin(); it != vanillaIds.end();) {
        auto node = vanillaIds.extract(it++);
        if (isCanonicalVanillaId(node.value()))
            vanillaIdStorage_.push_back(std::move(node.value()));
    }
    rebuildVanillaIdIndex();
}

void McItemIdMatcher::rebuildVanillaIdIndex() {
    vanillaIds_.clear();
    vanillaIds_.reserve(vanillaIdStorage_.size());
    for (const std::string& itemId : vanillaIdStorage_)
        vanillaIds_.emplace(itemId);
}

ItemIdMatch McItemIdMatcher::match(const std::string& value) const {
    const TrimmedValue trimmed = trim(value);
    const std::string_view v = trimmed.text;
    if (v.empty())
        return {};

    std::size_t sourceOffset = trimmed.sourceOffset;
    std::string_view path = v;
    const std::size_t colon = v.find(':');
    if (colon != std::string_view::npos) {
        const std::string_view prefix = v.substr(0, colon);
        if (equalsIgnoreAsciiCase(prefix, "minecraft")) {
            // minecraft:<id> 与 minecraft:<id>:<数字数据值>
            path = stripNumericDataSuffix(v.substr(colon + 1));
        } else if (isNumericSuffix(v, colon + 1)) {
            // 旧式插件配置常见写法：iron_ingot:1
            path = v.substr(0, colon);
        } else if (isMenuItemField(prefix)) {
            // MenuItem 紧凑字段对：type:MAP / material:diamond / id:iron_ingot
            // 字段名不是物品表达式的一部分；图标应锚定在冒号后的物品 ID 前。
            sourceOffset += colon + 1;
            path = v.substr(colon + 1);
        } else {
            // 其他 namespace 暂不支持，避免把普通冒号文本误判为物品。
            return {};
        }
    }

    if (!validIdChars(path))
        return {};

    // 哈希集合精确匹配，不做前缀模糊；仅成功后构造返回所需的 itemId。
    const auto item = vanillaIds_.find(path);
    if (item == vanillaIds_.end())
        return {};
    return {true, std::string(*item), sourceOffset};
}
