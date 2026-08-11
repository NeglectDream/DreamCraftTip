// ============================================================================
// McItemIdMatcher — Minecraft 原版物品 ID 判定
//
// 职责（SRP）：判断 value 是否为原版物品 ID，并返回归一化 itemId 及
//              物品表达式在原 value 中的起始偏移，供上层精确锚定图标。
//
// 判定规则：
//   1. 优先匹配 "minecraft:<id>"（1.13+ 命名空间形式）
//   2. 退化匹配裸 "<id>"（如 diamond_sword）
//   3. 兼容旧式数字数据后缀：iron_ingot:1 / minecraft:iron_ingot:1
//   4. 兼容 MenuItem 紧凑字段对：type:MAP / material:diamond / id:iron_ingot
//      （Shop 插件配置常见写法，字段名白名单见 .cpp 的 menuItemFields()）
//   5. 仅当 id 出现在内置原版 ID 集合时才认作物品，避免把 "server"、
//      "plugin" 等普通词误判
//   6. 用户需在 icons/ 放置对应 png 才显示图标；无 png 只上色不加图标
//      （由上层 InlineIconLayer 决定，本类只负责判定是否原版物品 ID）
//
// 原版 ID 集合：内置一份小型静态表（1.20 常用物品），用户可通过
//              config/items.txt 扩展。集合由调用方注入，便于测试。
// ============================================================================
#pragma once

#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

struct ItemIdMatch {
    bool        matched = false;
    std::string itemId;            // 归一化：去 namespace、小写，如 "diamond_sword"
    std::size_t sourceOffset = 0;  // 物品表达式在传入 value 中的起始字节偏移
};

class McItemIdMatcher {
public:
    explicit McItemIdMatcher(std::set<std::string> vanillaIds);

    // vanillaIds_ 中的 string_view 指向 vanillaIdStorage_ 的字符串内存。
    // 默认拷贝会深拷贝 vector 而保留旧 view，导致悬空指针，因此禁用拷贝。
    // 实例由 DecorationCoordinator 通过 unique_ptr 持有，无需任何拷贝。
    McItemIdMatcher(const McItemIdMatcher&) = delete;
    McItemIdMatcher& operator=(const McItemIdMatcher&) = delete;
    // 移动安全：vector<string> 移动只转移 buffer 所有权，字符地址稳定；
    // unordered_set<string_view> 移动只转移 bucket 数组，view 副本仍有效。
    McItemIdMatcher(McItemIdMatcher&&) noexcept = default;
    McItemIdMatcher& operator=(McItemIdMatcher&&) noexcept = default;

    ItemIdMatch match(const std::string& value) const;

private:
    // 哈希与比较在查询时折叠 ASCII 大小写，使 match() 可直接用
    // string_view 查找，不必为 lowercase 临时值分配 std::string。
    struct LowercaseAsciiHash {
        std::size_t operator()(std::string_view value) const noexcept;
    };

    struct LowercaseAsciiEqual {
        bool operator()(std::string_view left, std::string_view right) const noexcept;
    };

    void rebuildVanillaIdIndex();

    // string_view 索引依赖此存储；成员顺序保证存储先构造、后析构。
    std::vector<std::string> vanillaIdStorage_;
    std::unordered_set<std::string_view, LowercaseAsciiHash, LowercaseAsciiEqual> vanillaIds_;
};
