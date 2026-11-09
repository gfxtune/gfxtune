// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include <QFlags>
#include <QString>
#include <QStringList>

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

// What a Proton build does with games that ship FSR 3.1.
enum class Fsr4Support {
    Unknown,   // the tool's files could not be read
    BuiltIn,   // upgrades to FSR 4 by itself (Valve's Proton 11, proton-cachyos)
    OnRequest, // upgrades when PROTON_FSR4_UPGRADE asks for it (GE-Proton)
    None,      // cannot upgrade
};

struct CompatTool {
    QString name;        // Steam's internal name, e.g. "proton_experimental"
    QString displayName; // what Steam shows, e.g. "Proton Experimental"
    QString installPath; // empty when the tool is not installed or unknown
    Fsr4Support fsr4 = Fsr4Support::Unknown;
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
    // Anti-cheat the game ships or Steam lists (Valve Anti-Cheat), by name.
    QStringList antiCheat;
    // gfxtune's own record, not Steam's: the game ran for a minute without
    // showing a Vulkan picture (see settings::GameFacts::openGlSeen).
    bool openGlSeen = false;

    // The launch options currently set in Steam for this game.
    QString launchOptions;

    // Steam's build number of the installed version; changes with updates.
    QString buildId;
    // Portrait cover from Steam's own image cache; empty when Steam has none.
    QString coverPath;
};

QString runtimeName(Runtime runtime);

// A game whose files point to OpenGL and nothing else.
inline bool drawsOnlyWithOpenGl(GraphicsApis apis)
{
    return apis == GraphicsApis(GraphicsApi::OpenGL);
}

// Reads a Proton build's own files: AMD's FSR 4 library in its contrib
// folder (copied into every prefix), or its launch script switching FSR 4
// on by itself or reading PROTON_FSR4_UPGRADE.
Fsr4Support readFsr4Support(const QString &toolPath);
QString apiNames(GraphicsApis apis);

} // namespace gfxtune::steam
