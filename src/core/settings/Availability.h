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
    Option fsr4;
    Option gamescope;
    Option lodBias;
    Option measurePerformance;
    Option gameMode;
    Option mangoHud;
    // The in-game filters as a whole.
    Option filters;
    // Anti-cheat found in the game while the user has not accepted the risk:
    // the filters stay locked and `filtersWarning` says why.
    bool filtersLocked = false;
    QString filtersWarning;
};

struct SystemInfo {
    bool gamescopeInstalled = false;
    bool gameModeInstalled = false;
    bool mangoHudInstalled = false;
    bool mangoAppInstalled = false; // MangoHud for gamescope

    // Looks at this computer.
    static SystemInfo detect();
};

Availability availabilityFor(const steam::Game &game, const GameSettings &settings, const SystemInfo &system);
// For the global settings: what this computer allows, with notes on where
// a setting does not reach. Each game's page says what applies to it.
Availability availabilityForAll(const GameSettings &settings, const SystemInfo &system);

// What gfxtune-run needs to know about a game, from what the scan found.
GameFacts factsOf(const steam::Game &game);

} // namespace gfxtune::settings
