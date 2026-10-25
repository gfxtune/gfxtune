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

} // namespace

filters::Values parseFilterSettings(std::string_view text)
{
    filters::Values values;
    bool inFilters = false;
    while (!text.empty()) {
        const std::size_t newline = text.find('\n');
        const std::string_view line = trimmed(text.substr(0, newline));
        text.remove_prefix(newline == std::string_view::npos ? text.size() : newline + 1);

        if (line.empty() || line.front() == ';' || line.front() == '#') {
            continue;
        }
        if (line.front() == '[') {
            inFilters = line == "[filters]";
            continue;
        }
        const std::size_t equals = line.find('=');
        if (!inFilters || equals == std::string_view::npos) {
            continue;
        }
        const std::string_view key = trimmed(line.substr(0, equals));
        const std::string_view value = trimmed(line.substr(equals + 1));

        if (key == "enabled") {
            if (value == "true" || value == "false") {
                values.enabled = value == "true";
            }
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
        }
    }
    return values;
}

SettingsFile::SettingsFile(std::string path, std::chrono::milliseconds interval)
    : m_path(std::move(path))
    , m_interval(interval)
{
}

const filters::Values &SettingsFile::values(std::chrono::steady_clock::time_point now)
{
    if (m_checked && now - m_lastCheck < m_interval) {
        return m_values;
    }
    m_checked = true;
    m_lastCheck = now;

    struct stat info{};
    if (::stat(m_path.c_str(), &info) != 0) {
        // Deleted, as "Reset" or "Remove All" do: back to the original picture.
        m_values = filters::Values{};
        m_size = -1;
        return m_values;
    }
    if (info.st_mtim.tv_sec != m_modified.tv_sec || info.st_mtim.tv_nsec != m_modified.tv_nsec || info.st_size != m_size) {
        m_modified = info.st_mtim;
        m_size = info.st_size;
        reload();
    }
    return m_values;
}

void SettingsFile::reload()
{
    // The app writes a few hundred bytes; never read much more than that.
    constexpr std::streamsize MaxSize = 64 * 1024;
    std::ifstream file(m_path, std::ios::binary);
    std::string text(static_cast<std::size_t>(MaxSize), '\0');
    file.read(text.data(), MaxSize);
    text.resize(static_cast<std::size_t>(file.gcount()));
    m_values = parseFilterSettings(text);
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
