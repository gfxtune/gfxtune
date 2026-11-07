// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "Recorder.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <string_view>
#include <vector>

#include <dirent.h>
#include <unistd.h>

namespace gfxtune::layer
{

namespace
{

std::string readSmallFile(const std::string &path)
{
    std::ifstream file(path, std::ios::binary);
    std::string text(4096, '\0');
    file.read(text.data(), std::streamsize(text.size()));
    text.resize(std::size_t(file.gcount()));
    while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) {
        text.pop_back();
    }
    return text;
}

bool readNumber(const std::string &path, double &out)
{
    const std::string text = readSmallFile(path);
    if (text.empty()) {
        return false;
    }
    long long value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc() || end != text.data() + text.size()) {
        return false;
    }
    out = double(value);
    return true;
}

std::uint32_t readHex(const std::string &path)
{
    std::string text = readSmallFile(path);
    if (text.starts_with("0x")) {
        text.erase(0, 2);
    }
    std::uint32_t value = 0;
    std::from_chars(text.data(), text.data() + text.size(), value, 16);
    return value;
}

std::vector<std::string> entries(const std::string &dir)
{
    std::vector<std::string> names;
    if (DIR *d = opendir(dir.c_str())) {
        // Our own stream, read by this thread only.
        while (const dirent *e = readdir(d)) { // NOLINT(concurrency-mt-unsafe)
            const std::string name = e->d_name;
            if (name != "." && name != "..") {
                names.push_back(name);
            }
        }
        closedir(d);
    }
    return names;
}

long long unixSeconds()
{
    return static_cast<long long>(std::time(nullptr));
}

std::string formatNumber(double value)
{
    char text[64];
    std::snprintf(text, sizeof text, "%.3f", value);
    return text;
}

} // namespace

bool GpuSensors::find(const std::string &sysfsRoot, std::uint32_t vendorId, std::uint32_t deviceId)
{
    *this = GpuSensors{};
    constexpr std::uint32_t Amd = 0x1002;
    if (vendorId != Amd) {
        return false;
    }
    const std::string drm = sysfsRoot + "/class/drm/";
    for (const std::string &card : entries(drm)) {
        // card0, card1...; not the connectors (card0-DP-1).
        if (!card.starts_with("card") || card.find('-') != std::string::npos) {
            continue;
        }
        const std::string device = drm + card + "/device/";
        if (readHex(device + "vendor") != vendorId || readHex(device + "device") != deviceId) {
            continue;
        }
        for (const std::string &hwmon : entries(device + "hwmon")) {
            std::string dir = device + "hwmon/";
            dir.append(hwmon).append("/");
            for (int i = 1; i <= 9; ++i) {
                const std::string label = readSmallFile(dir + "temp" + std::to_string(i) + "_label");
                const std::string input = dir + "temp" + std::to_string(i) + "_input";
                if (label == "edge") {
                    m_edge = input;
                } else if (label == "junction") {
                    m_hotspot = input;
                }
            }
            m_hwmon = dir;
            m_device = device;
            return true;
        }
    }
    return false;
}

bool GpuSensors::read(Reading &out) const
{
    if (!found()) {
        return false;
    }
    double value = 0;
    out = Reading{};
    if (!m_edge.empty() && readNumber(m_edge, value)) {
        out.edge = value / 1000.0;
    }
    if (!m_hotspot.empty() && readNumber(m_hotspot, value)) {
        out.hotspot = value / 1000.0;
    }
    if (readNumber(m_hwmon + "power1_average", value) || readNumber(m_hwmon + "power1_input", value)) {
        out.power = value / 1e6;
    }
    if (readNumber(m_device + "gpu_busy_percent", value)) {
        out.load = value;
    }
    return true;
}

bool writeFileAtomically(const std::string &path, const std::string &text)
{
    const std::string temporary = path + ".tmp" + std::to_string(::getpid());
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out || !(out << text) || !out.flush()) {
            std::remove(temporary.c_str());
            return false;
        }
    }
    if (std::rename(temporary.c_str(), path.c_str()) != 0) {
        std::remove(temporary.c_str());
        return false;
    }
    return true;
}

PerformanceRecorder::PerformanceRecorder(std::string dir, std::string sysfsRoot, bool startThread)
    : m_dir(std::move(dir))
    , m_sysfsRoot(std::move(sysfsRoot))
    , m_pid(::getpid())
{
    m_start = unixSeconds();
    load();
    if (startThread) {
        m_thread = std::thread([this] { run(); });
    }
}

PerformanceRecorder::~PerformanceRecorder()
{
    {
        const std::scoped_lock lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    if (m_thread.joinable()) {
        m_thread.join();
    }
    tick(true);
}

std::string PerformanceRecorder::sessionPath() const
{
    return m_dir + "/session-" + std::to_string(m_pid);
}

void PerformanceRecorder::frame(Clock::time_point at)
{
    const std::scoped_lock lock(m_mutex);
    if (m_last != Clock::time_point{}) {
        const std::int64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(at - m_last).count();
        m_session.add(ns);
        if (shared::FrameHistogram::bucketOf(ns) >= 0) {
            ++m_secondFrames;
            m_secondSumNs += ns;
            m_secondMaxNs = std::max(m_secondMaxNs, ns);
        }
    }
    m_last = at;
}

void PerformanceRecorder::setDevice(std::uint32_t vendorId, std::uint32_t deviceId)
{
    const std::scoped_lock lock(m_mutex);
    if (vendorId != m_vendor || deviceId != m_device) {
        m_vendor = vendorId;
        m_device = deviceId;
        m_deviceChanged = true;
    }
}

void PerformanceRecorder::run()
{
    int seconds = 0;
    std::unique_lock lock(m_mutex);
    while (!m_stop) {
        if (m_wake.wait_for(lock, std::chrono::seconds(1), [this] { return m_stop; })) {
            break;
        }
        lock.unlock();
        tick(++seconds % 5 == 0);
        lock.lock();
    }
}

void PerformanceRecorder::tick(bool writeSession)
{
    std::uint64_t frames = 0;
    std::int64_t sumNs = 0;
    std::int64_t maxNs = 0;
    shared::FrameHistogram session;
    std::uint32_t vendor = 0;
    std::uint32_t device = 0;
    bool deviceChanged = false;
    {
        const std::scoped_lock lock(m_mutex);
        frames = m_secondFrames;
        sumNs = m_secondSumNs;
        maxNs = m_secondMaxNs;
        m_secondFrames = 0;
        m_secondSumNs = 0;
        m_secondMaxNs = 0;
        if (writeSession) {
            session = m_session;
        }
        vendor = m_vendor;
        device = m_device;
        deviceChanged = m_deviceChanged;
        m_deviceChanged = false;
    }

    if (deviceChanged) {
        m_sensors.find(m_sysfsRoot, vendor, device);
    }
    GpuSensors::Reading reading;
    // Only while the game draws: a paused game says nothing about its load.
    if (frames > 0 && m_sensors.read(reading)) {
        ++m_samples;
        m_edgeSum += reading.edge;
        m_hotspotSum += reading.hotspot;
        m_hotspotMax = std::max(m_hotspotMax, reading.hotspot);
        m_powerSum += reading.power;
        m_loadSum += reading.load;
    }

    std::string live = "# Written by gfxtune's Vulkan layer while the game runs.\n";
    live += "time=" + std::to_string(unixSeconds()) + "\n";
    live += "fps=" + formatNumber(sumNs > 0 ? double(frames) * 1e9 / double(sumNs) : 0.0) + "\n";
    live += "frame_ms=" + formatNumber(frames > 0 ? double(sumNs) / double(frames) / 1e6 : 0.0) + "\n";
    live += "frame_ms_max=" + formatNumber(double(maxNs) / 1e6) + "\n";
    writeFileAtomically(livePath(), live);

    if (!writeSession) {
        return;
    }
    std::string text = "# Written by gfxtune's Vulkan layer: one play session.\n";
    text += "start=" + std::to_string(m_start) + "\n";
    text += "updated=" + std::to_string(unixSeconds()) + "\n";
    text += "histogram=" + session.serialize() + "\n";
    text += "gpu_samples=" + std::to_string(m_samples) + "\n";
    text += "edge_sum=" + formatNumber(m_edgeSum) + "\n";
    text += "hotspot_sum=" + formatNumber(m_hotspotSum) + "\n";
    text += "hotspot_max=" + formatNumber(m_hotspotMax) + "\n";
    text += "power_sum=" + formatNumber(m_powerSum) + "\n";
    text += "load_sum=" + formatNumber(m_loadSum) + "\n";
    writeFileAtomically(sessionPath(), text);
}

void PerformanceRecorder::load()
{
    // Not readSmallFile: the histogram line can be longer than 4 KiB.
    std::ifstream file(sessionPath(), std::ios::binary);
    if (!file) {
        return;
    }
    std::string line;
    while (std::getline(file, line)) {
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string_view key(line.data(), equals);
        const std::string value = line.substr(equals + 1);
        double number = 0;
        const auto parsed = [&value, &number] {
            return std::from_chars(value.data(), value.data() + value.size(), number).ec == std::errc();
        };
        if (key == "start" && parsed()) {
            m_start = static_cast<long long>(number);
        } else if (key == "histogram") {
            m_session.parse(value);
        } else if (key == "gpu_samples" && parsed()) {
            m_samples = static_cast<std::uint64_t>(number);
        } else if (key == "edge_sum" && parsed()) {
            m_edgeSum = number;
        } else if (key == "hotspot_sum" && parsed()) {
            m_hotspotSum = number;
        } else if (key == "hotspot_max" && parsed()) {
            m_hotspotMax = number;
        } else if (key == "power_sum" && parsed()) {
            m_powerSum = number;
        } else if (key == "load_sum" && parsed()) {
            m_loadSum = number;
        }
    }
}

} // namespace gfxtune::layer
