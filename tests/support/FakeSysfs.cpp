// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "FakeSysfs.h"

#include "Fixtures.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>

namespace gfxtune::test
{

namespace
{

QString devicePath(const QString &root, int index)
{
    return root + QStringLiteral("/class/drm/card%1/device").arg(index);
}

} // namespace

void addCard(const QString &root, int index, const FakeCard &card)
{
    const QString d = devicePath(root, index) + QLatin1Char('/');
    writeFile(d + QStringLiteral("vendor"), card.vendor.toLatin1() + '\n');
    writeFile(d + QStringLiteral("device"), card.device.toLatin1() + '\n');
    writeFile(d + QStringLiteral("revision"), card.revision.toLatin1() + '\n');
    writeFile(d + QStringLiteral("power/runtime_status"), card.runtimeStatus.toLatin1() + '\n');
    writeFile(d + QStringLiteral("gpu_busy_percent"), "3\n");
    writeFile(d + QStringLiteral("mem_busy_percent"), "1\n");
    writeFile(d + QStringLiteral("mem_info_vram_total"), QByteArray::number(card.vramTotal) + '\n');
    writeFile(d + QStringLiteral("mem_info_vram_used"), "1073741824\n");
    // A connector and the render node sit next to cards and are not cards.
    QDir().mkpath(root + QStringLiteral("/class/drm/card%1-DP-1").arg(index));
    QDir().mkpath(root + QStringLiteral("/class/drm/renderD%1").arg(128 + index));
    if (!card.hwmon) {
        return;
    }
    const QString h = d + QStringLiteral("hwmon/hwmon%1/").arg(index);
    const QList<std::pair<const char *, QByteArray>> files{
        {"name", "amdgpu"},
        {"temp1_label", "edge"},
        {"temp1_input", "41000"},
        {"temp2_label", "junction"},
        {"temp2_input", "46000"},
        {"temp3_label", "mem"},
        {"temp3_input", "52000"},
        {"power1_average", "21000000"},
        {"power1_cap", "315000000"},
        {"freq1_input", "28000000"},
        {"freq2_input", "96000000"},
        {"fan1_input", "0"},
        {"pwm1", "0"},
        {"in0_input", "632"},
    };
    for (const auto &[name, value] : files) {
        writeFile(h + QString::fromLatin1(name), value + '\n');
    }
}

void setCardFile(const QString &root, int index, const QString &relative, const QByteArray &value)
{
    writeFile(devicePath(root, index) + QLatin1Char('/') + relative, value + '\n');
}

QString writeNameTable(const QString &dir)
{
    const QString path = dir + QStringLiteral("/amdgpu.ids");
    writeFile(path,
              "# List of AMDGPU IDs\n#\n\n1.0.0\n"
              "744C,\tC8,\tAMD Radeon RX 7900 XTX\n"
              "744C,\tCC,\tAMD Radeon RX 7900 XT\n"
              "15BF,\tC2,\tAMD Radeon 780M Graphics\n"
              "13C0,\tC1,\tAMD Radeon Graphics, with a comma\n");
    return path;
}

bool loadCardReport(const QString &reportPath, const QString &root)
{
    QFile report(reportPath);
    if (!report.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    static const QRegularExpression cardLine(QStringLiteral("^== /sys/class/drm/(card\\d+)$"));
    static const QRegularExpression gcLine(QStringLiteral("^-- GC (major|minor): (\\S+)$"));
    QString device;
    QString file;       // multi-line value being collected
    QByteArray content;
    bool hexDump = false;
    auto flush = [&] {
        if (!file.isEmpty()) {
            if (hexDump) {
                content = QByteArray::fromHex(content.simplified().replace(' ', ""));
            }
            writeFile(device + QLatin1Char('/') + file, content);
        }
        file.clear();
        content.clear();
        hexDump = false;
    };
    while (!report.atEnd()) {
        const QString line = QString::fromUtf8(report.readLine()).chopped(1);
        if (const auto m = cardLine.match(line); m.hasMatch()) {
            flush();
            device = root + QStringLiteral("/class/drm/") + m.captured(1) + QStringLiteral("/device");
            continue;
        }
        if (device.isEmpty() || !line.startsWith(QLatin1String("-- "))) {
            if (!file.isEmpty()) {
                content += line.toUtf8() + (hexDump ? " " : "\n");
            }
            continue;
        }
        flush();
        if (const auto m = gcLine.match(line); m.hasMatch()) {
            writeFile(device + QStringLiteral("/ip_discovery/die/0/GC/0/") + m.captured(1), m.captured(2).toUtf8() + '\n');
        } else if (line.startsWith(QLatin1String("-- hwmon/"))) {
            const qsizetype colon = line.indexOf(QLatin1String(": "));
            const QString name = line.mid(9, (colon < 0 ? line.size() : colon) - 9);
            const QByteArray value = colon < 0 ? QByteArray() : line.mid(colon + 2).toUtf8();
            writeFile(device + QStringLiteral("/hwmon/hwmon0/") + name, value + '\n');
        } else if (line.startsWith(QLatin1String("-- fan_ctrl/"))) {
            file = QStringLiteral("gpu_od/fan_ctrl/") + line.mid(12);
        } else {
            file = line.mid(3);
            hexDump = file == QLatin1String("gpu_metrics");
        }
    }
    flush();
    return !device.isEmpty();
}

} // namespace gfxtune::test
