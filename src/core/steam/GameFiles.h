// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include "Game.h"

#include <QString>
#include <QStringList>

namespace gfxtune::steam
{

struct GameFileReport {
    GraphicsApis apis;
    bool fsr31Dx12 = false;
    bool fsr31Vulkan = false;
    // Anti-cheat systems whose files ship with the game, by name.
    QStringList antiCheat;
};

// Looks through an installed game's folder without changing anything.
// `executables` are the launch entries Steam knows for the system the game
// runs as, relative to the install folder (Windows-style paths are fine;
// letter case may differ from the files on disk).
GameFileReport inspectGameFiles(const QString &installPath, const QStringList &executables, bool windowsGame);

// The anti-cheat a file or folder name (lower case) belongs to, or an empty
// string. Specific vendors first; then any folder named after anti-cheat and
// any Windows kernel driver, since games ship drivers almost only for it.
QString antiCheatOfFile(const QString &fileName);
QString antiCheatOfFolder(const QString &folderName);

// Which graphics systems a list of imported library names points to.
GraphicsApis apisFromLibraries(const QStringList &libraries);

} // namespace gfxtune::steam
