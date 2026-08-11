// ============================================================================
// McItemIdMatcher 实现
// ============================================================================
#include "McItemIdMatcher.h"
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <utility>

namespace {

struct TrimmedValue {
    std::string text;
    size_t sourceOffset = 0;
};

TrimmedValue trim(const std::string& s) {
    auto isSpaceOrQuote = [](char c) { return c == ' ' || c == '\t' || c == '\'' || c == '"'; };
    size_t a = 0, b = s.size();
    while (a < b && isSpaceOrQuote(s[a])) ++a;
    while (b > a && isSpaceOrQuote(s[b - 1])) --b;
    return {s.substr(a, b - a), a};
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// 合法 MC 物品 ID 字符集：[a-z0-9_/]
bool validIdChars(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '/'))
            return false;
    }
    return true;
}

bool isNumericSuffix(const std::string& s, size_t start) noexcept {
    if (start >= s.size()) return false;
    for (size_t i = start; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
    }
    return true;
}

void stripNumericDataSuffix(std::string& path) {
    const size_t colon = path.rfind(':');
    if (colon != std::string::npos && isNumericSuffix(path, colon + 1))
        path.resize(colon);
}

// MenuItem 紧凑字段对的字段名白名单（小写）。
// Shop 插件（BossShopPro / Shopkeepers / EconomyShopGUI 等）与技能/Pet 配置中
// 表示"物品材质类型"的字段名惯例。冒号后跟物品 ID，如 type:MAP / material:diamond。
// 用白名单精确锁定语义，避免把 CustomModelData:772 / amount:1 / name:&a文本
// 等普通字段误判为物品。
const std::unordered_set<std::string>& menuItemFields() {
    static const std::unordered_set<std::string> fields = {
        "type", "material", "id", "item", "itemid", "itemtype", "materialtype"
    };
    return fields;
}

bool isMenuItemField(const std::string& lowerPrefix) {
    return menuItemFields().count(lowerPrefix) > 0;
}

} // namespace

McItemIdMatcher::McItemIdMatcher(std::set<std::string> vanillaIds)
    : vanillaIds_(std::move(vanillaIds)) {
}

ItemIdMatch McItemIdMatcher::match(const std::string& value) const {
    const TrimmedValue trimmed = trim(value);
    const std::string& v = trimmed.text;
    if (v.empty()) return {false, ""};

    size_t sourceOffset = trimmed.sourceOffset;
    std::string path = v;
    const size_t colon = v.find(':');
    if (colon != std::string::npos) {
        const std::string prefix = toLower(v.substr(0, colon));
        if (prefix == "minecraft") {
            // minecraft:<id> 与 minecraft:<id>:<数字数据值>
            path = v.substr(colon + 1);
            stripNumericDataSuffix(path);
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
            return {false, ""};
        }
    }

    path = toLower(path);
    if (!validIdChars(path)) return {false, ""};

    // 精确匹配，不做前缀模糊（避免 "diam" 误命中 "diamond"）
    if (vanillaIds_.count(path))
        return {true, path, sourceOffset};
    return {false, ""};
}
