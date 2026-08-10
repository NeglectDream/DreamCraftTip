// ============================================================================
// YamlValueLocator — YAML 行级 value 区间定位
//
// 职责（SRP）：对单行 YAML 文本，定位 "key: value" 中 value 的字符区间
//              [start, end)（相对于文档起点）。quoted value 的外层 YAML
//              引号不属于内容区间。不解析完整 YAML 语法树，只做够用的
//              行级状态机，保持轻量（KISS）。
//
// 支持范围（第一版）：
//   - "key: value"           行内 value
//   - "key: 'quoted'"        单引号 value
//   - "key: \"quoted\""      双引号 value
//   - "key: |" / "key: >"    块标量首行（块体由调用方逐行续读）
//   - "# comment"            注释行 → valid=false
//   - "  - item"             列表标量，value 为 "item"
//   - "  - type:iron_ingot:1" 紧凑列表映射（兼容常见插件配置）
//
// 不支持（留 TODO）：流式语法 {a: b, c: d}、[a, b, c]
// ============================================================================
#pragma once

#include "sdk/Sci_Position.h"
#include <string>

struct YamlValueRange {
    Sci_Position start = 0;       // 内容首字符；quoted value 不含 opening quote
    Sci_Position end = 0;         // 内容末字符的下一位置；不含 closing quote
    Sci_Position scalarStart = 0; // YAML scalar 原始起点（opening quote 或首字符）
    bool valid = false;           // 该行是否存在可处理的 value
};

class YamlValueLocator {
public:
    // lineText:  当前行文本（不含行尾符）
    // lineStart: 当前行首字符在文档中的绝对位置
    YamlValueRange locateValue(const std::string& lineText, Sci_Position lineStart) const;
};
