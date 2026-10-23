// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

// A made-up /sys with graphics cards, for the card panel's tests.

#include <QString>

namespace gfxtune::test
{

struct FakeCard {
    QString vendor = QStringLiteral("0x1002");
    QString device = QStringLiteral("0x744c");
    QString revision = QStringLiteral("0xcc");
    quint64 vramTotal = 21458059264ull; // 20 GiB minus a little, as the 7900 XT reports
    bool hwmon = true;
    QString runtimeStatus = QStringLiteral("active");
};

// Adds card<index> under <root>/class/drm with typical idle readings.
void addCard(const QString &root, int index, const FakeCard &card = {});
// Changes one file of card<index>, for example "hwmon/hwmon0/temp1_input".
void setCardFile(const QString &root, int index, const QString &relative, const QByteArray &value);
// A small amdgpu.ids with the 7900 cards.
QString writeNameTable(const QString &dir);

// Recreates the card files recorded in a card report (the read-only command
// users run to share their card's sysfs files) under <root>/class/drm.
bool loadCardReport(const QString &reportPath, const QString &root);

} // namespace gfxtune::test
