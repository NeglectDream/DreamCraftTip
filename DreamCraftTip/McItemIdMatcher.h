// ============================================================================
// McItemIdMatcher — Minecraft 原版物品 ID 判定
//
// 职责（SRP）：判断 value 是否为原版物品 ID，返回归一化的 itemId。
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

#include <string>
#include <set>

struct ItemIdMatch {
    bool        matched = false;
    std::string itemId;  // 归一化：去 namespace、小写，如 "diamond_sword"
};

class McItemIdMatcher {
public:
    explicit McItemIdMatcher(std::set<std::string> vanillaIds);

    ItemIdMatch match(const std::string& value) const;

private:
    std::set<std::string> vanillaIds_;
};
