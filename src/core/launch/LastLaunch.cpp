// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "LastLaunch.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace gfxtune::launch
{

QString lastLaunchPath(const QString &baseDir, quint32 appId)
{
    return baseDir + QStringLiteral("/last-launch/%1.ini").arg(appId);
}

bool writeLastLaunch(const QString &baseDir, quint32 appId, const LaunchPlan &plan, const QDateTime &time)
{
    const QString path = lastLaunchPath(baseDir, appId);
    if (!QDir().mkpath(QFileInfo(path).path())) {
        return false;
    }
    QSettings out(path, QSettings::IniFormat);
    out.clear();
    out.setValue(QStringLiteral("launch/time"), time.toUTC().toString(Qt::ISODate));
    out.setValue(QStringLiteral("launch/applied"), plan.applied);
    out.setValue(QStringLiteral("launch/kept_from_steam"), plan.keptFromSteam);
    out.setValue(QStringLiteral("launch/not_applied"), plan.notApplied);
    out.sync();
    return out.status() == QSettings::NoError;
}

std::optional<LastLaunch> readLastLaunch(const QString &baseDir, quint32 appId)
{
    const QString path = lastLaunchPath(baseDir, appId);
    if (!QFileInfo::exists(path)) {
        return std::nullopt;
    }
    QSettings in(path, QSettings::IniFormat);
    LastLaunch last;
    last.time = QDateTime::fromString(in.value(QStringLiteral("launch/time")).toString(), Qt::ISODate);
    if (!last.time.isValid()) {
        return std::nullopt;
    }
    last.applied = in.value(QStringLiteral("launch/applied")).toStringList();
    last.keptFromSteam = in.value(QStringLiteral("launch/kept_from_steam")).toStringList();
    last.notApplied = in.value(QStringLiteral("launch/not_applied")).toStringList();
    return last;
}

} // namespace gfxtune::launch
