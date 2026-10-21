// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "Covers.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace gfxtune::steam
{

namespace
{

bool isFile(const QString &path)
{
    return QFileInfo(path).isFile();
}

} // namespace

QString findCover(const QString &steamRoot, quint32 accountId, quint32 appId)
{
    const QString id = QString::number(appId);

    if (accountId != 0) {
        const QString grid = steamRoot + QStringLiteral("/userdata/%1/config/grid/").arg(accountId) + id + QLatin1Char('p');
        for (const char *extension : {".png", ".jpg", ".jpeg", ".webp"}) {
            if (isFile(grid + QLatin1String(extension))) {
                return grid + QLatin1String(extension);
            }
        }
    }

    const QString cache = steamRoot + QStringLiteral("/appcache/librarycache/");
    const QString direct = cache + id + QStringLiteral("/library_600x900.jpg");
    if (isFile(direct)) {
        return direct;
    }

    // One level deeper, under a 40-character content hash. Two candidates
    // would be a guess, so only a single one is used.
    static const QRegularExpression hash(QStringLiteral("^[0-9a-f]{40}$"));
    QString nested;
    const QStringList folders = QDir(cache + id).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &folder : folders) {
        if (!hash.match(folder).hasMatch()) {
            continue;
        }
        const QString candidate = cache + id + QLatin1Char('/') + folder + QStringLiteral("/library_600x900.jpg");
        if (isFile(candidate)) {
            if (!nested.isEmpty()) {
                nested.clear();
                break;
            }
            nested = candidate;
        }
    }
    if (!nested.isEmpty()) {
        return nested;
    }

    const QString flat = cache + id + QStringLiteral("_library_600x900.jpg");
    return isFile(flat) ? flat : QString();
}

} // namespace gfxtune::steam
