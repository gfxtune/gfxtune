// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include "Game.h"
#include "GameFiles.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace gfxtune::steam
{

struct ScanOptions {
    // Home folder to look for Steam in.
    QString homePath;
    // Folders where system packages install extra compatibility tools, such
    // as CachyOS's proton-cachyos. Steam's own compatibilitytools.d is
    // always added after these and wins on name clashes.
    QStringList compatToolDirs;
    // Read each game's folder for graphics libraries and FSR 3.1.
    bool inspectGameFiles = true;
    // Replaces that reading, for example with a cached version. Called on
    // the scanning thread. Empty means inspectGameFiles() is used.
    std::function<GameFileReport(const Game &game, const QStringList &executables, bool windowsGame)> inspector;

    // Defaults for this computer: $HOME, the standard system folders and
    // STEAM_EXTRA_COMPAT_TOOLS_PATHS.
    static ScanOptions fromEnvironment();
};

struct LibraryFolder {
    QString path;
    bool available = true; // false when, for example, its drive is not mounted
};

// An installed app left out of the game list, and why.
struct SkippedApp {
    quint32 appId = 0;
    QString name;
    QString reason;
};

struct SteamScan {
    bool steamFound = false;
    QString steamRoot;
    quint32 accountId = 0; // the account last signed in; 0 if unknown
    // That account's localconfig.vdf, which holds launch options; empty when
    // no account is known. It may not exist yet for a new account.
    QString localConfigPath;
    QList<LibraryFolder> libraries;
    QList<Game> games; // installed games, sorted by name
    QList<SkippedApp> skipped;
    // Problems that did not stop the scan, such as an unreadable file.
    QStringList warnings;
    // Files and folders whose changes mean the scan is out of date.
    QStringList watchPaths;
};

// Reads Steam's files where they are. Nothing is written or copied.
SteamScan scanSteam(const ScanOptions &options);

} // namespace gfxtune::steam
