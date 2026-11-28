// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "Commands.h"

#include "framegen/LsfgVk.h"
#include "launch/LaunchLines.h"
#include "settings/SettingsStore.h"
#include "steam/SteamProcess.h"

#include "steam/RunningGames.h"
#include "steam/SteamLibrary.h"

#include <KLocalizedString>

namespace gfxtune
{

using namespace steam;

namespace
{

QString fsr4Words(Fsr4Support support)
{
    switch (support) {
    case Fsr4Support::BuiltIn:
        return QStringLiteral("this Proton turns it on by itself");
    case Fsr4Support::OnRequest:
        return QStringLiteral("this Proton turns it on when asked");
    case Fsr4Support::None:
        return QStringLiteral("not possible with this Proton");
    case Fsr4Support::Unknown:
        break;
    }
    return QStringLiteral("unknown (Steam's default Proton)");
}

} // namespace

// In English on purpose, like log messages: it goes into bug reports.
int listGames(QTextStream &out)
{
    const SteamScan scan = scanSteam(ScanOptions::fromEnvironment());
    if (!scan.steamFound) {
        out << "Steam was not found in this home folder.\n";
        return 1;
    }

    out << "Steam: " << scan.steamRoot << '\n';
    out << "Signed-in account: " << (scan.accountId != 0 ? "found" : "not found (launch options unavailable)") << '\n';
    for (const LibraryFolder &library : scan.libraries) {
        out << "Library: " << library.path << (library.available ? "" : "  (not available)") << '\n';
    }
    // Frame generation: lsfg-vk and Lossless Scaling, both installed by the user.
    out << "lsfg-vk: " << (framegen::layerInstalled(framegen::implicitLayerDirs()) ? "installed" : "not installed") << '\n';
    const LosslessScaling &lossless = scan.losslessScaling;
    if (!lossless.installed) {
        out << "Lossless Scaling: not installed\n";
    } else {
        out << "Lossless Scaling: branch " << (lossless.branch.isEmpty() ? QStringLiteral("(default)") : lossless.branch) << ", "
            << (lossless.dll.isEmpty() ? QStringLiteral("no frame generation file") : lossless.dll) << '\n';
    }

    const QSet<quint32> running = runningSteamApps();
    const settings::SettingsStore store(settings::SettingsStore::defaultBaseDir());
    out << '\n' << scan.games.size() << (scan.games.size() == 1 ? " game\n" : " games\n");
    for (const Game &game : scan.games) {
        out << '\n' << game.appId << "  " << game.name << (running.contains(game.appId) ? "  [running]" : "") << '\n';
        QString runsAs = runtimeName(game.runtime);
        if (game.runtime == Runtime::Proton) {
            runsAs += game.tool.name.isEmpty() ? QStringLiteral(" (Steam's default)") : QStringLiteral(" (%1)").arg(game.tool.displayName);
        }
        out << "    runs as:   " << runsAs << '\n';
        const QString apis = apiNames(game.apis);
        if (game.runtime == Runtime::Native && store.loadFacts(game.appId).openGlSeen) {
            out << "    graphics:  OpenGL (seen when it last ran)\n";
        } else {
            out << "    graphics:  " << (apis.isEmpty() ? QStringLiteral("not detected") : apis + QStringLiteral(" (from its files)"))
                << '\n';
        }
        QStringList fsr;
        if (game.fsr31Dx12) {
            fsr << QStringLiteral("DirectX 12");
        }
        if (game.fsr31Vulkan) {
            fsr << QStringLiteral("Vulkan");
        }
        out << "    FSR 3.1:   " << (fsr.isEmpty() ? QStringLiteral("no") : fsr.join(QStringLiteral(", "))) << '\n';
        if (game.runtime == Runtime::Proton) {
            out << "    FSR 4:     " << fsr4Words(game.tool.fsr4) << '\n';
        }
        if (!game.antiCheat.isEmpty()) {
            out << "    anti-cheat: " << game.antiCheat.join(QStringLiteral(", ")) << '\n';
        }
        out << "    cover:     " << (game.coverPath.isEmpty() ? "none in Steam's cache" : "found") << '\n';
        if (!game.launchOptions.isEmpty()) {
            out << "    launch options: " << game.launchOptions << '\n';
        }
    }

    if (!scan.skipped.isEmpty()) {
        out << "\nInstalled but not listed (" << scan.skipped.size() << "):\n";
        for (const SkippedApp &app : scan.skipped) {
            out << "    " << app.appId << "  " << (app.name.isEmpty() ? QStringLiteral("(no name)") : app.name) << ": " << app.reason
                << '\n';
        }
    }

    if (!scan.warnings.isEmpty()) {
        out << "\nWarnings:\n";
        for (const QString &warning : scan.warnings) {
            out << "    " << warning << '\n';
        }
    }
    return 0;
}

int removeLaunchLines(QTextStream &out)
{
    ScanOptions options = ScanOptions::fromEnvironment();
    options.inspectGameFiles = false;
    const SteamScan scan = scanSteam(options);
    if (!scan.steamFound || scan.localConfigPath.isEmpty()) {
        out << i18n("No Steam account found; nothing to remove.") << '\n';
        return 0;
    }
    if (isSteamRunning()) {
        out << i18n("Steam is running. Close Steam, then run this again: Steam rewrites its settings when it exits.") << '\n';
        return 1;
    }
    QList<launch::LineChange> done;
    QString error;
    if (!launch::syncLaunchLines(scan.localConfigPath, {}, &done, &error)) {
        out << i18n("Could not update Steam's launch options: %1", error) << '\n';
        return 1;
    }
    out << i18np("Removed gfxtune-run from 1 game.", "Removed gfxtune-run from %1 games.", done.size()) << '\n';
    return 0;
}

} // namespace gfxtune
