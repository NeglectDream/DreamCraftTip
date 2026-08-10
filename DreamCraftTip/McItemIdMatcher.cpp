// ============================================================================
// McItemIdMatcher 实现
// ============================================================================
#include "McItemIdMatcher.h"
#include <algorithm>
#include <cctype>
#include <utility>

namespace {

std::string trim(const std::string& s) {
    auto isSpaceOrQuote = [](char c) { return c == ' ' || c == '\t' || c == '\'' || c == '"'; };
    size_t a = 0, b = s.size();
    while (a < b && isSpaceOrQuote(s[a])) ++a;
    while (b > a && isSpaceOrQuote(s[b - 1])) --b;
    return s.substr(a, b - a);
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

} // namespace

McItemIdMatcher::McItemIdMatcher(std::set<std::string> vanillaIds)
    : vanillaIds_(std::move(vanillaIds)) {
}

ItemIdMatch McItemIdMatcher::match(const std::string& value) const {
    std::string v = trim(value);
    if (v.empty()) return {false, ""};

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
        } else {
            // 其他 namespace 暂不支持，避免把普通冒号文本误判为物品。
            return {false, ""};
        }
    }

    path = toLower(path);
    if (!validIdChars(path)) return {false, ""};

    // 精确匹配，不做前缀模糊（避免 "diam" 误命中 "diamond"）
    if (vanillaIds_.count(path))
        return {true, path};
    return {false, ""};
}
