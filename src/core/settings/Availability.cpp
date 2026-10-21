// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "Availability.h"

#include <QStandardPaths>

namespace gfxtune::settings
{

using steam::GraphicsApi;
using steam::GraphicsApis;
using steam::Runtime;

namespace
{

// The Vulkan-level settings (anisotropic filtering, vertical sync) reach
// every Vulkan game, which includes every DirectX game under Proton. Only a
// game that draws with OpenGL alone is out of reach.
bool onlyOpenGl(GraphicsApis apis)
{
    return apis == GraphicsApis(GraphicsApi::OpenGL);
}

constexpr auto DxvkApis = GraphicsApi::DirectX9 | GraphicsApi::DirectX10 | GraphicsApi::DirectX11;

} // namespace

SystemInfo SystemInfo::detect()
{
    SystemInfo info;
    info.gamescopeInstalled = !QStandardPaths::findExecutable(QStringLiteral("gamescope")).isEmpty();
    return info;
}

Availability availabilityFor(const steam::Game &game, const GameSettings &settings, const SystemInfo &system)
{
    Availability a;
    const QString openGlNote = QStringLiteral("This game draws with OpenGL, which this setting does not reach.");

    if (onlyOpenGl(game.apis)) {
        a.anisotropicFiltering = {false, openGlNote};
        a.verticalSync = {false, openGlNote};
    }

    if (settings.gamescope && system.gamescopeInstalled) {
        a.frameCap = {true, QStringLiteral("gamescope caps the frame rate.")};
    } else if (game.runtime == Runtime::Proton && (game.apis & DxvkApis)) {
        a.frameCap = {true, {}};
    } else if (game.runtime == Runtime::Proton && !game.apis) {
        a.frameCap = {true, QStringLiteral("Only takes effect if the game uses DirectX 9, 10 or 11.")};
    } else if (system.gamescopeInstalled) {
        a.frameCap = {false, QStringLiteral("Turn on gamescope to cap this game's frame rate.")};
    } else {
        a.frameCap = {false, QStringLiteral("Capping this game's frame rate needs gamescope, which is not installed.")};
    }

    if (game.runtime != Runtime::Proton) {
        a.fsr4Upgrade = {false, QStringLiteral("Only for games that run through Proton.")};
    } else if (!game.fsr31Dx12) {
        a.fsr4Upgrade = {false, QStringLiteral("This game does not ship FSR 3.1, which the upgrade replaces.")};
    }

    if (!system.gamescopeInstalled) {
        a.gamescope = {false, QStringLiteral("gamescope is not installed.")};
    }
    return a;
}

} // namespace gfxtune::settings
