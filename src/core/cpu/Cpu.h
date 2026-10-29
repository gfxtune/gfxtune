// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include <QString>

#include <optional>

namespace gfxtune::cpu
{

// The processor's time counters from /proc/stat, for working out its load
// between two readings.
struct CpuTimes {
    quint64 busy = 0;
    quint64 total = 0;
};

std::optional<CpuTimes> readCpuTimes(const QString &procRoot = QStringLiteral("/proc"));
// Percent of the time between two readings spent working, all cores.
std::optional<int> cpuLoad(const CpuTimes &before, const CpuTimes &after);

// The package temperature, from the k10temp (AMD), zenpower or coretemp
// (Intel) driver: Tctl, Tdie or "Package id 0".
std::optional<double> cpuTemperature(const QString &sysRoot = QStringLiteral("/sys"));

// The average clock of all cores right now, in MHz.
std::optional<int> cpuClock(const QString &sysRoot = QStringLiteral("/sys"));

} // namespace gfxtune::cpu
