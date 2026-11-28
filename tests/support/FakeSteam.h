// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

// A made-up Steam installation for tests, with games covering the cases the
// scanner has to tell apart. Tests never read a real Steam installation.

#include "steam/SteamLibrary.h"
#include "vdf/KeyValues.h"

#include <QString>
#include <QTemporaryDir>

#include <vector>

namespace gfxtune::test
{

// 64-bit Steam IDs for two made-up accounts; userdata/ uses the low 32 bits.
constexpr quint64 OlderAccount = 76561198000000001ull;
constexpr quint64 RecentAccount = 76561198000000002ull;
constexpr quint32 RecentAccountFolder = quint32(RecentAccount & 0xFFFFFFFFu);

// Where game 1002's cover is, relative to its folder in Steam's library cache.
inline const QString NeonDriftCover = QStringLiteral("a1b2c3d4e5f60718293a4b5c6d7e8f9012345678/library_capsule.jpg");

vdf::Node file(std::vector<vdf::Node> children);

// A made-up Steam installation:
//   home/.local/share/Steam            the Steam folder, also reached through ~/.steam/steam
//   lib2/                              a second library
//   /does/not/exist                    a library on a drive that is not mounted
class FakeSteam
{
public:
    QTemporaryDir dir;
    QString home;
    QString root;
    QString lib2;
    QString systemTools;

    FakeSteam();

    steam::ScanOptions options() const;
    void text(const QString &path, const vdf::Node &contents);
    void game(const QString &library, quint32 appId, const QString &name, const QString &installDir,
              const QString &stateFlags = QStringLiteral("4"));
    // Lossless Scaling in `library`, on a Steam branch (empty for the default
    // one) and with its frame generation file under that name, if any.
    void losslessScaling(const QString &library, const QString &branch, const QString &dllName);
    void build();
};

} // namespace gfxtune::test
