// ============================================================================
// PluginEntry 实现 — Notepad++ 6 个导出函数 + 通知分发
// ============================================================================
#include "PluginEntry.h"
#include "AboutDialog.h"
#include "DecorationCoordinator.h"
#include "PluginMeta.h"
#include "sdk/Notepad_plus_msgs.h"
#include "sdk/Scintilla.h"
#include <cwchar>
#include <memory>

namespace {

constexpr const wchar_t* kPluginName = PluginMeta::kName;
constexpr int     kCmdCount = 2;

FuncItem g_funcItems[kCmdCount];
std::unique_ptr<DecorationCoordinator> g_coordinator;
HMODULE g_hModule = nullptr;

NppData& nppData() {
    static NppData data{};
    return data;
}

void setMenuName(FuncItem& item, const wchar_t* name) {
    std::wcsncpy(item._itemName, name, menuItemSize - 1);
    item._itemName[menuItemSize - 1] = L'\0';
}

void cmdRescan() {
    try {
        if (g_coordinator)
            g_coordinator->onFileChanged();
    } catch (...) {
    }
}

void cmdAbout() {
    AboutDialog::show(nppData()._nppHandle);
}

void initFuncItems() {
    for (auto& item : g_funcItems)
        item = FuncItem{};

    setMenuName(g_funcItems[0], L"Rescan now");
    g_funcItems[0]._pFunc = cmdRescan;

    setMenuName(g_funcItems[1], L"About");
    g_funcItems[1]._pFunc = cmdAbout;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*reserved*/) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = hModule;
        ::DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}

extern "C" __declspec(dllexport) void setInfo(NppData data) {
    try {
        nppData() = data;
        initFuncItems();
        g_coordinator = std::make_unique<DecorationCoordinator>(data, g_hModule);
        // Notepad++ 尚未完成初始化；等待 NPPN_READY 再访问编辑控件。
    } catch (...) {
        g_coordinator.reset();
    }
}

extern "C" __declspec(dllexport) const wchar_t* getName() {
    return kPluginName;
}

extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* nbFuncItem) {
    if (nbFuncItem)
        *nbFuncItem = kCmdCount;
    return g_funcItems;
}

extern "C" __declspec(dllexport) void beNotified(SCNotification* notify) {
    try {
        if (!notify) return;

        if (notify->nmhdr.code == NPPN_SHUTDOWN) {
            // 在 DLL 卸载前主动析构，确保定时器和 GDI+ 不在 loader lock 中释放。
            g_coordinator.reset();
            return;
        }
        if (!g_coordinator) return;

        switch (notify->nmhdr.code) {
            case NPPN_READY:
                g_coordinator->onReady();
                break;

            case NPPN_FILEOPENED:
            case NPPN_FILESAVED:
            case NPPN_GLOBALMODIFIED: // Replace All 从 8.6.5 起不再逐项发 SCN_MODIFIED
                g_coordinator->onFileChanged();
                break;

            case NPPN_BUFFERACTIVATED:
                g_coordinator->onFileChanged();
                break;

            case NPPN_LANGCHANGED:
            case NPPN_WORDSTYLESUPDATED:
                // lexer/主题切换可能重写基础 style，克隆缓存必须失效。
                g_coordinator->onStylesChanged();
                break;

            case SCN_UPDATEUI:
                if (notify->updated & SC_UPDATE_V_SCROLL)
                    g_coordinator->onViewportChanged(reinterpret_cast<HWND>(notify->nmhdr.hwndFrom));
                break;

            case SCN_PAINTED:
                g_coordinator->onPainted(reinterpret_cast<HWND>(notify->nmhdr.hwndFrom));
                break;

            case SCN_MODIFIED:
                if (notify->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)) {
                    g_coordinator->onModifiedDebounced(
                        reinterpret_cast<HWND>(notify->nmhdr.hwndFrom),
                        notify->position, notify->length, notify->modificationType);
                }
                break;

            default:
                break;
        }
    } catch (...) {
        // 任何 C++ 异常都必须止于插件 ABI 边界。
    }
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT msg, WPARAM wp, LPARAM lp) {
    // 本插件不拦截 Notepad++ Windows 消息。
    (void)msg; (void)wp; (void)lp;
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL isUnicode() {
    return TRUE;
}
