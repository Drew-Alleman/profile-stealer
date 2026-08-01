#pragma once
#include "sleep_common.h"
#include <time.h>
#include <cerrno>
#include <random>

inline void sleepMs(int sleepTime = SLEEP_MS) {
    if (sleepTime <= 0) return;

    double delay = sleepTime;
    if (SLEEP_JITTER > 0.0f) {
        static thread_local std::mt19937 rng{ std::random_device{}() };
        std::uniform_real_distribution<double> dist(-SLEEP_JITTER, SLEEP_JITTER);
        delay += delay * dist(rng);
        if (delay < 0.0) delay = 0.0;
    }

    long total = static_cast<long>(delay);
    struct timespec req;
    req.tv_sec = total / 1000;
    req.tv_nsec = (total % 1000) * 1000000L;   // ms -> ns
    while (nanosleep(&req, &req) == -1 && errno == EINTR) {}
}