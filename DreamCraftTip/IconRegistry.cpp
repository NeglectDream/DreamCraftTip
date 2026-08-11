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

bool copyBitmapBgra(Gdiplus::Bitmap& bitmap, IconImage& out) {
    Gdiplus::BitmapData data{};
    Gdiplus::Rect rect(0, 0, MC_ICON_SIZE, MC_ICON_SIZE);
    if (bitmap.LockBits(&rect, Gdiplus::ImageLockModeRead,
                        PixelFormat32bppARGB, &data) != Gdiplus::Ok) {
        return false;
    }

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
    bitmap.UnlockBits(&data);

    out.drawable = std::make_unique<Gdiplus::Bitmap>(
        out.width, out.height, out.width * 4, PixelFormat32bppARGB,
        out.bgraPixels.data());
    if (out.drawable->GetLastStatus() != Gdiplus::Ok) {
        out.drawable.reset();
        return false;
    }
    return true;
}

// 加载 PNG；原生槽位尺寸直接提取像素，其他尺寸只在首次使用时缩放。
bool loadIconBgra(const std::wstring& path, IconImage& out) {
    Gdiplus::Bitmap src(path.c_str());
    if (src.GetLastStatus() != Gdiplus::Ok)
        return false;

    if (src.GetWidth() == MC_ICON_SIZE && src.GetHeight() == MC_ICON_SIZE)
        return copyBitmapBgra(src, out);

    Gdiplus::Bitmap resized(MC_ICON_SIZE, MC_ICON_SIZE, PixelFormat32bppARGB);
    if (resized.GetLastStatus() != Gdiplus::Ok)
        return false;

    {
        Gdiplus::Graphics graphics(&resized);
        if (graphics.GetLastStatus() != Gdiplus::Ok)
            return false;
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        if (graphics.DrawImage(&src, 0, 0, MC_ICON_SIZE, MC_ICON_SIZE) != Gdiplus::Ok)
            return false;
    }
    return copyBitmapBgra(resized, out);
}

} // namespace

IconRegistry::~IconRegistry() {
    // drawable 必须在 GdiplusShutdown 前析构。
    icons_.clear();
    if (gdiplusReady_)
        gdiplusRelease();
}

bool IconRegistry::ensureReady() const {
    if (gdiplusReady_)
        return true;
    gdiplusReady_ = gdiplusAcquire();
    return gdiplusReady_;
}

void IconRegistry::indexDirectory(const std::wstring& dir) {
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

        IconEntry entry;
        entry.path = dir + L"\\" + fname;
        icons_[itemId] = std::move(entry);
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

bool IconRegistry::hasIcon(const std::string& itemId) const {
    return icons_.find(itemId) != icons_.end();
}

const IconImage* IconRegistry::getIcon(const std::string& itemId) const {
    const auto it = icons_.find(itemId);
    if (it == icons_.end()) return nullptr;

    const IconEntry& entry = it->second;
    if (entry.state == LoadState::Loaded)
        return &entry.image;
    if (entry.state == LoadState::Failed)
        return nullptr;

    if (!ensureReady() || !loadIconBgra(entry.path, entry.image)) {
        entry.state = LoadState::Failed;
        return nullptr;
    }
    entry.state = LoadState::Loaded;
    return &entry.image;
}

std::vector<std::string> IconRegistry::listIds() const {
    std::vector<std::string> ids;
    ids.reserve(icons_.size());
    for (const auto& kv : icons_)
        ids.push_back(kv.first);
    return ids;
}
