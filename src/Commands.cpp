// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "Commands.h"

#include "launch/LaunchLines.h"
#include "steam/SteamProcess.h"

#include "steam/RunningGames.h"
#include "steam/SteamLibrary.h"

namespace gfxtune
{

using namespace steam;

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

    const QSet<quint32> running = runningSteamApps();
    out << '\n' << scan.games.size() << (scan.games.size() == 1 ? " game\n" : " games\n");
    for (const Game &game : scan.games) {
        out << '\n' << game.appId << "  " << game.name << (running.contains(game.appId) ? "  [running]" : "") << '\n';
        QString runsAs = runtimeName(game.runtime);
        if (game.runtime == Runtime::Proton) {
            runsAs += game.tool.name.isEmpty() ? QStringLiteral(" (Steam's default)")
                                               : QStringLiteral(" (%1)").arg(game.tool.displayName);
        }
        out << "    runs as:   " << runsAs << '\n';
        const QString apis = apiNames(game.apis);
        out << "    graphics:  " << (apis.isEmpty() ? QStringLiteral("not detected") : apis + QStringLiteral(" (from its files)")) << '\n';
        QStringList fsr;
        if (game.fsr31Dx12) {
            fsr << QStringLiteral("DirectX 12");
        }
        if (game.fsr31Vulkan) {
            fsr << QStringLiteral("Vulkan");
        }
        out << "    FSR 3.1:   " << (fsr.isEmpty() ? QStringLiteral("no") : fsr.join(QStringLiteral(", "))) << '\n';
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
        out << "No Steam account found; nothing to remove.\n";
        return 0;
    }
    if (isSteamRunning()) {
        out << "Steam is running. Close Steam, then run this again: Steam rewrites its settings when it exits.\n";
        return 1;
    }
    QList<launch::LineChange> done;
    QString error;
    if (!launch::syncLaunchLines(scan.localConfigPath, {}, &done, &error)) {
        out << "Could not update Steam's launch options: " << error << '\n';
        return 1;
    }
    out << "Removed gfxtune-run from " << done.size() << (done.size() == 1 ? " game.\n" : " games.\n");
    return 0;
}

} // namespace gfxtune
