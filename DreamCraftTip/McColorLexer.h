// ============================================================================
// McColorLexer — Minecraft 颜色代码解析器
//
// 职责（SRP）：把含颜色代码的字符串解析为带颜色与修饰的分段列表。
//              纯字符串输入输出，不接触 Scintilla，便于单元测试。
//
// 支持格式：
//   1. &x / §x     传统颜色码（x=0-9,a-f,A-F），& 与 § 等价；颜色码会清除当前修饰
//   2. &l &n &o &m &k  修饰码：加粗/下划线/斜体/删除线/随机(忽略视觉)
//   3. &r          重置全部
//   4. &#RRGGBB    legacy hex 颜色（& 后跟 #），同样清除当前修饰
//   5. §#RRGGBB    legacy hex（§ 后跟 #），同样清除当前修饰
//   6. <#RRGGBB> / <color:#RRGGBB> MiniMessage hex
//      verbose color 同时支持 <color:red>、<colour:...>、<c:...> 及闭合标签
//   7. <tag>...</tag>  命名标签：<bold>/<b>/<italic>/<underlined>/<strikethrough>
//                     <reset> 及其闭合 </...>
//   8. <red>/<yellow>/... 命名颜色
//
// 输出语义：legacy 颜色码（如 "&e"）归属新颜色且不继承旧修饰；开启 MiniMessage
//          码字（如 "<aqua>"）归属切换后的状态；闭合标签（如 "</aqua>"）
//          归属被关闭的当前状态，标签后的文本恢复外层状态。
// ============================================================================
#pragma once

#include <windows.h>
#include <vector>
#include <string>
#include "sdk/Sci_Position.h"

struct ColorSegment {
    Sci_Position start = 0;    // 相对 value 起点的字节偏移
    Sci_Position length = 0;
    COLORREF     color = CLR_INVALID; // RGB；CLR_INVALID 表示继承默认色
    bool         bold = false;
    bool         italic = false;
    bool         underline = false;
    bool         strike = false;
};

class McColorLexer {
public:
    std::vector<ColorSegment> lex(const std::string& value) const;
};
