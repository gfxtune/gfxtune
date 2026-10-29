// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

// Frame times collected for a whole play session in constant memory, and the
// numbers people compare games by: average FPS and the 1 % and 0.1 % lows.
// Plain C++: the layer fills it inside the game, the app reads it back.
//
// The lows are the frame rate at the 99th and 99.9th percentile of frame
// times (the "P1" and "P0.1" of PresentMon and CapFrameX), from buckets of
// 0.1 ms up to 100 ms and 10 ms up to a second. Gaps of a second or more
// are pauses (loading, the game in the background), not frames.

#include <array>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

namespace gfxtune::shared
{

class FrameHistogram
{
public:
    static constexpr int FineBuckets = 1000;  // 0.1 ms each, up to 100 ms
    static constexpr int CoarseBuckets = 90;  // 10 ms each, 100 ms to 1 s
    static constexpr int Buckets = FineBuckets + CoarseBuckets;
    static constexpr std::int64_t PauseNs = 1'000'000'000;

    // The bucket for a frame time, or -1 for a pause.
    static int bucketOf(std::int64_t ns)
    {
        if (ns < 0 || ns >= PauseNs) {
            return -1;
        }
        if (ns < 100'000'000) {
            return int(ns / 100'000);
        }
        return FineBuckets + int((ns - 100'000'000) / 10'000'000);
    }

    // The middle of a bucket, in milliseconds.
    static double bucketMs(int bucket)
    {
        if (bucket < FineBuckets) {
            return (double(bucket) + 0.5) * 0.1;
        }
        return 100.0 + (double(bucket - FineBuckets) + 0.5) * 10.0;
    }

    void add(std::int64_t ns)
    {
        const int bucket = bucketOf(ns);
        if (bucket < 0) {
            return;
        }
        ++m_counts[std::size_t(bucket)];
        ++m_frames;
        m_nanoseconds += std::uint64_t(ns);
    }

    std::uint64_t frames() const { return m_frames; }
    double seconds() const { return double(m_nanoseconds) / 1e9; }
    double averageFps() const { return m_nanoseconds > 0 ? double(m_frames) / seconds() : 0.0; }

    // The frame time below which `percent` of the frames were, in ms.
    double percentileMs(double percent) const
    {
        if (m_frames == 0) {
            return 0.0;
        }
        const double wanted = double(m_frames) * percent / 100.0;
        std::uint64_t seen = 0;
        for (int i = 0; i < Buckets; ++i) {
            seen += m_counts[std::size_t(i)];
            if (double(seen) >= wanted) {
                return bucketMs(i);
            }
        }
        return bucketMs(Buckets - 1);
    }

    double lowFps(double percent) const
    {
        const double ms = percentileMs(100.0 - percent);
        return ms > 0.0 ? 1000.0 / ms : 0.0;
    }

    // "bucket:count,..." for the buckets in use, plus the total time.
    std::string serialize() const
    {
        std::string out = std::to_string(m_nanoseconds);
        for (int i = 0; i < Buckets; ++i) {
            if (m_counts[std::size_t(i)] != 0) {
                out += ',';
                out += std::to_string(i);
                out += ':';
                out += std::to_string(m_counts[std::size_t(i)]);
            }
        }
        return out;
    }

    // Reads what serialize() wrote; false, and empty, on anything else.
    bool parse(std::string_view text)
    {
        *this = FrameHistogram{};
        std::uint64_t nanoseconds = 0;
        std::size_t comma = text.find(',');
        if (!number(text.substr(0, comma), nanoseconds)) {
            return false;
        }
        while (comma != std::string_view::npos) {
            text.remove_prefix(comma + 1);
            comma = text.find(',');
            const std::string_view item = text.substr(0, comma);
            const std::size_t colon = item.find(':');
            std::uint64_t bucket = 0;
            std::uint64_t count = 0;
            if (colon == std::string_view::npos || !number(item.substr(0, colon), bucket) || !number(item.substr(colon + 1), count)
                || bucket >= std::uint64_t(Buckets)) {
                *this = FrameHistogram{};
                return false;
            }
            m_counts[std::size_t(bucket)] += count;
            m_frames += count;
        }
        m_nanoseconds = nanoseconds;
        return true;
    }

    // Two sessions' worth, for a game's totals.
    void merge(const FrameHistogram &other)
    {
        for (int i = 0; i < Buckets; ++i) {
            m_counts[std::size_t(i)] += other.m_counts[std::size_t(i)];
        }
        m_frames += other.m_frames;
        m_nanoseconds += other.m_nanoseconds;
    }

private:
    static bool number(std::string_view text, std::uint64_t &out)
    {
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out);
        return error == std::errc() && end == text.data() + text.size() && !text.empty();
    }

    std::array<std::uint64_t, Buckets> m_counts{};
    std::uint64_t m_frames = 0;
    std::uint64_t m_nanoseconds = 0;
};

} // namespace gfxtune::shared
