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
    // The game's command runs through Proton, where DXVK and VKD3D-Proton cap
    // the frame rate exactly, inside the game.
    bool proton = false;
    // Where gfxtune's Vulkan layer reads the game's filter settings while it
    // runs, and where it reports back.
    QString filterSettingsPath;
    // The global settings, which the layer reads first, when the game
    // follows them.
    QString globalSettingsPath;
    QString filterReportPath;
    // Where the layer keeps its frame-time measurements for this game.
    QString performanceDir;
    // What gfxtune found out about the game when it last looked.
    settings::GameFacts facts;
    bool gameModeInstalled = false;
    bool mangoHudInstalled = false;
    bool mangoAppInstalled = false; // MangoHud for gamescope
    bool lsfgVkInstalled = false;
    // Lossless Scaling's frame generation file, checked to be there: without
    // it lsfg-vk makes the game fail to create its swapchain.
    QString lsfgDll;
    // gfxtune's lsfg-vk settings file for this game, once written.
    QString lsfgConfigPath;
};

struct LaunchPlan {
    QList<std::pair<QString, QString>> environment; // to set
    QStringList wrapper;                            // runs before the game's command
    // For the "last launch" report, in plain words.
    QStringList applied;
    QStringList keptFromSteam; // left alone because the launch options set it
    QStringList notApplied;    // with the reason
    bool filters = false;         // the in-game filters will be drawn
    bool layer = false;           // gfxtune's Vulkan layer was asked for
    bool frameGeneration = false; // lsfg-vk was asked to generate frames
};

bool needsScreen(const settings::GameSettings &settings);
// Whether a game's command, as Steam passes it on, starts Proton: a program
// named "proton" followed by one of its verbs, directly or after the Steam
// Linux Runtime's entry point.
bool runsThroughProton(const QStringList &command);
LaunchPlan planLaunch(const settings::GameSettings &settings, const LaunchContext &context);

} // namespace gfxtune::launch
