// ============================================================================
// IconRegistry 实现 — GDI+ PNG 解码与 BGRA 缓存
// ============================================================================
#include "IconRegistry.h"
#include "McConstants.h"
#include <windows.h>
#include <wtypes.h>   // PROPID（MinGW + WIN32_LEAN_AND_MEAN 下需显式包含）
#include <gdiplus.h>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <utility>

#ifdef _MSC_VER
#pragma comment(lib, "gdiplus.lib")
#endif

namespace {

// 全插件仅 IconRegistry 使用 GDI+。调用均在 Notepad++ UI 线程，无需锁。
ULONG_PTR g_token = 0;
int       g_ref   = 0;

bool gdiplusAcquire() {
    if (g_ref > 0) {
        ++g_ref;
        return true;
    }
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&g_token, &input, nullptr) != Gdiplus::Ok) {
        g_token = 0;
        return false;
    }
    g_ref = 1;
    return true;
}

void gdiplusRelease() {
    if (g_ref <= 0) return;
    if (--g_ref == 0) {
        Gdiplus::GdiplusShutdown(g_token);
        g_token = 0;
    }
}

std::string wideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string s(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], len, nullptr, nullptr);
    s.resize(static_cast<size_t>(len) - 1);  // 去 NUL
    return s;
}

std::string asciiLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// 加载 PNG 并缩放到 MC_ICON_SIZE，缓存 GDI+ 可直接绘制的 BGRA 像素。
bool loadIconBgra(const std::wstring& path, IconImage& out) {
    Gdiplus::Bitmap src(path.c_str());
    if (src.GetLastStatus() != Gdiplus::Ok)
        return false;

    Gdiplus::Bitmap bmp(MC_ICON_SIZE, MC_ICON_SIZE, PixelFormat32bppARGB);
    if (bmp.GetLastStatus() != Gdiplus::Ok)
        return false;

    {
        Gdiplus::Graphics g(&bmp);
        if (g.GetLastStatus() != Gdiplus::Ok)
            return false;
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        if (g.DrawImage(&src, 0, 0, MC_ICON_SIZE, MC_ICON_SIZE) != Gdiplus::Ok)
            return false;
    } // 必须先释放 Graphics，再对同一 Bitmap 调 LockBits。

    Gdiplus::BitmapData data{};
    Gdiplus::Rect rect(0, 0, MC_ICON_SIZE, MC_ICON_SIZE);
    if (bmp.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok)
        return false;

    out.width = MC_ICON_SIZE;
    out.height = MC_ICON_SIZE;
    out.bgraPixels.resize(static_cast<size_t>(MC_ICON_SIZE) * MC_ICON_SIZE * 4);

    const unsigned char* base = static_cast<const unsigned char*>(data.Scan0);
    for (int y = 0; y < MC_ICON_SIZE; ++y) {
        const unsigned char* row = base + static_cast<std::ptrdiff_t>(y) * data.Stride;
        unsigned char* output = out.bgraPixels.data() +
            static_cast<size_t>(y) * MC_ICON_SIZE * 4;
        std::copy(row, row + static_cast<size_t>(MC_ICON_SIZE) * 4, output);
    }
    bmp.UnlockBits(&data);
    return true;
}

} // namespace

IconRegistry::~IconRegistry() {
    if (gdiplusReady_)
        gdiplusRelease();
}

void IconRegistry::loadFromDirectory(const std::wstring& dir) {
    if (!gdiplusReady_) {
        gdiplusReady_ = gdiplusAcquire();
        if (!gdiplusReady_) return;
    }

    icons_.clear();
    const std::wstring pattern = dir + L"\\*.png";
    WIN32_FIND_DATAW fd{};
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const std::wstring fname = fd.cFileName;
        const size_t dot = fname.rfind(L'.');
        if (dot == std::wstring::npos) continue;
        const std::string itemId = asciiLower(wideToUtf8(fname.substr(0, dot)));
        if (itemId.empty()) continue;

        IconImage img;
        if (loadIconBgra(dir + L"\\" + fname, img))
            icons_[itemId] = std::move(img);
        // 单文件失败仅跳过，不阻断其余图标。
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

bool IconRegistry::hasIcon(const std::string& itemId) const {
    return icons_.find(itemId) != icons_.end();
}

const IconImage* IconRegistry::getIcon(const std::string& itemId) const {
    const auto it = icons_.find(itemId);
    return it != icons_.end() ? &it->second : nullptr;
}

std::vector<std::string> IconRegistry::listIds() const {
    std::vector<std::string> ids;
    ids.reserve(icons_.size());
    for (const auto& kv : icons_)
        ids.push_back(kv.first);
    return ids;
}
