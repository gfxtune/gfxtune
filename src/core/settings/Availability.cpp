// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "Availability.h"

#include "launch/LaunchLine.h"

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

// DXVK covers DirectX 9 to 11 and VKD3D-Proton DirectX 12; both can cap the frame rate.
constexpr auto ProtonApis = GraphicsApi::DirectX9 | GraphicsApi::DirectX10 | GraphicsApi::DirectX11 | GraphicsApi::DirectX12;

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
    // The game's own Steam launch options win over gfxtune.
    const launch::OwnSettings own = launch::ownSettings(game.launchOptions);
    auto setInSteam = [](const QString &what) {
        return QStringLiteral("Set by %1 in this game's Steam launch options, which gfxtune leaves alone.").arg(what);
    };

    if (own.environment.contains(QStringLiteral("RADV_TEX_ANISO"))) {
        a.anisotropicFiltering = {false, setInSteam(QStringLiteral("RADV_TEX_ANISO"))};
    } else if (onlyOpenGl(game.apis)) {
        a.anisotropicFiltering = {false, openGlNote};
    }

    if (own.environment.contains(QStringLiteral("MESA_VK_WSI_PRESENT_MODE"))) {
        a.verticalSync = {false, setInSteam(QStringLiteral("MESA_VK_WSI_PRESENT_MODE"))};
    } else if (onlyOpenGl(game.apis)) {
        a.verticalSync = {false, openGlNote};
    }

    const bool gamescopeUsable = system.gamescopeInstalled && !own.gamescope;
    if (settings.gamescope && gamescopeUsable) {
        a.frameCap = {true, QStringLiteral("gamescope caps the frame rate.")};
    } else if (own.environment.contains(QStringLiteral("VKD3D_FRAME_RATE")) || own.environment.contains(QStringLiteral("DXVK_FRAME_RATE"))) {
        a.frameCap = {false, setInSteam(QStringLiteral("a frame rate variable"))};
    } else if (game.runtime == Runtime::Proton && (game.apis & ProtonApis)) {
        a.frameCap = {true, {}};
    } else if (game.runtime == Runtime::Proton && !game.apis) {
        a.frameCap = {true, QStringLiteral("Only takes effect if the game uses DirectX 9 to 12.")};
    } else if (gamescopeUsable) {
        a.frameCap = {false, QStringLiteral("Turn on gamescope to cap this game's frame rate.")};
    } else if (own.gamescope) {
        a.frameCap = {false, QStringLiteral("This game's Steam launch options start gamescope; set the frame rate there.")};
    } else {
        a.frameCap = {false, QStringLiteral("Capping this game's frame rate needs gamescope, which is not installed.")};
    }
    if (a.frameCap.available && own.environment.contains(QStringLiteral("DXVK_CONFIG")) && !settings.gamescope) {
        a.frameCap.note = QStringLiteral("DirectX 9 to 11 games: this game's Steam launch options set DXVK_CONFIG, so the cap only reaches DirectX 12.");
    }

    if (game.runtime != Runtime::Proton) {
        a.fsr4Upgrade = {false, QStringLiteral("Only for games that run through Proton.")};
    } else if (own.environment.contains(QStringLiteral("PROTON_FSR4_UPGRADE"))) {
        a.fsr4Upgrade = {false, setInSteam(QStringLiteral("PROTON_FSR4_UPGRADE"))};
    } else if (!game.fsr31Dx12) {
        a.fsr4Upgrade = {false, QStringLiteral("This game does not ship FSR 3.1, which the upgrade replaces.")};
    }

    if (own.gamescope) {
        a.gamescope = {false, QStringLiteral("This game's Steam launch options already start gamescope.")};
    } else if (!system.gamescopeInstalled) {
        a.gamescope = {false, QStringLiteral("gamescope is not installed.")};
    }
    return a;
}

} // namespace gfxtune::settings
