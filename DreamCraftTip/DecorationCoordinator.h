// ============================================================================
// DecorationCoordinator — 装饰流程编排器（门面 + 编排）
//
// 职责（SRP）：编排"清旧装饰 → 定位 value → 上色 → 加行内图标"全流程。
//              持有所有子组件，对外暴露触发入口。是唯一感知全部子模块的类，
//              子模块之间无横向依赖（遵循 DIP：高层依赖抽象编排，低层互不知）。
//
// 触发时机（由 PluginEntry 转发）：
//   - NPPN_READY           初始化子组件
//   - NPPN_BUFFERACTIVATED buffer 切换 → 全量重扫
//   - NPPN_LANGCHANGED     语言切换 → 全量重扫
//   - NPPN_FILEOPENED/FILESAVED 文件打开/保存 → 全量重扫
//   - SCN_MODIFIED(节流)        编辑 → DebounceTimer 合并后重扫
//   - SCN_UPDATEUI(垂直滚动)    大文件视口变化 → DebounceTimer 合并后重扫
// ============================================================================
#pragma once

#include "sdk/PluginInterface.h"
#include "ScintillaGateway.h"
#include "YamlValueLocator.h"
#include "McColorLexer.h"
#include "McItemIdMatcher.h"
#include "ColorIndicator.h"
#include "IconRegistry.h"
#include "InlineIconLayer.h"
#include "DebounceTimer.h"
#include <memory>

class DecorationCoordinator {
public:
    explicit DecorationCoordinator(NppData nppData, HMODULE hModule);
    ~DecorationCoordinator();

    // NPPN_READY 时调用：初始化各子组件并首次扫描
    void onReady();

    // 文件/buffer/语言切换时调用：全量重扫
    void onFileChanged();

    // SCN_MODIFIED 时调用：重置 300ms 防抖，超时后重扫
    void onModifiedDebounced();

    // SCN_UPDATEUI 垂直滚动时调用；仅大文件触发防抖重扫
    void onViewportChanged();

    // SCN_PAINTED 后调用：按文档锚点叠加当前可见行图标。
    void onPainted(HWND scintilla);

private:
    // 取当前活跃 Scintilla 句柄（主/副视图）
    HWND currentScintilla() const;
    // 判定当前文件是否 yml/yaml（语言类型 + 扩展名双判）
    bool isYamlFile() const;
    // 取 DLL 同级资源路径（icons 目录、私有槽位字体等）。
    std::wstring resourcePath(const wchar_t* name) const;
    // 主扫描流程：清旧 → 逐行定位 value → 上色 → 加行内图标
    void scan();

    NppData nppData_;
    HMODULE hModule_;
    // 子组件（gateway/colorIndicator/inlineIcons/debouncer/idMatcher 持有所有权）
    std::unique_ptr<ScintillaGateway> gateway_;
    YamlValueLocator locator_;
    McColorLexer colorLexer_;
    std::unique_ptr<McItemIdMatcher> idMatcher_;
    std::unique_ptr<ColorIndicator> colorIndicator_;
    IconRegistry iconRegistry_;
    std::unique_ptr<InlineIconLayer> inlineIcons_;
    std::unique_ptr<DebounceTimer> debouncer_;
    bool                                ready_ = false;
};
