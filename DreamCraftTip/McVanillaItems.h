// ============================================================================
// McVanillaItems.h — Minecraft 原版物品 ID 内置静态表
//
// 用途：供 McItemIdMatcher 构造时注入。用户可通过 config/items.txt 扩展。
//
// 数据来源：Minecraft Java 版 1.20+ 命名空间 minecraft:* 下的常用物品 ID。
// 完整列表约 1500+ 项；本表收录 YAML 配置文件中最常引用的子集（工具、
// 防具、材料、常见方块、食物、关键杂项），其余由用户按需扩展。
//
// 命名约定：全部小写、不含 "minecraft:" 前缀，与 McItemIdMatcher 归一化
//           输出一致。
// ============================================================================
#pragma once

#include <set>
#include <string>

namespace McVanillaItems {

    // 返回内置原版物品 ID 集合（小写、无 namespace）
    inline std::set<std::string> defaultSet() {
        return {
            // ---- 工具与武器 ----
            "diamond_sword","diamond_pickaxe","diamond_axe","diamond_shovel","diamond_hoe",
            "iron_sword","iron_pickaxe","iron_axe","iron_shovel","iron_hoe",
            "netherite_sword","netherite_pickaxe","netherite_axe","netherite_shovel","netherite_hoe",
            "wooden_sword","wooden_pickaxe","wooden_axe","wooden_shovel","wooden_hoe",
            "stone_sword","stone_pickaxe","stone_axe","stone_shovel","stone_hoe",
            "golden_sword","golden_pickaxe","golden_axe","golden_shovel","golden_hoe",
            "bow","crossbow","trident","shield","fishing_rod","shears","flint_and_steel",

            // ---- 防具 ----
            "diamond_helmet","diamond_chestplate","diamond_leggings","diamond_boots",
            "netherite_helmet","netherite_chestplate","netherite_leggings","netherite_boots",
            "iron_helmet","iron_chestplate","iron_leggings","iron_boots",

            // ---- 材料与矿物 ----
            "diamond","iron_ingot","gold_ingot","netherite_ingot","emerald","coal","charcoal",
            "stick","string","leather","bone","gunpowder","redstone","glowstone_dust",
            "lapis_lazuli","quartz","netherite_scrap","iron_nugget","gold_nugget","clay_ball",

            // ---- 常见方块 ----
            "stone","cobblestone","dirt","grass_block","sand","gravel","oak_log","oak_planks",
            "glass","brick","netherrack","obsidian","bedrock","tnt","crafting_table","furnace",
            "chest","ender_chest","bookshelf","torch","lantern","glowstone","sea_lantern",

            // ---- 食物 ----
            "apple","golden_apple","enchanted_golden_apple","bread","cooked_beef",
            "cooked_porkchop","cooked_cod","cooked_salmon","carrot","potato","wheat",

            // ---- 关键杂项 ----
            "ender_pearl","ender_eye","blaze_rod","book","enchanted_book","writable_book",
            "map","compass","clock","name_tag","saddle","experience_bottle","totem_of_undying",
            "firework_rocket","firework_star","potion","splash_potion","lingering_potion"
        };
    }

} // namespace McVanillaItems
