// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include <chrono>

namespace gfxtune::layer
{

// Holds each frame back until its slot on a fixed grid, so frames come out
// evenly spaced at the cap (like DXVK's and framepace's limiters). A frame
// that is late does not shift the grid; one late by more than a whole
// interval starts it afresh, so a stall is never followed by a rush.
class FrameLimiter
{
public:
    using Clock = std::chrono::steady_clock;

    // When the frame may go, for a cap of `fps`; 0 means no cap and resets.
    Clock::time_point slot(int fps, Clock::time_point now);
    // Waits until then: sleeps most of the way, spins the last bit.
    static void waitUntil(Clock::time_point when);

private:
    int m_fps = 0;
    Clock::time_point m_next{};
};

// A sampler's LOD bias with the user's texture detail (in hundredths) added,
// within the card's limit.
float biasedLod(float original, int bias, float limit);

} // namespace gfxtune::layer
