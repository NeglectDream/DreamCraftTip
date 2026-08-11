// ============================================================================
// IconRegistry — 索引 icons/*.png，并按需解码为 GDI+ BGRA 像素缓冲
//
// 职责（SRP）：维护 itemId 到 PNG 路径/像素缓存的映射，供 InlineIconLayer
//              在 Scintilla 上叠加绘制。
//
// 解码方案：启动时只索引文件名；首次绘制可见图标时用 Windows GDI+ 解码。
//          原生 16×16 图片直接提取像素，其他尺寸一次缩放后缓存。
//
// 文件名约定：icons/diamond_sword.png → itemId="diamond_sword"
// ============================================================================
#pragma once

#include <windows.h>
#include <wtypes.h>
#include <gdiplus.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct IconImage {
    int width = 0;
    int height = 0;
    // GDI+ PixelFormat32bppARGB 的内存顺序为 BGRA。
    std::vector<unsigned char> bgraPixels;
    // 直接引用 bgraPixels 的可绘制包装，避免每次 SCN_PAINTED 重建 Bitmap。
    std::unique_ptr<Gdiplus::Bitmap> drawable;
};

class IconRegistry {
public:
    ~IconRegistry();

    // 仅扫描 dir 下的 *.png 并建立文件名索引；不在启动阶段解码图片。
    void indexDirectory(const std::wstring& dir);

    bool hasIcon(const std::string& itemId) const;
    // 首次绘制前初始化 GDI+，确保 Graphics 与懒加载 Bitmap 使用同一生命周期。
    bool ensureReady() const;
    // 首次请求时解码并缓存；失败结果同样缓存，避免每次绘制重复访问磁盘。
    const IconImage* getIcon(const std::string& itemId) const;
    // 列出所有已索引 itemId，供物品匹配集合扩展。
    std::vector<std::string> listIds() const;

private:
    enum class LoadState {
        Unloaded,
        Loaded,
        Failed
    };

    struct IconEntry {
        std::wstring path;
        mutable IconImage image;
        mutable LoadState state = LoadState::Unloaded;
    };

    std::unordered_map<std::string, IconEntry> icons_;
    mutable bool gdiplusReady_ = false;
};
