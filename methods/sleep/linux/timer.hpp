#include <sys/timerfd.h>
#include <unistd.h>
#include <random>
#include <cstdint>

inline void sleepMs(int sleepTime = SLEEP_MS) {
    if (sleepTime <= 0) return;

    double delay = sleepTime;
    if (JITTER > 0) {
        static thread_local std::mt19937 rng{ std::random_device{}() };
        std::uniform_real_distribution<double> dist(-SLEEP_JITTER / 100.0, SLEEP_JITTER / 100.0);
        delay += delay * dist(rng);
        if (delay < 0.0) delay = 0.0;
    }

    int tfd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (tfd == -1) return;

    itimerspec its{};
    its.it_value.tv_sec = static_cast<time_t>(delay / 1000.0);
    its.it_value.tv_nsec = static_cast<long>((delay - its.it_value.tv_sec * 1000.0) * 1'000'000.0);

    if (timerfd_settime(tfd, 0, &its, nullptr) == -1) {
        close(tfd);
        return;
    }

    // Block until the timer expires
    uint64_t expirations = 0;
    read(tfd, &expirations, sizeof(expirations));

    close(tfd);
}