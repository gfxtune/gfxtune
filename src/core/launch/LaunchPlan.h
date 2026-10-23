// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include "settings/GameSettings.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>
#include <utility>

namespace gfxtune::launch
{

struct Screen {
    int width = 0;
    int height = 0;
    int refreshRate = 0;
};

// What the helper knows when a game starts.
struct LaunchContext {
    // The environment Steam gave the game, including any variables set in
    // the game's own launch options.
    QHash<QString, QString> environment;
    bool gamescopeInstalled = false;
    std::optional<Screen> screen; // the main monitor, when it could be read
};

struct LaunchPlan {
    QList<std::pair<QString, QString>> environment; // to set
    QStringList wrapper;                            // runs before the game's command
    // For the "last launch" report, in plain words.
    QStringList applied;
    QStringList keptFromSteam; // left alone because the launch options set it
    QStringList notApplied;    // with the reason
};

bool needsScreen(const settings::GameSettings &settings);
LaunchPlan planLaunch(const settings::GameSettings &settings, const LaunchContext &context);

} // namespace gfxtune::launch
