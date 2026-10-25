// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include "filters/Filters.h"

#include <chrono>
#include <ctime>
#include <string>
#include <string_view>

namespace gfxtune::layer
{

// The [filters] section of a game's gfxtune settings file, as the app writes
// it. Unknown keys and values out of range are ignored, so a damaged file
// only ever means fewer filters.
filters::Values parseFilterSettings(std::string_view text);

// Follows one settings file while the game runs: reads it again when it
// changed, at most every `interval`.
class SettingsFile
{
public:
    explicit SettingsFile(std::string path, std::chrono::milliseconds interval = std::chrono::milliseconds(500));

    // The current values, re-read if the file changed since the last check.
    const filters::Values &values(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

private:
    void reload();

    std::string m_path;
    std::chrono::milliseconds m_interval;
    std::chrono::steady_clock::time_point m_lastCheck{};
    bool m_checked = false;
    timespec m_modified{};
    long long m_size = -1;
    filters::Values m_values;
};

// Tells the game's page in gfxtune what happened inside the game. Written to
// a temporary file and renamed, so the app never reads half a report.
bool writeReport(const std::string &path, bool running, const std::string &detail);

} // namespace gfxtune::layer
