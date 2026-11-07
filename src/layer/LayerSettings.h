// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include "filters/Filters.h"

#include <chrono>
#include <ctime>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace gfxtune::layer
{

// What the layer needs from gfxtune's settings files, as the app writes
// them: [filters], the frame cap from [frames] and the texture detail from
// [image]. The frame cap's switch and number are kept apart until the end,
// since a later file may set only one of them.
struct LayerSettings {
    filters::Values filters;
    bool frameCapEnabled = false;
    int frameCap = 0;
    int lodBias = 0;

    filters::LayerConfig config() const { return {filters, frameCapEnabled ? frameCap : 0, lodBias}; }
};

// Reads one file onto `settings`: what it sets wins, the rest stays.
// Unknown keys and values out of range are ignored, so a damaged file only
// ever means less is done.
void readLayerSettings(std::string_view text, LayerSettings &settings);
// One file onto the defaults.
filters::LayerConfig parseLayerConfig(std::string_view text);

// Follows the settings files while the game runs: the global settings, then
// the game's own on top. Reads them again when one changed, at most every
// `interval`; a missing file adds nothing. Safe to call from any thread.
class SettingsFile
{
public:
    explicit SettingsFile(std::vector<std::string> paths, std::chrono::milliseconds interval = std::chrono::milliseconds(500));

    // The current settings, re-read if a file changed since the last check.
    filters::LayerConfig config(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

private:
    struct Source {
        std::string path;
        timespec modified{};
        long long size = -1; // -1: missing
    };
    void reload();

    std::mutex m_mutex;
    std::vector<Source> m_sources;
    std::chrono::milliseconds m_interval;
    std::chrono::steady_clock::time_point m_lastCheck;
    bool m_checked = false;
    filters::LayerConfig m_config;
};

// Tells the game's page in gfxtune what happened inside the game. Written to
// a temporary file and renamed, so the app never reads half a report.
bool writeReport(const std::string &path, bool running, const std::string &detail);

} // namespace gfxtune::layer
