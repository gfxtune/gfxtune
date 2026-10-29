// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "Cpu.h"

#include <QDir>
#include <QFile>

#include <algorithm>
#include <cmath>

namespace gfxtune::cpu
{

namespace
{

QByteArray readSmall(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.read(4096).trimmed() : QByteArray();
}

} // namespace

std::optional<CpuTimes> readCpuTimes(const QString &procRoot)
{
    QFile file(procRoot + QStringLiteral("/stat"));
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    // "cpu  user nice system idle iowait irq softirq steal guest guest_nice"
    const QList<QByteArray> fields = file.readLine().simplified().split(' ');
    if (fields.size() < 5 || fields.at(0) != "cpu") {
        return std::nullopt;
    }
    CpuTimes times;
    // Guest time is already part of user time.
    for (qsizetype i = 1; i < std::min<qsizetype>(fields.size(), 9); ++i) {
        bool ok = false;
        const quint64 value = fields.at(i).toULongLong(&ok);
        if (!ok) {
            return std::nullopt;
        }
        times.total += value;
        if (i != 4 && i != 5) { // idle and iowait
            times.busy += value;
        }
    }
    return times;
}

std::optional<int> cpuLoad(const CpuTimes &before, const CpuTimes &after)
{
    if (after.total <= before.total || after.busy < before.busy) {
        return std::nullopt;
    }
    const double share = double(after.busy - before.busy) / double(after.total - before.total);
    return int(std::lround(std::clamp(share, 0.0, 1.0) * 100.0));
}

std::optional<double> cpuTemperature(const QString &sysRoot)
{
    const QString hwmon = sysRoot + QStringLiteral("/class/hwmon/");
    const QStringList sensors = QDir(hwmon).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &sensor : sensors) {
        const QString dir = hwmon + sensor + QLatin1Char('/');
        const QByteArray name = readSmall(dir + QStringLiteral("name"));
        if (name != "k10temp" && name != "zenpower" && name != "coretemp") {
            continue;
        }
        // The labels say which input is the whole package.
        std::optional<double> first;
        for (int i = 1; i <= 16; ++i) {
            const QString base = dir + QStringLiteral("temp%1_").arg(i);
            bool ok = false;
            const double value = readSmall(base + QStringLiteral("input")).toDouble(&ok) / 1000.0;
            if (!ok) {
                continue;
            }
            const QByteArray label = readSmall(base + QStringLiteral("label"));
            if (label == "Tctl" || label == "Tdie" || label.startsWith("Package id")) {
                return value;
            }
            if (!first) {
                first = value;
            }
        }
        if (first) {
            return first;
        }
    }
    return std::nullopt;
}

std::optional<int> cpuClock(const QString &sysRoot)
{
    const QString cpus = sysRoot + QStringLiteral("/devices/system/cpu/");
    const QStringList entries = QDir(cpus).entryList({QStringLiteral("cpu*")}, QDir::Dirs);
    double sum = 0;
    int count = 0;
    for (const QString &entry : entries) {
        bool numbered = false;
        entry.mid(3).toInt(&numbered);
        if (!numbered) {
            continue; // cpufreq, cpuidle
        }
        bool ok = false;
        const double khz = readSmall(cpus + entry + QStringLiteral("/cpufreq/scaling_cur_freq")).toDouble(&ok);
        if (ok && khz > 0) {
            sum += khz;
            ++count;
        }
    }
    if (count == 0) {
        return std::nullopt;
    }
    return int(std::lround(sum / count / 1000.0));
}

} // namespace gfxtune::cpu
