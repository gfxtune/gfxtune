// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include "shared/FrameHistogram.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace gfxtune::layer
{

// The card's temperatures, power and load, read from the amdgpu driver's
// files for the card the game draws on.
class GpuSensors
{
public:
    struct Reading {
        double edge = 0;    // °C
        double hotspot = 0; // °C
        double power = 0;   // W
        double load = 0;    // %
    };

    // Finds the card by its PCI vendor and device IDs; AMD cards only.
    bool find(const std::string &sysfsRoot, std::uint32_t vendorId, std::uint32_t deviceId);
    bool found() const { return !m_hwmon.empty(); }
    bool read(Reading &out) const;

private:
    std::string m_device; // .../device/
    std::string m_hwmon;  // .../device/hwmon/hwmonN/
    std::string m_edge, m_hotspot;
};

// Records the frame times of the game's main swapchain, and the card's
// readings once a second, into the folder gfxtune-run named:
//   live           the last second, for the card panel while the game runs
//   session-<pid>  the whole session so far, for the game's history
// A background thread does all the reading and writing, never the game's
// present. If the session file of this process exists (the game made a new
// Vulkan instance), it carries on from it.
class PerformanceRecorder
{
public:
    using Clock = std::chrono::steady_clock;

    PerformanceRecorder(std::string dir, std::string sysfsRoot, bool startThread = true);
    ~PerformanceRecorder(); // stops the thread and writes a last time

    void frame(Clock::time_point at);
    void setDevice(std::uint32_t vendorId, std::uint32_t deviceId);

    // One second's work now: what the thread does on its own; for tests.
    void tick(bool writeSession);

    std::string livePath() const { return m_dir + "/live"; }
    std::string sessionPath() const;

private:
    void run();
    void load();

    std::string m_dir;
    std::string m_sysfsRoot;
    long long m_pid;

    std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_stop = false;
    std::thread m_thread;

    // Guarded by m_mutex.
    shared::FrameHistogram m_session;
    Clock::time_point m_last{};
    std::uint64_t m_secondFrames = 0;
    std::int64_t m_secondSumNs = 0;
    std::int64_t m_secondMaxNs = 0;
    std::uint32_t m_vendor = 0;
    std::uint32_t m_device = 0;
    bool m_deviceChanged = false;

    // Only touched by the thread (or tick()).
    GpuSensors m_sensors;
    long long m_start = 0; // Unix seconds
    std::uint64_t m_samples = 0;
    double m_edgeSum = 0;
    double m_hotspotSum = 0;
    double m_hotspotMax = 0;
    double m_powerSum = 0;
    double m_loadSum = 0;
};

// Writes `text` to `path` through a temporary file and a rename, so readers
// never see half of it.
bool writeFileAtomically(const std::string &path, const std::string &text);

} // namespace gfxtune::layer
