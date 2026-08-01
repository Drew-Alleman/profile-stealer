#pragma once
#include "sleep_common.h"
#include <thread>
#include <chrono>
#include <random>

inline void sleepMs(int sleepTime = SLEEP_MS) {
    if (sleepTime <= 0) return;

    double delay = sleepTime;
    if (SLEEP_JITTER > 0) {
        static thread_local std::mt19937 rng{ std::random_device{}() };
        const double j = std::min(SLEEP_JITTER / 100.0, 1.0);
        std::uniform_real_distribution<double> dist(-j, j);
        delay += delay * dist(rng);
        if (delay < 0.0) delay = 0.0;
    }

    std::this_thread::sleep_for(
        std::chrono::duration<double, std::milli>(delay));
}