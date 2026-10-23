// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "FakeSysfs.h"

#include "Fixtures.h"

#include <QDir>
#include <QFile>

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
        {"temp1_label", "edge"},     {"temp1_input", "41000"},
        {"temp2_label", "junction"}, {"temp2_input", "46000"},
        {"temp3_label", "mem"},      {"temp3_input", "52000"},
        {"power1_average", "21000000"}, {"power1_cap", "315000000"},
        {"freq1_input", "28000000"}, {"freq2_input", "96000000"},
        {"fan1_input", "0"}, {"pwm1", "0"},
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
    writeFile(path, "# List of AMDGPU IDs\n#\n\n1.0.0\n"
                    "744C,\tC8,\tAMD Radeon RX 7900 XTX\n"
                    "744C,\tCC,\tAMD Radeon RX 7900 XT\n"
                    "15BF,\tC2,\tAMD Radeon 780M Graphics\n"
                    "13C0,\tC1,\tAMD Radeon Graphics, with a comma\n");
    return path;
}

} // namespace gfxtune::test
