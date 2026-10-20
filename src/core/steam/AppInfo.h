// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include "vdf/KeyValues.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

namespace gfxtune::steam
{

// Steam's appcache/appinfo.vdf: a cache of every app's store data (type,
// supported systems, launch entries, Steam Play mappings). It is not a
// documented format; versions 28 and 29 are understood.
struct AppInfoFile {
    int version = 0;
    // The "appinfo" section of each requested app that was found.
    QHash<quint32, vdf::Node> apps;
    // Entries that could not be read; the rest of the file still is.
    QStringList warnings;
};

// Reads only the requested apps and skips everything else by its recorded
// size, so large caches stay cheap to read.
bool readAppInfo(const QString &path, const QSet<quint32> &wanted, AppInfoFile &out, QString *error = nullptr);

} // namespace gfxtune::steam
