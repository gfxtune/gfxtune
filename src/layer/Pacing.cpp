// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "Pacing.h"

#include <algorithm>
#include <cerrno>

#include <ctime>
#include <sched.h>

namespace gfxtune::layer
{

FrameLimiter::Clock::time_point FrameLimiter::slot(int fps, Clock::time_point now)
{
    if (fps <= 0) {
        m_fps = 0;
        return now;
    }
    const auto interval = std::chrono::nanoseconds(1'000'000'000LL / fps);
    if (fps != m_fps || m_next == Clock::time_point{} || now > m_next + interval) {
        m_fps = fps;
        m_next = now;
    }
    const Clock::time_point at = m_next;
    m_next += interval;
    return at;
}

void FrameLimiter::waitUntil(Clock::time_point when)
{
    // steady_clock is CLOCK_MONOTONIC on Linux. Sleeping is accurate to some
    // tens of microseconds; the last stretch is spun to land on time.
    constexpr auto Spin = std::chrono::microseconds(250);
    const auto sleepUntil = when - Spin;
    if (Clock::now() < sleepUntil) {
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(sleepUntil.time_since_epoch()).count();
        const timespec at{static_cast<time_t>(ns / 1'000'000'000), static_cast<long>(ns % 1'000'000'000)};
        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &at, nullptr) == EINTR) {
            // interrupted by a signal: sleep the rest
        }
    }
    while (Clock::now() < when) {
        sched_yield();
    }
}

float biasedLod(float original, int bias, float limit)
{
    return std::clamp(original + float(bias) / 100.0f, -limit, limit);
}

} // namespace gfxtune::layer
