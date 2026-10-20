// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include <QSet>
#include <QString>

namespace gfxtune::steam
{

// App ids of the Steam games running now. Steam starts every game through
// its "reaper" helper with "SteamLaunch AppId=<id>" on the command line;
// only processes named reaper are looked at closely.
QSet<quint32> runningSteamApps(const QString &procRoot = QStringLiteral("/proc"));

} // namespace gfxtune::steam
