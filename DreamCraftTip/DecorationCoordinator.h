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
#include <functional>
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

    // 复合键：Scintilla 视图句柄 + Scintilla 文档指针。
    // 保留二元组键以支持克隆视图（两个 view 共享同一 document 时各自独立），
    // 同时把原本的二级 unordered_map 压平为单级，消除一次哈希查找与空内层条目副作用。
    struct DecorationKey {
        HWND view;
        sptr_t document;
        bool operator==(const DecorationKey& other) const noexcept {
            return view == other.view && document == other.document;
        }
    };

    // boost::hash_combine 风格的哈希组合，避免标准库默认异或导致的碰撞聚集。
    struct DecorationKeyHash {
        std::size_t operator()(const DecorationKey& k) const noexcept {
            const std::size_t h1 = std::hash<HWND>{}(k.view);
            const std::size_t h2 = std::hash<sptr_t>{}(k.document);
            return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
        }
    };

    // 取当前活跃 Scintilla 句柄（主/副视图）
    HWND currentScintilla() const;
    // 入口守卫：判定指定视图当前 buffer 是否 YAML（.yml/.yaml 或 N++ L_YAML）。
    // 所有视图修改入口在排队防抖 / 操作 Scintilla 前先调用本函数，非 YAML 直接
    // 返回，跳过 scanView 的 Scintilla 读写与 InlineIconLayer 锚点维护开销。
    //
    // 实现：HWND → 所属视图 → NPPM_GETCURRENTDOCINDEX → NPPM_GETBUFFERIDFROMPOS
    //   → NPPM_GETBUFFERLANGTYPE；以 bufferID 为键缓存结果，编辑/滚动高频路径上
    //   命中缓存时仅一次 hash 查询。FILEOPENED/SAVED/LANGCHANGED/BUFFERACTIVATED
    //   等改变 buffer 语言类型的入口负责 yamlBufferCache_.clear()。
    bool needsDecorationFor(HWND scintilla) const;
    // 取 DLL 同级资源路径（icons 目录、私有槽位字体等）。
    std::wstring resourcePath(const wchar_t* name) const;
    // 扫描待处理视图，并同步重建显示同一 Scintilla 文档的克隆视图。
    void scan();
    // 单个视图的主流程：清旧 → 定位 value → 上色 → 加行内图标。
    // 调用前提：scintilla 对应 buffer 已由入口守卫 needsDecorationFor 判定为 YAML。
    void scanView(HWND scintilla);
    // 统一封装 inlineIcons_/colorIndicator_ 的区间清理：先 clamp 到 [0, docLen]，
    // 空区间直接返回。recolourise 仅在即将重扫 YAML 范围时启用。
    void clearDecorationRange(Sci_Position start, Sci_Position end,
                              Sci_Position docLen, bool recolourise);

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
    std::unordered_map<DecorationKey, DecoratedRange, DecorationKeyHash> decoratedRanges_;
    // 按 bufferID 缓存"是否 YAML"判定结果；编辑/滚动高频路径查 O(1)。
    // buffer 切换/语言变更时由 onFileChanged/onStylesChanged 清空。
    mutable std::unordered_map<UINT_PTR, bool> yamlBufferCache_;
    bool ready_ = false;
};
