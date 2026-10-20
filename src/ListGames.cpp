// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "ListGames.h"

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

    if (!scan.warnings.isEmpty()) {
        out << "\nWarnings:\n";
        for (const QString &warning : scan.warnings) {
            out << "    " << warning << '\n';
        }
    }
    return 0;
}

} // namespace gfxtune
