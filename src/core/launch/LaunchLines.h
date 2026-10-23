// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

namespace gfxtune::launch
{

struct LineChange {
    quint32 appId = 0;
    QString before;
    QString after;
};

// Games in `wanted` get gfxtune-run in their launch options; every other
// game that has it loses it. Nothing else changes.
QList<LineChange> planLineChanges(const QHash<quint32, QString> &launchOptions, const QSet<quint32> &wanted);

// Reads every game's launch options from Steam's localconfig.vdf.
bool readLaunchOptions(const QString &localConfigPath, QHash<quint32, QString> &out, QString *error = nullptr);

// Brings localconfig.vdf in line with `wanted`, reading it fresh first so a
// change Steam made since the last scan is never overwritten. Steam must be
// closed; see steam::isSteamRunning().
bool syncLaunchLines(const QString &localConfigPath, const QSet<quint32> &wanted, QList<LineChange> *done = nullptr,
                     QString *error = nullptr);

} // namespace gfxtune::launch
