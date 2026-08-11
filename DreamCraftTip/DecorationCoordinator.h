// ============================================================================
// DecorationCoordinator — 装饰流程编排器（门面 + 编排）
//
// 职责（SRP）：编排"清旧装饰 → 定位 value → 上色 → 加行内图标"全流程。
//              持有所有子组件，对外暴露触发入口。是唯一感知全部子模块的类，
//              子模块之间无横向依赖（遵循 DIP：高层依赖抽象编排，低层互不知）。
//
// 触发时机（由 PluginEntry 转发）：
//   - NPPN_READY           初始化子组件并排队首次扫描
//   - NPPN_BUFFERACTIVATED/LANGCHANGED/FILEOPENED/FILESAVED
//                         连续文件通知由 DebounceTimer 合并后扫描
//   - NPPN_WORDSTYLESUPDATED 使视图样式缓存失效并重扫
//   - SCN_MODIFIED(节流)      编辑 → DebounceTimer 合并后重扫
//   - SCN_UPDATEUI(垂直滚动)  大文件视口变化 → DebounceTimer 合并后重扫
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
#include <unordered_map>
#include <unordered_set>

class DecorationCoordinator {
public:
    explicit DecorationCoordinator(NppData nppData, HMODULE hModule);
    ~DecorationCoordinator();

    // NPPN_READY 时调用：初始化各子组件并首次扫描
    void onReady();

    // 文件/buffer/语言切换时调用：短延迟合并连续通知后重扫。
    void onFileChanged();

    // Notepad++ 主题/样式更新后调用：使视图样式缓存失效并排队重扫。
    void onStylesChanged();

    // SCN_MODIFIED 时调用：同步旧装饰范围、失效共享文档锚点并重置防抖。
    void onModifiedDebounced(HWND scintilla, Sci_Position position,
                             Sci_Position length, int modificationType);

    // SCN_UPDATEUI 垂直滚动时调用；仅大文件触发来源视图重扫。
    void onViewportChanged(HWND scintilla);

    // SCN_PAINTED 后调用：按文档锚点叠加当前可见行图标。
    void onPainted(HWND scintilla);

private:
    struct DecoratedRange {
        Sci_Position start = 0;
        Sci_Position end = 0;
    };

    // 取当前活跃 Scintilla 句柄（主/副视图）
    HWND currentScintilla() const;
    // 判定当前文件是否 yml/yaml（语言类型 + 扩展名双判）
    bool isYamlFile() const;
    // 取 DLL 同级资源路径（icons 目录、私有槽位字体等）。
    std::wstring resourcePath(const wchar_t* name) const;
    // 扫描待处理视图，并同步重建显示同一 Scintilla 文档的克隆视图。
    void scan();
    // 单个视图的主流程：清旧 → 定位 value → 上色 → 加行内图标。
    void scanView(HWND scintilla, bool yamlFile);

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
    std::unordered_set<HWND> pendingViews_;
    std::unordered_set<HWND> initializedViews_;
    std::unordered_map<HWND, std::unordered_map<sptr_t, DecoratedRange>> decoratedRanges_;
    bool ready_ = false;
};
