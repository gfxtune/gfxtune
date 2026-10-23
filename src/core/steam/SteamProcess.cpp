// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "SteamProcess.h"

#include <QDir>
#include <QFile>

#include <optional>

#include <unistd.h>

namespace gfxtune::steam
{

namespace
{

QByteArray readSmall(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.read(4096) : QByteArray();
}

// The real user id from /proc/<pid>/status ("Uid:  real effective saved fs").
std::optional<uint> ownerOf(const QString &processDir)
{
    for (const QByteArray &line : readSmall(processDir + QStringLiteral("/status")).split('\n')) {
        if (line.startsWith("Uid:")) {
            bool ok = false;
            const uint uid = line.mid(4).simplified().split(' ').value(0).toUInt(&ok);
            if (ok) {
                return uid;
            }
        }
    }
    return std::nullopt;
}

} // namespace

bool isSteamRunning(const QString &procRoot)
{
    return isSteamRunning(procRoot, uint(getuid()));
}

bool isSteamRunning(const QString &procRoot, uint userId)
{
    const QStringList entries = QDir(procRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        bool numeric = false;
        entry.toUInt(&numeric);
        if (!numeric) {
            continue;
        }
        const QString dir = procRoot + QLatin1Char('/') + entry;
        if (readSmall(dir + QStringLiteral("/comm")).trimmed() != "steam") {
            continue;
        }
        if (ownerOf(dir) == userId) {
            return true;
        }
    }
    return false;
}

} // namespace gfxtune::steam
