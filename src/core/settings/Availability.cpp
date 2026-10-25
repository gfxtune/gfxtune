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

    // A native game's libraries are a good guide. A Windows game's are not:
    // many link OpenGL without drawing with it, so under Proton an OpenGL
    // guess only earns a note.
    const bool openGl = onlyOpenGl(game.apis);
    const Option openGlOption = game.runtime == Runtime::Proton
        ? Option{true, QStringLiteral("If this game draws with OpenGL, this setting does not reach it.")}
        : Option{false, openGlNote};

    if (own.environment.contains(QStringLiteral("RADV_TEX_ANISO"))) {
        a.anisotropicFiltering = {false, setInSteam(QStringLiteral("RADV_TEX_ANISO"))};
    } else if (openGl) {
        a.anisotropicFiltering = openGlOption;
    }

    if (own.environment.contains(QStringLiteral("MESA_VK_WSI_PRESENT_MODE"))) {
        a.verticalSync = {false, setInSteam(QStringLiteral("MESA_VK_WSI_PRESENT_MODE"))};
    } else if (openGl) {
        a.verticalSync = openGlOption;
    }

    // Under Proton, DXVK and VKD3D-Proton cap the frame rate inside the game,
    // with or without gamescope. Other games only through gamescope, whose
    // screen holds a game back only while it waits for vertical sync.
    const bool gamescopeUsable = system.gamescopeInstalled && !own.gamescope;
    if (own.environment.contains(QStringLiteral("VKD3D_FRAME_RATE")) || own.environment.contains(QStringLiteral("DXVK_FRAME_RATE"))) {
        a.frameCap = {false, setInSteam(QStringLiteral("a frame rate variable"))};
    } else if (game.runtime == Runtime::Proton && (game.apis & ProtonApis)) {
        a.frameCap = {true, {}};
    } else if (game.runtime == Runtime::Proton && (!game.apis || openGl)) {
        a.frameCap = {true, QStringLiteral("Only takes effect if the game uses DirectX 9 to 12.")};
    } else if (game.runtime == Runtime::Proton) {
        a.frameCap = {false, QStringLiteral("This game draws with Vulkan itself, which the cap does not reach under Proton.")};
    } else if (settings.gamescope && gamescopeUsable) {
        a.frameCap = {true, QStringLiteral("gamescope holds the cap only while vertical sync is on, in the game or with Vertical sync set to On here.")};
    } else if (gamescopeUsable) {
        a.frameCap = {false, QStringLiteral("Turn on gamescope to cap this game's frame rate.")};
    } else if (own.gamescope) {
        a.frameCap = {false, QStringLiteral("This game's Steam launch options start gamescope; set the frame rate there.")};
    } else {
        a.frameCap = {false, QStringLiteral("Capping this game's frame rate needs gamescope, which is not installed.")};
    }
    if (a.frameCap.available && own.environment.contains(QStringLiteral("DXVK_CONFIG")) && game.runtime == Runtime::Proton) {
        a.frameCap.note = QStringLiteral("DirectX 9 to 11 games: this game's Steam launch options set DXVK_CONFIG, so the cap only reaches DirectX 12.");
    }

    const QString fsr4Cost = QStringLiteral("FSR 4 looks better than FSR 3.1 but costs some performance on RDNA 3 cards.");
    if (game.runtime != Runtime::Proton) {
        a.fsr4 = {false, QStringLiteral("Only for games that run through Proton.")};
    } else if (own.environment.contains(QStringLiteral("PROTON_FSR4_UPGRADE"))) {
        a.fsr4 = {false, setInSteam(QStringLiteral("PROTON_FSR4_UPGRADE"))};
    } else if (game.launchOptions.contains(QLatin1String("amdxcffx64"), Qt::CaseInsensitive)) {
        a.fsr4 = {false, setInSteam(QStringLiteral("a WINEDLLOVERRIDES entry for FSR 4"))};
    } else if (!game.fsr31Dx12) {
        a.fsr4 = {false, QStringLiteral("This game does not ship FSR 3.1, which FSR 4 replaces.")};
    } else {
        switch (game.tool.fsr4) {
        case steam::Fsr4Support::BuiltIn:
            a.fsr4 = {true, QStringLiteral("This game's Proton turns FSR 4 on by itself. ") + fsr4Cost};
            break;
        case steam::Fsr4Support::OnRequest:
            a.fsr4 = {true, QStringLiteral("This game's Proton keeps FSR 3.1 unless FSR 4 is set to On. ") + fsr4Cost};
            break;
        case steam::Fsr4Support::None:
            a.fsr4 = {false, QStringLiteral("This game's Proton cannot use FSR 4. Proton Experimental, proton-cachyos and GE-Proton can.")};
            break;
        case steam::Fsr4Support::Unknown:
            a.fsr4 = {true, QStringLiteral("gfxtune cannot tell whether Steam's default Proton turns FSR 4 on by itself. ") + fsr4Cost};
            break;
        }
    }

    // The layer reaches whatever draws with Vulkan: Vulkan games and, under
    // Proton, DirectX through DXVK and VKD3D-Proton. Not OpenGL.
    if (game.runtime == Runtime::Unknown) {
        a.filters = {false, QStringLiteral("gfxtune could not tell how this game runs.")};
    } else if (openGl && game.runtime != Runtime::Proton) {
        a.filters = {false, QStringLiteral("This game draws with OpenGL, which the filters cannot reach.")};
    } else if (openGl || !game.apis) {
        a.filters = {true, QStringLiteral("Only shows if the game draws with Vulkan or DirectX. Not while the game shows HDR.")};
    } else {
        a.filters = {true, QStringLiteral("Not while the game shows HDR.")};
    }
    if (a.filters.available && !game.antiCheat.isEmpty() && !settings.filtersRiskAccepted) {
        a.filtersLocked = true;
        a.filtersWarning = QStringLiteral("%1 ships %2. The filters run inside the game, like MangoHud does. No bans are known for this kind of filter, but nobody can promise there will never be one. Using them on this game is your responsibility.")
                               .arg(game.name, game.antiCheat.join(QStringLiteral(" and ")));
    }

    if (own.gamescope) {
        a.gamescope = {false, QStringLiteral("This game's Steam launch options already start gamescope.")};
    } else if (!system.gamescopeInstalled) {
        a.gamescope = {false, QStringLiteral("gamescope is not installed.")};
    }
    return a;
}

} // namespace gfxtune::settings
