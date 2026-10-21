// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include "GameSettings.h"
#include "steam/Game.h"

#include <QString>

namespace gfxtune::settings
{

// Whether a setting can do anything for a game, and a short note for the
// user: why it is unavailable, or what limits it.
struct Option {
    bool available = true;
    QString note;
};

struct Availability {
    Option anisotropicFiltering;
    Option verticalSync;
    Option frameCap;
    Option fsr4Upgrade;
    Option gamescope;
};

struct SystemInfo {
    bool gamescopeInstalled = false;

    // Looks at this computer.
    static SystemInfo detect();
};

Availability availabilityFor(const steam::Game &game, const GameSettings &settings, const SystemInfo &system);

} // namespace gfxtune::settings
