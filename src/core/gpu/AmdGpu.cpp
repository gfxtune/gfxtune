// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "AmdGpu.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>

#include <algorithm>

namespace gfxtune::gpu
{

namespace
{

constexpr auto AmdVendor = QLatin1StringView("0x1002");

QByteArray readSmall(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.read(4096).trimmed() : QByteArray();
}

template<typename T>
std::optional<T> number(const QString &path)
{
    const QByteArray text = readSmall(path);
    bool ok = false;
    const qlonglong value = text.toLongLong(&ok);
    if (!ok || value < 0) {
        return std::nullopt;
    }
    return T(value);
}

std::optional<quint64> hex(const QString &path)
{
    QByteArray text = readSmall(path);
    if (text.startsWith("0x")) {
        text = text.mid(2);
    }
    bool ok = false;
    const quint64 value = text.toULongLong(&ok, 16);
    return ok ? std::optional<quint64>(value) : std::nullopt;
}

QString findHwmon(const QString &devicePath)
{
    const QDir dir(devicePath + QStringLiteral("/hwmon"));
    const QStringList entries = dir.entryList({QStringLiteral("hwmon*")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    return entries.isEmpty() ? QString() : dir.filePath(entries.first());
}

std::optional<double> scaled(const QString &path, double divisor)
{
    const auto raw = number<qint64>(path);
    return raw ? std::optional<double>(double(*raw) / divisor) : std::nullopt;
}

} // namespace

QString marketingName(quint16 deviceId, quint8 revision, const QStringList &nameTables)
{
    const QString device = QStringLiteral("%1").arg(deviceId, 4, 16, QLatin1Char('0')).toUpper();
    const QString rev = QStringLiteral("%1").arg(revision, 2, 16, QLatin1Char('0')).toUpper();
    for (const QString &table : nameTables) {
        QFile file(table);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        while (!file.atEnd()) {
            const QString line = QString::fromUtf8(file.readLine()).trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
                continue;
            }
            const QStringList parts = line.split(QLatin1Char(','));
            if (parts.size() >= 3 && parts[0].trimmed().toUpper() == device && parts[1].trimmed().toUpper() == rev) {
                return parts.mid(2).join(QLatin1Char(',')).trimmed();
            }
        }
    }
    return QStringLiteral("AMD graphics card %1:%2").arg(device, rev);
}

GpuScan findGpus(const QString &sysRoot, const QStringList &nameTables)
{
    GpuScan scan;
    static const QRegularExpression cardName(QStringLiteral("^card\\d+$"));
    const QDir drm(sysRoot + QStringLiteral("/class/drm"));
    for (const QString &entry : drm.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (!cardName.match(entry).hasMatch()) {
            continue; // connectors such as card0-DP-1, and render nodes
        }
        const QString device = drm.filePath(entry) + QStringLiteral("/device");
        const QByteArray vendor = readSmall(device + QStringLiteral("/vendor"));
        if (vendor.isEmpty()) {
            continue;
        }
        if (QLatin1StringView(vendor) != AmdVendor) {
            ++scan.otherCards;
            continue;
        }
        AmdGpu gpu;
        gpu.devicePath = device;
        gpu.hwmonPath = findHwmon(device);
        gpu.deviceId = quint16(hex(device + QStringLiteral("/device")).value_or(0));
        gpu.revision = quint8(hex(device + QStringLiteral("/revision")).value_or(0));
        gpu.vramBytes = number<quint64>(device + QStringLiteral("/mem_info_vram_total")).value_or(0);
        gpu.name = marketingName(gpu.deviceId, gpu.revision, nameTables);
        scan.amd.append(gpu);
    }
    // A discrete card has far more video memory than a processor's built-in
    // graphics, which only reserves a little.
    std::stable_sort(scan.amd.begin(), scan.amd.end(), [](const AmdGpu &a, const AmdGpu &b) { return a.vramBytes > b.vramBytes; });
    return scan;
}

GpuReading readGpu(const AmdGpu &gpu)
{
    GpuReading r;
    if (readSmall(gpu.devicePath + QStringLiteral("/power/runtime_status")) == "suspended") {
        r.asleep = true;
        return r;
    }

    const QString d = gpu.devicePath + QLatin1Char('/');
    r.load = number<int>(d + QStringLiteral("gpu_busy_percent"));
    r.memoryLoad = number<int>(d + QStringLiteral("mem_busy_percent"));
    r.vramUsed = number<quint64>(d + QStringLiteral("mem_info_vram_used"));
    r.vramTotal = number<quint64>(d + QStringLiteral("mem_info_vram_total"));

    if (gpu.hwmonPath.isEmpty()) {
        return r;
    }
    const QString h = gpu.hwmonPath + QLatin1Char('/');
    // Temperatures are told apart by their labels, not their numbers.
    for (int i = 1; i <= 8; ++i) {
        const QString base = h + QStringLiteral("temp%1_").arg(i);
        const QByteArray label = readSmall(base + QStringLiteral("label"));
        if (label.isEmpty()) {
            continue;
        }
        const auto value = scaled(base + QStringLiteral("input"), 1000.0);
        if (label == "edge") {
            r.edgeTemp = value;
        } else if (label == "junction") {
            r.hotspotTemp = value;
        } else if (label == "mem") {
            r.memoryTemp = value;
        }
    }
    // The averaged value reads steadier; newer kernels may only offer the
    // instantaneous one.
    r.power = scaled(h + QStringLiteral("power1_average"), 1e6);
    if (!r.power) {
        r.power = scaled(h + QStringLiteral("power1_input"), 1e6);
    }
    r.powerLimit = scaled(h + QStringLiteral("power1_cap"), 1e6);
    if (const auto hz = number<qint64>(h + QStringLiteral("freq1_input"))) {
        r.coreClock = int(*hz / 1'000'000);
    }
    if (const auto hz = number<qint64>(h + QStringLiteral("freq2_input"))) {
        r.memoryClock = int(*hz / 1'000'000);
    }
    r.fanRpm = number<int>(h + QStringLiteral("fan1_input"));
    if (const auto pwm = number<int>(h + QStringLiteral("pwm1")); pwm && *pwm <= 255) {
        r.fanPercent = (*pwm * 100 + 127) / 255;
    }
    r.voltage = number<int>(h + QStringLiteral("in0_input"));
    return r;
}

} // namespace gfxtune::gpu
