// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "RunningGames.h"

#include "shared/Sizes.h"

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
    const QHash<quint32, qint64> reapers = steamGameReapers(procRoot);
    for (auto it = reapers.cbegin(); it != reapers.cend(); ++it) {
        apps.insert(it.key());
    }
    return apps;
}

QHash<quint32, qint64> steamGameReapers(const QString &procRoot)
{
    QHash<quint32, qint64> apps;
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
        const QList<QByteArray> args = readSmallFile(base + QStringLiteral("/cmdline"), 64 * KiB).split('\0');
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
                    apps.insert(appId, entry.toLongLong());
                }
                break;
            }
        }
    }
    return apps;
}

QList<qint64> processTree(qint64 root, const QString &procRoot)
{
    // Parent of every process, from /proc/<pid>/stat: "pid (name) state ppid ...".
    // The name may hold spaces and brackets, so read after the last ')'.
    QHash<qint64, QList<qint64>> children;
    const QStringList entries = QDir(procRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        bool numeric = false;
        const qint64 pid = entry.toLongLong(&numeric);
        if (!numeric) {
            continue;
        }
        const QByteArray stat = readSmallFile(procRoot + QLatin1Char('/') + entry + QStringLiteral("/stat"), 4096);
        const qsizetype close = stat.lastIndexOf(')');
        if (close < 0) {
            continue;
        }
        const QList<QByteArray> fields = stat.mid(close + 2).split(' ');
        bool ok = false;
        const qint64 parent = fields.size() > 1 ? fields.at(1).toLongLong(&ok) : 0;
        if (ok) {
            children[parent].append(pid);
        }
    }
    QList<qint64> tree{root};
    for (qsizetype i = 0; i < tree.size(); ++i) {
        for (const qint64 child : children.value(tree.at(i))) {
            if (!tree.contains(child)) {
                tree.append(child);
            }
        }
    }
    return tree;
}

} // namespace gfxtune::steam
