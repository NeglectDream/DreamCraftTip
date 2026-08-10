// ============================================================================
// DebounceTimer — SCN_MODIFIED 通知防抖
//
// 职责（SRP）：将高频的 Scintilla 修改通知合并为低频的"扫描"动作。
//
// 背景：Scintilla 在每次按键/粘贴/撤销时都发送 SCN_MODIFIED，若直接全量
//      扫描装饰会导致大文件明显卡顿。本类用 Win32 SetTimer 把
//      MC_DEBOUNCE_MS 内的多次触发合并为一次回调。
//
// 实现要点：
//   - trigger() 每次先 KillTimer 再 SetTimer，确保回调只在最后一次触发后
//     静默 MC_DEBOUNCE_MS 才执行（经典 debounce 语义）。
//   - 回调通过静态 timerProc 转发到对象，因 SetTimer 的回调签名无 this。
//   - owner 窗口需有消息泵（Notepad++ 主窗口即可），Timer 会在其 UI 线程触发。
// ============================================================================
#pragma once

#include <windows.h>
#include <functional>

class DebounceTimer {
public:
    using Callback = std::function<void()>;

    // owner: 能接收 WM_TIMER 的窗口句柄（一般传 NppData._nppHandle）
    // timerId: 本 timer 的唯一标识，由调用方分配
    // delayMs: 静默窗口长度，建议 MC_DEBOUNCE_MS
    DebounceTimer(HWND owner, UINT_PTR timerId, UINT delayMs, Callback cb) noexcept;
    ~DebounceTimer();

    DebounceTimer(const DebounceTimer&) = delete;
    DebounceTimer& operator=(const DebounceTimer&) = delete;

    // 触发：重置计时器。若 MC_DEBOUNCE_MS 内无再次触发，则执行回调
    void trigger() noexcept;
    // 取消未触发的回调
    void cancel() noexcept;

private:
    static VOID CALLBACK timerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) noexcept;

    HWND        owner_;
    UINT_PTR    timerId_;
    UINT        delayMs_;
    Callback    cb_;
};
