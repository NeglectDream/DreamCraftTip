// ============================================================================
// DebounceTimer — 扫描请求防抖
//
// 职责（SRP）：将高频 Scintilla 修改通知和连续文件事件合并为低频扫描。
//
// 背景：按键及文件打开/激活期间都会密集发送通知，若逐项全量扫描会造成
//      UI 卡顿。本类用 Win32 SetTimer 保留最后一次请求。
//
// 实现要点：
//   - trigger()/triggerAfter() 每次先 KillTimer 再 SetTimer，确保只保留
//     最后一次请求（经典 debounce 语义）。
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

    // 使用构造时默认延迟重置计时器。
    void trigger() noexcept;
    // 使用本次指定延迟重置计时器，用于合并短时间内的文件通知。
    void triggerAfter(UINT delayMs) noexcept;
    // 取消未触发的回调
    void cancel() noexcept;

private:
    static VOID CALLBACK timerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) noexcept;

    HWND        owner_;
    UINT_PTR    timerId_;
    UINT        delayMs_;
    ULONGLONG   deadlineMs_ = 0;
    Callback    cb_;
};
