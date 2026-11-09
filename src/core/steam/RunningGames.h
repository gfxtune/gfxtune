// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

#include <optional>

namespace gfxtune::steam
{

// App ids of the Steam games running now. Steam starts every game through
// its "reaper" helper with "SteamLaunch AppId=<id>" on the command line;
// only processes named reaper are looked at closely.
QSet<quint32> runningSteamApps(const QString &procRoot = QStringLiteral("/proc"));

// The same, with the process id of each game's reaper.
QHash<quint32, qint64> steamGameReapers(const QString &procRoot = QStringLiteral("/proc"));

// When a process started, from the kernel's count of clock ticks since boot.
std::optional<QDateTime> processStartTime(qint64 pid, const QString &procRoot = QStringLiteral("/proc"));

// A process and everything it started, and what they started in turn.
QList<qint64> processTree(qint64 root, const QString &procRoot = QStringLiteral("/proc"));

} // namespace gfxtune::steam
