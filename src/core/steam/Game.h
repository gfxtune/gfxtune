// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include <QFlags>
#include <QString>

namespace gfxtune::steam
{

enum class GraphicsApi : quint16 {
    DirectX8 = 0x01,
    DirectX9 = 0x02,
    DirectX10 = 0x04,
    DirectX11 = 0x08,
    DirectX12 = 0x10,
    Vulkan = 0x20,
    OpenGL = 0x40,
};
Q_DECLARE_FLAGS(GraphicsApis, GraphicsApi)
Q_DECLARE_OPERATORS_FOR_FLAGS(GraphicsApis)

enum class Runtime {
    Unknown,
    Native, // the game's own Linux version
    Proton, // the Windows version through a compatibility tool
};

struct CompatTool {
    QString name;        // Steam's internal name, e.g. "proton_experimental"
    QString displayName; // what Steam shows, e.g. "Proton Experimental"
    QString installPath; // empty when the tool is not installed or unknown
};

struct Game {
    quint32 appId = 0;
    QString name;
    QString libraryPath;
    QString installPath;

    Runtime runtime = Runtime::Unknown;
    // For Proton games: the tool Steam will use. An empty name means Steam's
    // default, which Steam does not write down anywhere we can read.
    CompatTool tool;

    // Read from the game's programs; a guess, since games can also load these
    // libraries in ways a file scan cannot see.
    GraphicsApis apis;
    // FSR 3.1 ships as separate libraries, which is what makes upgrading it
    // possible.
    bool fsr31Dx12 = false;
    bool fsr31Vulkan = false;

    // The launch options currently set in Steam for this game.
    QString launchOptions;

    // Steam's build number of the installed version; changes with updates.
    QString buildId;
    // Portrait cover from Steam's own image cache; empty when Steam has none.
    QString coverPath;
};

QString runtimeName(Runtime runtime);
QString apiNames(GraphicsApis apis);

} // namespace gfxtune::steam
