// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

// Builders for made-up Steam files and programs. Tests never read a real
// Steam installation.

#include "settings/SettingsStore.h"
#include "vdf/KeyValues.h"

#include <QByteArray>
#include <QFileDevice>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include <random>
#include <vector>

namespace gfxtune::test
{

vdf::Node value(const QString &key, const QString &value);
vdf::Node section(const QString &key, std::vector<vdf::Node> children);

// Valve text format, quoted and escaped the way Steam writes it.
QByteArray writeText(const vdf::Node &root);

// Valve binary format with string values. With `keyTable`, keys are written
// as indexes into it (appinfo.vdf version 29) and new keys are appended.
QByteArray writeBinary(const vdf::Node &root, QStringList *keyTable = nullptr);

// A complete appinfo.vdf; each node holds one app's "appinfo" contents.
QByteArray writeAppInfo(int version, const QList<QPair<quint32, vdf::Node>> &apps);

// Minimal Windows programs and Linux programs that import the given libraries.
QByteArray writePe(bool is64, const QStringList &imports, const QStringList &delayImports = {}, bool oldStyleDelay = false);
QByteArray writeElf(bool is64, const QStringList &needed);

bool writeFile(const QString &path, const QByteArray &data, bool executable = false);

// The owner, group and other bits of QFileDevice::Permissions, without the
// "current user" bits Qt adds, which depend on who runs the test.
inline constexpr QFileDevice::Permissions FileModeBits = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
    | QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::WriteOther
    | QFileDevice::ExeOther;

// Saves `settings` as a game's own: every setting that differs from the
// defaults becomes one of its changes.
bool saveGame(const settings::SettingsStore &store, quint32 appId, const settings::GameSettings &settings,
              const QString &name = QStringLiteral("Made-up Game"));

// Damaged copies of `data` for robustness tests: flipped bytes, cuts and
// inserted junk, from a fixed seed so failures can be reproduced.
QByteArray mutate(const QByteArray &data, std::mt19937 &random);

} // namespace gfxtune::test
