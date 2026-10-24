// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "Covers.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace gfxtune::steam
{

namespace
{

// A folder named after a content hash, as Steam names them.
const QRegularExpression &hashFolder()
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{40}$"));
    return pattern;
}

// Portrait cover names in either generation, optionally with a language and
// the double size: library_600x900.jpg, library_capsule_japanese_2x.jpg.
const QRegularExpression &coverName()
{
    static const QRegularExpression pattern(QStringLiteral("^library_(?:600x900|capsule)(?:_[a-z]+)?(?:_2x)?\\.(?:jpg|png)$"));
    return pattern;
}

// A name listed in Steam's data: a plain file name, optionally under one
// hash folder. Anything else could point outside the game's cache folder.
const QRegularExpression &safeListedName()
{
    static const QRegularExpression pattern(QStringLiteral("^(?:[0-9a-f]{40}/)?[A-Za-z0-9_][A-Za-z0-9_.-]*\\.(?:jpg|jpeg|png)$"));
    return pattern;
}

// The most recently written of the files that exist; earlier entries win a
// tie, so the result does not depend on chance.
QString newestFile(const QStringList &paths)
{
    QString best;
    QDateTime bestTime;
    for (const QString &path : paths) {
        const QFileInfo info(path);
        if (!info.isFile()) {
            continue;
        }
        const QDateTime time = info.lastModified();
        if (best.isEmpty() || time > bestTime) {
            best = path;
            bestTime = time;
        }
    }
    return best;
}

void addCoverFiles(const QString &folder, QStringList &out)
{
    const QStringList files = QDir(folder).entryList(QDir::Files, QDir::Name);
    for (const QString &file : files) {
        if (coverName().match(file).hasMatch()) {
            out.append(folder + file);
        }
    }
}

} // namespace

QStringList listedCovers(const vdf::Node *appInfo)
{
    QStringList names;
    const vdf::Node *capsule = appInfo ? appInfo->find({u"common", u"library_assets_full", u"library_capsule"}) : nullptr;
    if (!capsule) {
        return names;
    }
    for (const QStringView size : {u"image", u"image2x"}) {
        const vdf::Node *languages = capsule->child(size);
        if (!languages) {
            continue;
        }
        for (const vdf::Node &language : languages->children) {
            if (!language.isSection && safeListedName().match(language.value).hasMatch() && !names.contains(language.value)) {
                names.append(language.value);
            }
        }
    }
    return names;
}

QString findCover(const QString &steamRoot, quint32 accountId, quint32 appId, const QStringList &listed)
{
    const QString id = QString::number(appId);

    if (accountId != 0) {
        const QString grid = steamRoot + QStringLiteral("/userdata/%1/config/grid/").arg(accountId) + id + QLatin1Char('p');
        for (const char *extension : {".png", ".jpg", ".jpeg", ".webp"}) {
            if (QFileInfo(grid + QLatin1String(extension)).isFile()) {
                return grid + QLatin1String(extension);
            }
        }
    }

    const QString cache = steamRoot + QStringLiteral("/appcache/librarycache/");
    const QString folder = cache + id + QLatin1Char('/');

    QStringList named;
    for (const QString &name : listed) {
        if (safeListedName().match(name).hasMatch()) {
            named.append(folder + name);
        }
    }
    QString found = newestFile(named);
    if (!found.isEmpty()) {
        return found;
    }

    // Steam's data may be missing or name a file Steam has not saved yet:
    // look for cover-named files in the folder and its hash folders.
    QStringList candidates;
    addCoverFiles(folder, candidates);
    const QStringList folders = QDir(folder).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &sub : folders) {
        if (hashFolder().match(sub).hasMatch()) {
            addCoverFiles(folder + sub + QLatin1Char('/'), candidates);
        }
    }
    found = newestFile(candidates);
    if (!found.isEmpty()) {
        return found;
    }

    const QString flat = cache + id + QStringLiteral("_library_600x900.jpg");
    return QFileInfo(flat).isFile() ? flat : QString();
}

} // namespace gfxtune::steam
