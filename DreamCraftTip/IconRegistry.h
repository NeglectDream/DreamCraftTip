// ============================================================================
// IconRegistry — 扫描 icons/*.png，解码为 GDI+ BGRA 像素缓冲
//
// 职责（SRP）：把磁盘 PNG 加载并缩放为固定尺寸的 32bpp ARGB/BGRA
//              内存图像，供 InlineIconLayer 在 Scintilla 上叠加绘制。
//
// 解码方案：用 Windows 自带 GDI+；若尺寸 != MC_ICON_SIZE，加载时一次性
//          缩放并缓存，绘制阶段不再访问磁盘。
//
// 文件名约定：icons/diamond_sword.png → itemId="diamond_sword"
// ============================================================================
#pragma once

#include <string>
#include <vector>
#include <unordered_map>

struct IconImage {
    int width = 0;
    int height = 0;
    // GDI+ PixelFormat32bppARGB 的内存顺序为 BGRA。
    std::vector<unsigned char> bgraPixels;
};

class IconRegistry {
public:
    ~IconRegistry();

    // 扫描 dir 下所有 *.png，文件名（去扩展名）作为 itemId
    void loadFromDirectory(const std::wstring& dir);

    bool hasIcon(const std::string& itemId) const;
    const IconImage* getIcon(const std::string& itemId) const;
    // 列出所有已加载 itemId，供物品匹配集合扩展。
    std::vector<std::string> listIds() const;

private:
    std::unordered_map<std::string, IconImage> icons_;
    bool gdiplusReady_ = false;
};
