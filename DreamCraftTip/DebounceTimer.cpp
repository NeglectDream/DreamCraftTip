// ============================================================================
// DebounceTimer 实现
// ============================================================================
#include "DebounceTimer.h"
#include <unordered_map>
#include <utility>

namespace {
    // timerId -> DebounceTimer* 注册表。
    // trigger() 与 timerProc() 均在 Notepad++ UI 线程执行（SetTimer 回调由
    // owner 窗口的消息泵派发），单线程访问，无需同步原语（KISS）。
    std::unordered_map<UINT_PTR, DebounceTimer*>& registry() {
        static std::unordered_map<UINT_PTR, DebounceTimer*> r;
        return r;
    }
}

DebounceTimer::DebounceTimer(HWND owner, UINT_PTR timerId, UINT delayMs, Callback cb) noexcept
    : owner_(owner), timerId_(timerId), delayMs_(delayMs), cb_(std::move(cb)) {
}

DebounceTimer::~DebounceTimer() {
    cancel();
}

void DebounceTimer::trigger() noexcept {
    triggerAfter(delayMs_);
}

void DebounceTimer::triggerAfter(UINT delayMs) noexcept {
    // 经典 debounce：更新最新 deadline。旧 WM_TIMER 即使已入队，也会在
    // timerProc 中识别尚未到期并按剩余时间重新挂起。
    ::KillTimer(owner_, timerId_);
    deadlineMs_ = ::GetTickCount64() + delayMs;
    try {
        auto& r = registry();
        r[timerId_] = this;
        if (::SetTimer(owner_, timerId_, delayMs, &DebounceTimer::timerProc) == 0)
            r.erase(timerId_);
    } catch (...) {
        // 内存分配失败时降级为不触发扫描，不能让异常越过插件 ABI。
    }
}

void DebounceTimer::cancel() noexcept {
    ::KillTimer(owner_, timerId_);
    deadlineMs_ = 0;
    try {
        registry().erase(timerId_);
    } catch (...) {
    }
}

VOID CALLBACK DebounceTimer::timerProc(HWND hwnd, UINT /*uMsg*/, UINT_PTR idEvent, DWORD /*dwTime*/) noexcept {
    try {
        auto& r = registry();
        auto it = r.find(idEvent);
        if (it == r.end()) return;
        DebounceTimer* self = it->second;
        if (!self) {
            ::KillTimer(hwnd, idEvent);
            r.erase(it);
            return;
        }

        const ULONGLONG now = ::GetTickCount64();
        if (now < self->deadlineMs_) {
            const ULONGLONG remaining = self->deadlineMs_ - now;
            const UINT delay = remaining > MAXUINT
                ? MAXUINT : static_cast<UINT>(remaining);
            if (::SetTimer(hwnd, idEvent, delay, &DebounceTimer::timerProc) == 0)
                r.erase(it);
            return;
        }

        ::KillTimer(hwnd, idEvent);
        r.erase(it);
        if (self->cb_)
            self->cb_();
    } catch (...) {
        // Win32 回调边界禁止异常外逸。
    }
}
