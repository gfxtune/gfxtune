// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "Availability.h"

#include "framegen/LsfgVk.h"
#include "launch/LaunchLine.h"

#include <KLocalizedString>
#include <QLocale>
#include <QStandardPaths>

namespace gfxtune::settings
{

using steam::GraphicsApi;
using steam::GraphicsApis;
using steam::Runtime;

namespace
{

// DXVK covers DirectX 9 to 11 and VKD3D-Proton DirectX 12; both can cap the frame rate.
constexpr auto ProtonApis = GraphicsApi::DirectX9 | GraphicsApi::DirectX10 | GraphicsApi::DirectX11 | GraphicsApi::DirectX12;

// What frame generation needs on this computer, whatever the game: nothing
// when it is all there.
QString frameGenerationMissing(const SystemInfo &system)
{
    const steam::LosslessScaling &lossless = system.losslessScaling;
    if (!system.lsfgVkInstalled) {
        return i18n("Needs lsfg-vk, which is not installed. On CachyOS: sudo pacman -S lsfg-vk");
    }
    if (!lossless.installed) {
        return i18n("Needs Lossless Scaling from Steam, a paid app, switched to its lsfg-vk branch.");
    }
    if (lossless.branch != QLatin1String(framegen::LsfgVkBranch)) {
        return i18n("Switch Lossless Scaling to its lsfg-vk branch: in Steam, Lossless Scaling, Properties, Betas.");
    }
    if (lossless.dll.isEmpty()) {
        return i18n("Lossless Scaling's frame generation file is missing; let Steam finish updating it.");
    }
    return {};
}

QString frameGenerationCredit()
{
    return i18n(
        "By lsfg-vk (lsfg-vk.dev), installed separately, with Lossless Scaling's frame generation. Turning it off takes effect "
        "the next time the game starts.");
}

} // namespace

SystemInfo SystemInfo::detect()
{
    SystemInfo info;
    info.gamescopeInstalled = !QStandardPaths::findExecutable(QStringLiteral("gamescope")).isEmpty();
    info.gameModeInstalled = !QStandardPaths::findExecutable(QStringLiteral("gamemoderun")).isEmpty();
    info.mangoHudInstalled = !QStandardPaths::findExecutable(QStringLiteral("mangohud")).isEmpty();
    info.mangoAppInstalled = !QStandardPaths::findExecutable(QStringLiteral("mangoapp")).isEmpty();
    info.lsfgVkInstalled = framegen::layerInstalled(framegen::implicitLayerDirs());
    return info;
}

GameFacts factsOf(const steam::Game &game)
{
    GameFacts facts;
    facts.antiCheat = game.antiCheat;
    facts.openGlOnly = game.runtime == Runtime::Native && steam::drawsOnlyWithOpenGl(game.apis);
    facts.openGlSeen = game.runtime == Runtime::Native && game.openGlSeen;
    return facts;
}

Availability availabilityFor(const steam::Game &game, const GameSettings &settings, const SystemInfo &system)
{
    Availability a;
    const QString openGlNote = i18n("This game draws with OpenGL, which this setting does not reach.");
    // The game's own Steam launch options win over gfxtune.
    const launch::OwnSettings own = launch::ownSettings(game.launchOptions);
    const auto setInSteam = [](const QString &what) {
        return i18n("Set by %1 in this game's Steam launch options, which gfxtune leaves alone.", what);
    };

    // A native game's libraries are a good guide. A Windows game's are not:
    // many link OpenGL without drawing with it, so under Proton an OpenGL
    // guess only earns a note.
    // The Vulkan-level settings reach every Vulkan game, which includes every
    // DirectX game under Proton. Only a game that draws with OpenGL alone is
    // out of reach.
    const bool openGl = steam::drawsOnlyWithOpenGl(game.apis) || (game.runtime == Runtime::Native && game.openGlSeen);
    const GameFacts facts = factsOf(game);
    // gfxtune's layer goes in for measuring or filters, unless anti-cheat
    // locks it; it then also caps the frame rate and sets texture detail.
    const bool layer = layerWanted(settings, facts) && game.runtime != Runtime::Unknown;
    const bool layerLocked = !layerAllowed(settings, facts);
    const QString lockedLayer = i18n("gfxtune's layer, locked until you accept the risk under In-game filters");
    const Option openGlOption = game.runtime == Runtime::Proton
        ? Option{true, i18n("If this game draws with OpenGL, this setting does not reach it.")}
        : Option{false, openGlNote};

    if (own.environment.contains(QStringLiteral("RADV_TEX_ANISO"))) {
        a.anisotropicFiltering = {false, setInSteam(QStringLiteral("RADV_TEX_ANISO"))};
    } else if (openGl) {
        a.anisotropicFiltering = openGlOption;
    }

    if (own.environment.contains(QStringLiteral("MESA_VK_WSI_PRESENT_MODE"))) {
        a.verticalSync = {false, setInSteam(QStringLiteral("MESA_VK_WSI_PRESENT_MODE"))};
    } else if (openGl) {
        // Mesa's own switch reaches OpenGL games, with only off and on.
        a.verticalSync = {true, i18n("OpenGL games know only Off and On.")};
    }

    // Under Proton, DXVK and VKD3D-Proton cap the frame rate inside the game,
    // with or without gamescope. Other games only through gamescope, whose
    // screen holds a game back only while it waits for vertical sync.
    const bool gamescopeUsable = system.gamescopeInstalled && !own.gamescope;
    if (own.environment.contains(QStringLiteral("VKD3D_FRAME_RATE")) || own.environment.contains(QStringLiteral("DXVK_FRAME_RATE"))) {
        a.frameCap = {false, setInSteam(i18n("a frame rate variable"))};
    } else if (layer && !openGl) {
        a.frameCap = {true, game.apis ? QString() : i18n("Only takes effect if the game draws with Vulkan or DirectX.")};
    } else if (game.runtime == Runtime::Proton && (game.apis & ProtonApis)) {
        a.frameCap = {true, {}};
    } else if (game.runtime == Runtime::Proton && (!game.apis || openGl)) {
        a.frameCap = {true, i18n("Only takes effect if the game uses DirectX 9 to 12.")};
    } else if (game.runtime == Runtime::Proton) {
        a.frameCap = {false,
                      layerLocked ? i18n("This game draws with Vulkan itself: the cap needs %1.", lockedLayer)
                                  : i18n("This game draws with Vulkan itself: the cap needs performance measuring or a filter on.")};
    } else if (settings.gamescope && gamescopeUsable) {
        a.frameCap = {true,
                      i18n("gamescope holds the cap only while vertical sync is on, in the game or with Vertical sync set to On here.")};
    } else if (gamescopeUsable) {
        a.frameCap = {false,
                      openGl            ? i18n("Turn on gamescope to cap this game's frame rate.")
                          : layerLocked ? i18n("Turn on gamescope to cap this game's frame rate, or accept the risk under In-game filters.")
                                        : i18n("Turn on performance measuring or gamescope to cap this game's frame rate.")};
    } else if (own.gamescope) {
        a.frameCap = {false, i18n("This game's Steam launch options start gamescope; set the frame rate there.")};
    } else {
        a.frameCap = {false, i18n("Capping this game's frame rate needs gamescope, which is not installed.")};
    }
    if (a.frameCap.available && !layer && own.environment.contains(QStringLiteral("DXVK_CONFIG")) && game.runtime == Runtime::Proton) {
        a.frameCap.note =
            i18n("DirectX 9 to 11 games: this game's Steam launch options set DXVK_CONFIG, so the cap only reaches DirectX 12.");
    }

    const QString fsr4Cost = i18n("FSR 4 looks better than FSR 3.1 but costs some performance on RDNA 3 cards.");
    if (game.runtime != Runtime::Proton) {
        a.fsr4 = {false, i18n("Only for games that run through Proton.")};
    } else if (own.environment.contains(QStringLiteral("PROTON_FSR4_UPGRADE"))) {
        a.fsr4 = {false, setInSteam(QStringLiteral("PROTON_FSR4_UPGRADE"))};
    } else if (game.launchOptions.contains(QLatin1String("amdxcffx64"), Qt::CaseInsensitive)) {
        a.fsr4 = {false, setInSteam(i18n("a WINEDLLOVERRIDES entry for FSR 4"))};
    } else if (!game.fsr31Dx12) {
        a.fsr4 = {false, i18n("This game does not ship FSR 3.1, which FSR 4 replaces.")};
    } else {
        switch (game.tool.fsr4) {
        case steam::Fsr4Support::BuiltIn:
            a.fsr4 = {true, i18nc("%1 is what FSR 4 costs", "This game's Proton turns FSR 4 on by itself. %1", fsr4Cost)};
            break;
        case steam::Fsr4Support::OnRequest:
            a.fsr4 = {true, i18nc("%1 is what FSR 4 costs", "This game's Proton keeps FSR 3.1 unless FSR 4 is set to On. %1", fsr4Cost)};
            break;
        case steam::Fsr4Support::None:
            a.fsr4 = {false, i18n("This game's Proton cannot use FSR 4. Proton Experimental, proton-cachyos and GE-Proton can.")};
            break;
        case steam::Fsr4Support::Unknown:
            a.fsr4 = {true,
                      i18nc("%1 is what FSR 4 costs", "gfxtune cannot tell whether Steam's default Proton turns FSR 4 on by itself. %1",
                            fsr4Cost)};
            break;
        }
    }

    // The layer reaches whatever draws with Vulkan: Vulkan games and, under
    // Proton, DirectX through DXVK and VKD3D-Proton. Not OpenGL.
    if (game.runtime == Runtime::Unknown) {
        a.filters = {false, i18n("gfxtune could not tell how this game runs.")};
    } else if (openGl && game.runtime != Runtime::Proton) {
        a.filters = {false, i18n("This game draws with OpenGL, which the filters cannot reach.")};
    } else if (openGl || !game.apis) {
        a.filters = {true, i18n("Only shows if the game draws with Vulkan or DirectX. Not while the game shows HDR.")};
    } else {
        a.filters = {true, i18n("Not while the game shows HDR.")};
    }
    if (a.filters.available && !game.antiCheat.isEmpty() && !settings.filtersRiskAccepted) {
        a.filtersLocked = true;
        a.filtersWarning = i18n(
            "%1 ships %2. The filters and performance measuring run inside the game, like MangoHud does. No bans are known for this "
            "kind of tool, but nobody can promise there will never be one. Using them on this game is your responsibility.",
            game.name, QLocale().createSeparatedList(game.antiCheat));
    }

    // lsfg-vk is a Vulkan layer too: it reaches what the filters reach.
    const QString missing = frameGenerationMissing(system);
    if (own.environment.contains(QStringLiteral("LSFGVK_PROFILE")) || own.environment.contains(QStringLiteral("LSFGVK_ENV"))
        || own.environment.contains(QStringLiteral("LSFGVK_CONFIG")) || own.environment.contains(QStringLiteral("DISABLE_LSFGVK"))) {
        a.frameGeneration = {false, setInSteam(QStringLiteral("lsfg-vk"))};
    } else if (game.runtime == Runtime::Unknown) {
        a.frameGeneration = {false, i18n("gfxtune could not tell how this game runs.")};
    } else if (openGl && game.runtime != Runtime::Proton) {
        a.frameGeneration = {false, i18n("This game draws with OpenGL, which lsfg-vk cannot reach.")};
    } else if (!missing.isEmpty()) {
        a.frameGeneration = {false, missing};
    } else if (a.filtersLocked) {
        a.frameGeneration = {false, i18n("Locked until you accept the risk under In-game filters.")};
    } else if (openGl || !game.apis) {
        a.frameGeneration = {
            true, i18nc("%1 is who makes it", "Only works if the game draws with Vulkan or DirectX. %1", frameGenerationCredit())};
    } else {
        a.frameGeneration = {true, frameGenerationCredit()};
    }
    if (a.frameGeneration.available && settings.frameGeneration > 0 && a.verticalSync.available) {
        a.verticalSync = {false, i18n("lsfg-vk keeps vertical sync on while it generates frames.")};
    }

    if (own.gamescope) {
        a.gamescope = {false, i18n("This game's Steam launch options already start gamescope.")};
    } else if (!system.gamescopeInstalled) {
        a.gamescope = {false, i18n("gamescope is not installed.")};
    } else if (settings.supersampling()) {
        a.gamescope.note = i18n(
            "Supersampling: the game draws bigger than the screen and is scaled down smoothly; the upscaler and its sharpness are not "
            "used.");
    }

    // Texture detail: the layer sets it for every sampler; without it DXVK
    // can for DirectX 9 to 11.
    if (game.launchOptions.contains(QLatin1String("samplerLodBias"))) {
        a.lodBias = {false, setInSteam(QStringLiteral("samplerLodBias"))};
    } else if (openGl && game.runtime != Runtime::Proton) {
        a.lodBias = {false, openGlNote};
    } else if (layer) {
        a.lodBias = {true, i18n("Takes effect the next time the game starts. Sharper textures can shimmer.")};
    } else if (game.runtime == Runtime::Proton) {
        a.lodBias = {true, i18n("Reaches DirectX 9 to 11 only while performance measuring and the filters are off.")};
    } else if (layerLocked) {
        a.lodBias = {false, i18n("Needs %1.", lockedLayer)};
    } else {
        a.lodBias = {false, i18n("Needs performance measuring or a filter on for this game.")};
    }

    if (game.runtime == Runtime::Unknown) {
        a.measurePerformance = {false, i18n("gfxtune could not tell how this game runs.")};
    } else if (openGl && game.runtime != Runtime::Proton) {
        a.measurePerformance = {false, i18n("This game draws with OpenGL, which gfxtune cannot measure.")};
    } else {
        a.measurePerformance = {true,
                                i18n("Frame rate, frame times and the card's readings while you play, kept for each session. It "
                                     "reads the clock once per frame and nothing more.")};
    }
    if (a.measurePerformance.available && a.filtersLocked) {
        a.measurePerformance.note = i18n("Locked until you accept the risk under In-game filters.");
    }

    if (own.gameMode) {
        a.gameMode = {false, i18n("This game's Steam launch options already start gamemoderun.")};
    } else if (!system.gameModeInstalled) {
        a.gameMode = {false, i18n("GameMode is not installed.")};
    } else {
        a.gameMode.note = i18n("Asks the system for the processor's full speed and the right priorities while the game runs.");
    }
    if (own.mangoHud || own.environment.contains(QStringLiteral("MANGOHUD"))) {
        a.mangoHud = {false, i18n("This game's Steam launch options already start MangoHud.")};
    } else if (own.gamescope) {
        a.mangoHud = {false, i18n("This game's Steam launch options start gamescope; add --mangoapp to its options there for MangoHud.")};
    } else if (settings.gamescope && a.gamescope.available) {
        if (system.mangoAppInstalled) {
            a.mangoHud.note = i18n("Inside gamescope, gamescope shows it through mangoapp. MangoHud's own settings decide what it shows.");
        } else {
            a.mangoHud = {false, i18n("Inside gamescope MangoHud needs mangoapp, which is not installed.")};
        }
    } else if (!system.mangoHudInstalled) {
        a.mangoHud = {false, i18n("MangoHud is not installed.")};
    } else {
        a.mangoHud.note = i18n("MangoHud's own settings decide what it shows; Right Shift+F12 shows or hides it.");
    }
    return a;
}

Availability availabilityForAll(const GameSettings &settings, const SystemInfo &system)
{
    Availability a;
    a.verticalSync.note = i18n("OpenGL games know only Off and On.");
    a.lodBias.note =
        i18n("Reaches every game gfxtune measures or filters, OpenGL ones aside; otherwise only DirectX 9 to 11 games under Proton.");
    a.frameCap.note = i18n("Exact in every game gfxtune measures; otherwise through DXVK and VKD3D-Proton, or gamescope.");
    a.fsr4.note = i18n("Reaches games that run through Proton and ship FSR 3.1.");
    // While either is on, gfxtune keeps out of games with anti-cheat (see
    // holdsBackForAntiCheat()).
    const QString antiCheat =
        i18n("While on, gfxtune keeps out of games with anti-cheat until you accept the risk or change a setting on their page.");
    a.filters.note =
        i18nc("%1 is the note about anti-cheat", "Reach games that draw with Vulkan or DirectX, not while a game shows HDR. %1", antiCheat);
    a.measurePerformance.note =
        i18nc("%1 is the note about anti-cheat",
              "Frame rate, frame times and the card's readings while you play, kept for each session. %1", antiCheat);
    if (!system.gamescopeInstalled) {
        a.gamescope = {false, i18n("gamescope is not installed.")};
    } else if (settings.supersampling()) {
        a.gamescope.note = i18n(
            "Supersampling: games draw bigger than the screen and are scaled down smoothly; the upscaler and its sharpness are not used.");
    }
    if (const QString missing = frameGenerationMissing(system); !missing.isEmpty()) {
        a.frameGeneration = {false, missing};
    } else {
        a.frameGeneration.note = i18nc("%1 is who makes it, %2 the note about anti-cheat",
                                       "Reaches games that draw with Vulkan or DirectX. %1 %2", frameGenerationCredit(), antiCheat);
        if (settings.frameGeneration > 0) {
            a.verticalSync = {false, i18n("lsfg-vk keeps vertical sync on while it generates frames.")};
        }
    }
    if (!system.gameModeInstalled) {
        a.gameMode = {false, i18n("GameMode is not installed.")};
    }
    if (settings.gamescope && a.gamescope.available) {
        a.mangoHud = system.mangoAppInstalled ? Option{true, i18n("Inside gamescope, gamescope shows it through mangoapp.")}
                                              : Option{false, i18n("Inside gamescope MangoHud needs mangoapp, which is not installed.")};
    } else if (!system.mangoHudInstalled) {
        a.mangoHud = {false, i18n("MangoHud is not installed.")};
    } else {
        a.mangoHud.note = i18n("MangoHud's own settings decide what it shows; Right Shift+F12 shows or hides it.");
    }
    return a;
}

} // namespace gfxtune::settings
