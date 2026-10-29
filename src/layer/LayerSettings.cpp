// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "LayerSettings.h"

#include <charconv>
#include <cstdio>
#include <fstream>
#include <sstream>

#include <sys/stat.h>
#include <unistd.h>

namespace gfxtune::layer
{

namespace
{

std::string_view trimmed(std::string_view text)
{
    const auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
    while (!text.empty() && blank(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && blank(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

bool readNumber(std::string_view text, const filters::Range &range, int &out)
{
    int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc() || end != text.data() + text.size() || value < range.min || value > range.max) {
        return false;
    }
    out = value;
    return true;
}

void readFlag(std::string_view text, bool &out)
{
    if (text == "true" || text == "false") {
        out = text == "true";
    }
}

} // namespace

filters::LayerConfig parseLayerConfig(std::string_view text)
{
    filters::LayerConfig config;
    filters::Values &values = config.filters;
    bool capEnabled = false;
    int cap = 0;
    std::string_view section;
    while (!text.empty()) {
        const std::size_t newline = text.find('\n');
        const std::string_view line = trimmed(text.substr(0, newline));
        text.remove_prefix(newline == std::string_view::npos ? text.size() : newline + 1);

        if (line.empty() || line.front() == ';' || line.front() == '#') {
            continue;
        }
        if (line.front() == '[') {
            section = line;
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            continue;
        }
        const std::string_view key = trimmed(line.substr(0, equals));
        const std::string_view value = trimmed(line.substr(equals + 1));

        if (section == "[frames]") {
            if (key == "frame_cap_enabled") {
                readFlag(value, capEnabled);
            } else if (key == "frame_cap") {
                readNumber(value, filters::Range{10, 1000, 0}, cap);
            }
            continue;
        }
        if (section == "[image]") {
            if (key == "lod_bias") {
                readNumber(value, filters::LodBias, config.lodBias);
            }
            continue;
        }
        if (section != "[filters]") {
            continue;
        }
        if (key == "enabled") {
            readFlag(value, values.enabled);
        } else if (key == "sharpening") {
            readNumber(value, filters::Sharpening, values.sharpening);
        } else if (key == "brightness") {
            readNumber(value, filters::Brightness, values.brightness);
        } else if (key == "contrast") {
            readNumber(value, filters::Contrast, values.contrast);
        } else if (key == "gamma") {
            readNumber(value, filters::Gamma, values.gamma);
        } else if (key == "saturation") {
            readNumber(value, filters::Saturation, values.saturation);
        } else if (key == "vibrance") {
            readNumber(value, filters::Vibrance, values.vibrance);
        } else if (key == "temperature") {
            readNumber(value, filters::Temperature, values.temperature);
        } else if (key == "anti_aliasing") {
            for (int i = 0; i <= filters::AntiAliasing.max; ++i) {
                if (value == filters::AntiAliasingNames[i]) {
                    values.antiAliasing = i;
                }
            }
        }
    }
    config.frameCap = capEnabled ? cap : 0;
    return config;
}

SettingsFile::SettingsFile(std::string path, std::chrono::milliseconds interval)
    : m_path(std::move(path))
    , m_interval(interval)
{
}

filters::LayerConfig SettingsFile::config(std::chrono::steady_clock::time_point now)
{
    std::lock_guard lock(m_mutex);
    if (m_checked && now - m_lastCheck < m_interval) {
        return m_config;
    }
    m_checked = true;
    m_lastCheck = now;

    struct stat info{};
    if (::stat(m_path.c_str(), &info) != 0) {
        // Deleted, as "Reset" or "Remove All" do: back to the original picture.
        m_config = filters::LayerConfig{};
        m_size = -1;
        return m_config;
    }
    if (info.st_mtim.tv_sec != m_modified.tv_sec || info.st_mtim.tv_nsec != m_modified.tv_nsec || info.st_size != m_size) {
        m_modified = info.st_mtim;
        m_size = info.st_size;
        reload();
    }
    return m_config;
}

void SettingsFile::reload()
{
    // The app writes a few hundred bytes; never read much more than that.
    constexpr std::streamsize MaxSize = 64 * 1024;
    std::ifstream file(m_path, std::ios::binary);
    std::string text(static_cast<std::size_t>(MaxSize), '\0');
    file.read(text.data(), MaxSize);
    text.resize(static_cast<std::size_t>(file.gcount()));
    m_config = parseLayerConfig(text);
}

bool writeReport(const std::string &path, bool running, const std::string &detail)
{
    if (path.empty()) {
        return false;
    }
    const std::string temporary = path + ".tmp" + std::to_string(::getpid());
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out << "# Written by gfxtune's Vulkan layer from inside the game.\n"
            << "time=" << static_cast<long long>(std::time(nullptr)) << '\n'
            << "state=" << (running ? "running" : "skipped") << '\n'
            << "detail=" << detail << '\n';
        if (!out.flush()) {
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

} // namespace gfxtune::layer
