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
    // 经典 debounce：先取消上次未触发的计时，再重新起算。
    ::KillTimer(owner_, timerId_);
    try {
        auto& r = registry();
        r[timerId_] = this;
        if (::SetTimer(owner_, timerId_, delayMs_, &DebounceTimer::timerProc) == 0)
            r.erase(timerId_);
    } catch (...) {
        // 内存分配失败时降级为不触发扫描，不能让异常越过插件 ABI。
    }
}

void DebounceTimer::cancel() noexcept {
    ::KillTimer(owner_, timerId_);
    try {
        registry().erase(timerId_);
    } catch (...) {
    }
}

VOID CALLBACK DebounceTimer::timerProc(HWND hwnd, UINT /*uMsg*/, UINT_PTR idEvent, DWORD /*dwTime*/) noexcept {
    ::KillTimer(hwnd, idEvent);
    try {
        auto& r = registry();
        auto it = r.find(idEvent);
        if (it == r.end()) return;
        DebounceTimer* self = it->second;
        r.erase(it);
        if (self && self->cb_)
            self->cb_();
    } catch (...) {
        // Win32 回调边界禁止异常外逸。
    }
}
