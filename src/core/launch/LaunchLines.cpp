// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "LaunchLines.h"

#include "LaunchLine.h"
#include "vdf/TextVdf.h"
#include "vdf/TextVdfEdit.h"

#include <QFileInfo>

#include <algorithm>

namespace gfxtune::launch
{

namespace
{

const QStringList &appsPath()
{
    static const QStringList path{
        QStringLiteral("UserLocalConfigStore"),
        QStringLiteral("Software"),
        QStringLiteral("Valve"),
        QStringLiteral("Steam"),
        QStringLiteral("apps"),
    };
    return path;
}

} // namespace

QList<LineChange> planLineChanges(const QHash<quint32, QString> &launchOptions, const QSet<quint32> &wanted)
{
    QList<LineChange> changes;
    for (quint32 appId : wanted) {
        const QString before = launchOptions.value(appId);
        const QString after = withHelper(before);
        if (after != before) {
            changes.append({appId, before, after});
        }
    }
    for (auto it = launchOptions.cbegin(); it != launchOptions.cend(); ++it) {
        if (!wanted.contains(it.key()) && hasHelper(it.value())) {
            changes.append({it.key(), it.value(), withoutHelper(it.value())});
        }
    }
    std::sort(changes.begin(), changes.end(), [](const LineChange &a, const LineChange &b) { return a.appId < b.appId; });
    return changes;
}

bool readLaunchOptions(const QString &localConfigPath, QHash<quint32, QString> &out, QString *error)
{
    out.clear();
    if (!QFileInfo::exists(localConfigPath)) {
        return true; // a new account: nothing set yet
    }
    vdf::Node file;
    if (!vdf::readTextFile(localConfigPath, file, error)) {
        return false;
    }
    const vdf::Node *apps = file.find({u"UserLocalConfigStore", u"Software", u"Valve", u"Steam", u"apps"});
    if (!apps) {
        return true;
    }
    for (const vdf::Node &app : apps->children) {
        bool ok = false;
        const quint32 appId = app.key.toUInt(&ok);
        if (ok && app.isSection && app.child(u"LaunchOptions")) {
            out.insert(appId, app.valueOf(u"LaunchOptions"));
        }
    }
    return true;
}

bool syncLaunchLines(const QString &localConfigPath, const QSet<quint32> &wanted, QList<LineChange> *done, QString *error)
{
    QHash<quint32, QString> current;
    if (!readLaunchOptions(localConfigPath, current, error)) {
        return false;
    }
    const QList<LineChange> changes = planLineChanges(current, wanted);
    if (done) {
        *done = changes;
    }
    if (changes.isEmpty()) {
        return true;
    }
    if (!QFileInfo::exists(localConfigPath)) {
        if (error) {
            *error = QStringLiteral("Steam has not created %1 yet").arg(localConfigPath);
        }
        return false;
    }
    QList<vdf::ValueEdit> edits;
    for (const LineChange &change : changes) {
        QStringList path = appsPath();
        path.append(QString::number(change.appId));
        edits.append({path, QStringLiteral("LaunchOptions"), change.after.isEmpty() ? std::nullopt : std::optional<QString>(change.after)});
    }
    return vdf::editTextFile(localConfigPath, edits, error);
}

} // namespace gfxtune::launch
