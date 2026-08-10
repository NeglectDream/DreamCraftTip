// ============================================================================
// PluginMeta — 插件元数据单一来源
//
// 职责（SRP）：集中定义插件对外可见的名称、版本、作者、官网，供
//              PluginEntry::getName 与 AboutDialog 共享，避免多处硬编码漂移。
//
// 修改任一字段只需改本文件一处（DRY），所有展示点自动同步。
// ============================================================================
#pragma once

namespace PluginMeta {

// Notepad++ 插件管理器与加载目录所见的插件名；同时决定 DLL/目录命名。
constexpr wchar_t kName[] = L"DreamCraftTip";

// About 面板显示的版本号。发布新版本时同步更新此处一处。
constexpr wchar_t kVersion[] = L"1.0.0";

// About 面板显示的作者署名。
constexpr wchar_t kAuthor[] = L"\u6625\u590f\u4e4b\u68a6\u5de5\u4f5c\u5ba4";

// About 面板可点击官网，点击由 ShellExecute 调起系统默认浏览器。
constexpr wchar_t kHomepage[] = L"https://www.mcd7.cn";

} // namespace PluginMeta
