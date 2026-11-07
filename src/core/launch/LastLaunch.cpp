// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "LastLaunch.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>

namespace gfxtune::launch
{

QString lastLaunchPath(const QString &baseDir, quint32 appId)
{
    return baseDir + QStringLiteral("/last-launch/%1.ini").arg(appId);
}

QString filterReportPath(const QString &baseDir, quint32 appId)
{
    return baseDir + QStringLiteral("/last-launch/%1.filters").arg(appId);
}

std::optional<FilterReport> readFilterReport(const QString &baseDir, quint32 appId)
{
    QFile file(filterReportPath(baseDir, appId));
    // A few short lines; anything bigger was not written by the layer.
    if (file.size() > 4096 || !file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    FilterReport report;
    QString state;
    for (const QByteArray &raw : file.readAll().split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        const qsizetype equals = line.indexOf(QLatin1Char('='));
        if (line.startsWith(QLatin1Char('#')) || equals <= 0) {
            continue;
        }
        const QString key = line.left(equals);
        const QString value = line.mid(equals + 1);
        if (key == QLatin1String("time")) {
            bool ok = false;
            const qint64 seconds = value.toLongLong(&ok);
            if (ok && seconds > 0) {
                report.time = QDateTime::fromSecsSinceEpoch(seconds);
            }
        } else if (key == QLatin1String("state")) {
            state = value;
        } else if (key == QLatin1String("detail")) {
            report.detail = value;
        }
    }
    if (!report.time.isValid() || (state != QLatin1String("running") && state != QLatin1String("skipped"))) {
        return std::nullopt;
    }
    report.running = state == QLatin1String("running");
    return report;
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
    const QSettings in(path, QSettings::IniFormat);
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
