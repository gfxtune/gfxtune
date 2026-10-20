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
};

// Looks through an installed game's folder without changing anything.
// `executables` are the launch entries Steam knows for the system the game
// runs as, relative to the install folder (Windows-style paths are fine;
// letter case may differ from the files on disk).
GameFileReport inspectGameFiles(const QString &installPath, const QStringList &executables, bool windowsGame);

// Which graphics systems a list of imported library names points to.
GraphicsApis apisFromLibraries(const QStringList &libraries);

} // namespace gfxtune::steam
