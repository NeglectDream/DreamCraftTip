// ============================================================================
// AboutDialog 实现 — 内存模板模态对话框 + 可点击官网
//
// 设计要点：
//   1. 用 DialogBoxIndirectParamW + 运行时构建的 DLGTEMPLATE，避免 .rc 资源
//      和资源编译器依赖，保持与现有纯代码构建链一致（KISS）。
//   2. 官网行用 Static(SS_NOTIFY) + WM_CTLCOLORSTATIC 蓝色下划线 + WM_SETCURSOR
//      手型光标 + WM_COMMAND/STN_CLICKED 调 ShellExecuteW 打开默认浏览器，
//      实现"板正且可点击"的官网链接，不依赖 comctl32 6.x 的 SysLink 控件。
//   3. 白底面板（WM_CTLCOLORDLG + WHITE_BRUSH）保证文字"板正"清晰。
// ============================================================================
#include "AboutDialog.h"
#include "PluginMeta.h"
#include <shellapi.h>  // ShellExecuteW
#include <vector>
#include <cstdlib>     // malloc/free
#include <cstring>     // memcpy

namespace {

// ---- 控件 ID（内存模板内自定义标识）----
constexpr int IDC_TITLE = 100;
constexpr int IDC_AUTHOR = 101;
constexpr int IDC_VERSION = 102;
constexpr int IDC_HOMEPAGE = 103;
// IDOK 由系统定义，用于"确定"按钮。

// 对话框运行期资源，随对话框销毁释放。
struct DialogState {
    HFONT hTitleFont = nullptr; // 标题加粗字体
    HFONT hBodyFont = nullptr;  // 作者/版本/按钮正文字体
    HFONT hLinkFont = nullptr;  // 官网蓝色下划线字体
};

// Win32 标准控件原子：Button=0x0080、Static=0x0082
constexpr WORD kAtomButton = 0x0080;
constexpr WORD kAtomStatic = 0x0082;

// 对话框尺寸（对话框单位 DLU）
constexpr short kDlgW = 200;
constexpr short kDlgH = 96;

// ---- 内存模板构建 ----
// 基于标准 DLGTEMPLATE / DLGITEMTEMPLATE 结构体 + memcpy 追加变长字段构建。
// 关键：DLGITEMTEMPLATE.id 是 WORD(2 字节)，不是 DWORD；控件结构与变长字段
// (class/title/创建数据)均按 pack(2) 自然对齐。用手写 putD(id) 会多写 2 字节，
// 导致 0xFFFF 类标识符错位、模板校验失败。用结构体 memcpy 可根除这一类 bug。
BYTE* buildDlgTemplate() {
    std::vector<BYTE> v;
    auto append = [&](const void* p, size_t n) {
        v.insert(v.end(), static_cast<const BYTE*>(p), static_cast<const BYTE*>(p) + n);
    };
    auto alignDw = [&] { while (v.size() % 4 != 0) v.push_back(0); };
    auto appendW = [&](WORD x) { append(&x, sizeof(x)); };
    auto appendStr = [&](const wchar_t* s) {
        do { appendW(static_cast<WORD>(*s)); } while (*s++);
    };
    auto appendItem = [&](DWORD style, short x, short y, short cx, short cy,
                           WORD id, WORD atom) {
        alignDw(); // 每个控件必须从 DWORD 边界开始
        DLGITEMTEMPLATE item{};
        item.style          = style;
        item.dwExtendedStyle = 0;
        item.x = x; item.y = y; item.cx = cx; item.cy = cy;
        item.id = id;                // WORD(2 字节)，非 DWORD
        append(&item, sizeof(item));
        appendW(0xFFFF); appendW(atom); // 类：原子
        appendW(0);                     // 标题：空串
        appendW(0);                     // 创建数据 count=0
    };

    // ---- 模板头 ----
    constexpr DWORD kDlgStyle =
        WS_POPUP | WS_VISIBLE | WS_CAPTION | WS_SYSMENU |
        DS_MODALFRAME | DS_3DLOOK | DS_SETFONT;
    DLGTEMPLATE hdr{};
    hdr.style = kDlgStyle;
    hdr.dwExtendedStyle = 0;
    hdr.cdit = 5;
    hdr.x = 0; hdr.y = 0; hdr.cx = kDlgW; hdr.cy = kDlgH;
    append(&hdr, sizeof(hdr));
    appendW(0);                          // 菜单：无
    appendW(0);                          // 窗口类：默认对话框
    appendStr(L"\u5173\u4e8e DreamCraftTip"); // 标题"关于 DreamCraftTip"
    appendW(9);                          // 字号(pt) —— DS_SETFONT 要求
    appendStr(L"Segoe UI");             // 字体名

    // ---- 控件（坐标/尺寸为 DLU）----
    // id 字段均为 WORD，与 DLGITEMTEMPLATE.id 一致。
    appendItem(WS_CHILD | WS_VISIBLE | SS_CENTER,
               40, 6, 120, 14, static_cast<WORD>(IDC_TITLE), kAtomStatic);
    appendItem(WS_CHILD | WS_VISIBLE | SS_LEFT,
               30, 28, 140, 10, static_cast<WORD>(IDC_AUTHOR), kAtomStatic);
    appendItem(WS_CHILD | WS_VISIBLE | SS_LEFT,
               30, 42, 140, 10, static_cast<WORD>(IDC_VERSION), kAtomStatic);
    // 官网：SS_NOTIFY 才能收到 STN_CLICKED
    appendItem(WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOTIFY,
               30, 56, 140, 12, static_cast<WORD>(IDC_HOMEPAGE), kAtomStatic);
    appendItem(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
               80, 76, 40, 14, static_cast<WORD>(IDOK), kAtomButton);

    BYTE* buf = static_cast<BYTE*>(malloc(v.size()));
    if (buf) memcpy(buf, v.data(), v.size());
    return buf;
}

// 创建 Segoe UI 字体；ptSize 为负值=点字号；bBold/bUnderline 控制字重与下划线。
HFONT createUiFont(int ptSize, bool bBold, bool bUnderline) {
    return CreateFontW(
        -ptSize, 0, 0, 0,
        bBold ? FW_BOLD : FW_NORMAL,
        FALSE, bUnderline ? TRUE : FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

// 在父窗口内居中对话框。
void centerInParent(HWND hDlg) {
    HWND parent = GetParent(hDlg);
    if (!parent) return;
    RECT rc{}, rp{};
    GetWindowRect(hDlg, &rc);
    GetWindowRect(parent, &rp);
    const int x = rp.left + ((rp.right - rp.left) - (rc.right - rc.left)) / 2;
    const int y = rp.top + ((rp.bottom - rp.top) - (rc.bottom - rc.top)) / 2;
    SetWindowPos(hDlg, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

INT_PTR CALLBACK dlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    DialogState* s = reinterpret_cast<DialogState*>(
        GetWindowLongPtrW(hDlg, GWLP_USERDATA));
    switch (msg) {
        case WM_INITDIALOG: {
            s = new DialogState();
            SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
            s->hTitleFont = createUiFont(16, true,  false);
            s->hBodyFont  = createUiFont(14, false, false);
            s->hLinkFont  = createUiFont(14, false, true );

            wchar_t buf[128];
            HWND hTitle = GetDlgItem(hDlg, IDC_TITLE);
            SendMessageW(hTitle, WM_SETFONT, reinterpret_cast<WPARAM>(s->hTitleFont), TRUE);
            SetWindowTextW(hTitle, PluginMeta::kName);

            HWND hAuthor = GetDlgItem(hDlg, IDC_AUTHOR);
            SendMessageW(hAuthor, WM_SETFONT, reinterpret_cast<WPARAM>(s->hBodyFont), TRUE);
            wsprintfW(buf, L"\u4f5c\u8005\uff1a%s", PluginMeta::kAuthor); // 作者：
            SetWindowTextW(hAuthor, buf);

            HWND hVer = GetDlgItem(hDlg, IDC_VERSION);
            SendMessageW(hVer, WM_SETFONT, reinterpret_cast<WPARAM>(s->hBodyFont), TRUE);
            wsprintfW(buf, L"\u7248\u672c\uff1a%s", PluginMeta::kVersion); // 版本：
            SetWindowTextW(hVer, buf);

            HWND hLink = GetDlgItem(hDlg, IDC_HOMEPAGE);
            SendMessageW(hLink, WM_SETFONT, reinterpret_cast<WPARAM>(s->hLinkFont), TRUE);
            wsprintfW(buf, L"\u5b98\u7f51\uff1a%s", PluginMeta::kHomepage); // 官网：
            SetWindowTextW(hLink, buf);

            SendMessageW(GetDlgItem(hDlg, IDOK), WM_SETFONT,
                         reinterpret_cast<WPARAM>(s->hBodyFont), TRUE);

            centerInParent(hDlg);
            return TRUE;
        }
        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC: {
            // 白底板正；官网文字蓝色，其余黑色。
            HDC hdc = reinterpret_cast<HDC>(wp);
            SetBkMode(hdc, TRANSPARENT);
            const DWORD id = (msg == WM_CTLCOLORSTATIC)
                ? GetDlgCtrlID(reinterpret_cast<HWND>(lp)) : 0u;
            SetTextColor(hdc, id == IDC_HOMEPAGE ? RGB(0, 0, 200) : RGB(0, 0, 0));
            return reinterpret_cast<INT_PTR>(GetStockObject(WHITE_BRUSH));
        }
        case WM_SETCURSOR: {
            // 官网控件上手型光标。
            if (GetDlgCtrlID(reinterpret_cast<HWND>(wp)) == IDC_HOMEPAGE) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            return FALSE;
        }
        case WM_COMMAND: {
            const WORD code = HIWORD(wp);
            const WORD id = LOWORD(wp);
            if (id == IDC_HOMEPAGE && code == STN_CLICKED) {
                ShellExecuteW(nullptr, L"open", PluginMeta::kHomepage,
                              nullptr, nullptr, SW_SHOWNORMAL);
                return TRUE;
            }
            if (id == IDOK || id == IDCANCEL) {
                EndDialog(hDlg, IDOK);
                return TRUE;
            }
            return FALSE;
        }
        case WM_CLOSE: {
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        case WM_DESTROY: {
            if (s) {
                if (s->hTitleFont) DeleteObject(s->hTitleFont);
                if (s->hBodyFont)  DeleteObject(s->hBodyFont);
                if (s->hLinkFont)  DeleteObject(s->hLinkFont);
                delete s;
                SetWindowLongPtrW(hDlg, GWLP_USERDATA, 0);
            }
            return FALSE;
        }
        default:
            return FALSE;
    }
}

} // namespace

void AboutDialog::show(HWND parent) {
    BYTE* tmpl = buildDlgTemplate();
    if (!tmpl) return;
    DialogBoxIndirectParamW(
        GetModuleHandleW(nullptr),
        reinterpret_cast<LPCDLGTEMPLATEW>(tmpl),
        parent, dlgProc, 0);
    free(tmpl);
}
