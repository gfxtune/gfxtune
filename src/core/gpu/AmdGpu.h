// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace gfxtune::gpu
{

// An AMD card as the amdgpu driver presents it under /sys/class/drm.
struct AmdGpu {
    QString devicePath; // /sys/class/drm/cardN/device
    QString hwmonPath;  // its hwmon folder; empty if there is none
    QString name;       // marketing name, e.g. "AMD Radeon RX 7900 XT"
    quint16 deviceId = 0;
    quint8 revision = 0;
    quint64 vramBytes = 0;
};

struct GpuScan {
    QList<AmdGpu> amd; // strongest first: most video memory
    int otherCards = 0; // cards from other vendors, which gfxtune does not read
};

// Reads the cards from `sysRoot` (normally "/sys"). Card names come from
// libdrm's amdgpu.ids table; without it a card is named by its ID.
GpuScan findGpus(const QString &sysRoot = QStringLiteral("/sys"),
                 const QStringList &nameTables = {QStringLiteral("/usr/share/libdrm/amdgpu.ids")});

QString marketingName(quint16 deviceId, quint8 revision, const QStringList &nameTables);

// One reading of a card's sensors. A value the card does not report stays
// empty. Units: °C, watts, percent, bytes, MHz, RPM, millivolts.
struct GpuReading {
    // The card is in runtime sleep; reading its sensors would wake it, so
    // nothing else was read.
    bool asleep = false;

    std::optional<double> edgeTemp;
    std::optional<double> hotspotTemp;
    std::optional<double> memoryTemp;
    std::optional<double> power;
    std::optional<double> powerLimit;
    std::optional<int> load;
    std::optional<int> memoryLoad;
    std::optional<quint64> vramUsed;
    std::optional<quint64> vramTotal;
    std::optional<int> coreClock;
    std::optional<int> memoryClock;
    std::optional<int> fanRpm;
    std::optional<int> fanPercent;
    std::optional<int> voltage;
};

GpuReading readGpu(const AmdGpu &gpu);

} // namespace gfxtune::gpu
