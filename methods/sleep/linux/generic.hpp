#pragma once
#include "sleep_common.h"
#include <time.h>
#include <cerrno>
#include <random>

inline void sleepMs(int sleepTime = SLEEP_MS) {
    if (sleepTime <= 0) return;

    double delay = static_cast<double>(sleepTime);

    if (SLEEP_JITTER > 0.0f) {
        static thread_local std::mt19937 rng{ std::random_device{}() };
        
        // Convert percentage (e.g. 25.0) to decimal factor (0.25)
        double jitter_factor = static_cast<double>(SLEEP_JITTER) / 100.0;
        std::uniform_real_distribution<double> dist(-jitter_factor, jitter_factor);
        
        delay += delay * dist(rng);
        if (delay < 0.0) delay = 0.0;
    }

    // Preserve fractional milliseconds in sub-second precision
    struct timespec req;
    req.tv_sec = static_cast<time_t>(delay / 1000.0);
    req.tv_nsec = static_cast<long>((delay - (req.tv_sec * 1000.0)) * 1000000.0);

    while (nanosleep(&req, &req) == -1 && errno == EINTR) {}
}
