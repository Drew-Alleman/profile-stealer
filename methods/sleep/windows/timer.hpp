#include "sleep_common.h"
#include <windows.h>
#include <random>

inline void sleepMs(int sleepTime = SLEEP_MS) {
    if (sleepTime <= 0) return;

    double delay = sleepTime;

    if (SLEEP_JITTER > 0) {
        static thread_local std::mt19937 rng{ std::random_device{}() };
        // MUST divide by 100.0 — otherwise delay can become minutes long
        std::uniform_real_distribution<double> dist(-SLEEP_JITTER / 100.0, SLEEP_JITTER / 100.0);
        delay += delay * dist(rng);
        if (delay < 0.0) delay = 0.0;
    }

    HANDLE hTimer = CreateWaitableTimer(NULL, TRUE, NULL);
    if (!hTimer) return;

    // Relative time in 100-nanosecond units.
    // 1 ms = 10 000 * 100-ns units
    LARGE_INTEGER dueTime;
    dueTime.QuadPart = -static_cast<LONGLONG>(delay * 10000.0);

    if (SetWaitableTimer(hTimer, &dueTime, 0, NULL, NULL, FALSE)) {
        WaitForSingleObject(hTimer, INFINITE);
    }

    CloseHandle(hTimer);
}