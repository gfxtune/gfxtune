// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "RunningGames.h"

#include <QDir>
#include <QFile>

namespace gfxtune::steam
{

namespace
{

QByteArray readSmallFile(const QString &path, qint64 limit)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {}; // the process may have ended meanwhile
    }
    return file.read(limit);
}

} // namespace

QSet<quint32> runningSteamApps(const QString &procRoot)
{
    QSet<quint32> apps;
    const QDir proc(procRoot);
    const QStringList entries = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        bool numeric = false;
        entry.toUInt(&numeric);
        if (!numeric) {
            continue;
        }
        const QString base = procRoot + QLatin1Char('/') + entry;
        if (readSmallFile(base + QStringLiteral("/comm"), 64).trimmed() != "reaper") {
            continue;
        }
        const QList<QByteArray> args = readSmallFile(base + QStringLiteral("/cmdline"), 64 * 1024).split('\0');
        if (!args.contains(QByteArrayLiteral("SteamLaunch"))) {
            continue;
        }
        for (const QByteArray &arg : args) {
            if (arg == "--") {
                break; // what follows is the game's own command line
            }
            if (arg.startsWith("AppId=")) {
                bool ok = false;
                const quint32 appId = arg.mid(6).toUInt(&ok);
                if (ok && appId != 0) {
                    apps.insert(appId);
                }
                break;
            }
        }
    }
    return apps;
}

} // namespace gfxtune::steam
