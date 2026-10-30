// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include "GameSettings.h"

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace gfxtune::settings
{

// One setting as a page shows it: its name there and the keys that hold it
// in the settings files. The frame cap's switch and number are one setting.
struct SettingKind {
    QString id;
    QStringList keys;
    // Copies this setting from one set of settings to another.
    void (*copy)(const GameSettings &from, GameSettings &to);
};

// Every setting, in page order.
const QList<SettingKind> &settingKinds();
const SettingKind *findSetting(const QString &id);
// Whether two sets of settings agree on this setting.
bool sameSetting(const SettingKind &kind, const GameSettings &a, const GameSettings &b);

// What a game gets when gfxtune leaves it alone: no setting, no measuring.
GameSettings untouchedSettings();

// A game's own part of its settings: it follows the global settings, or
// when `usesGlobal` is off is left alone, with the settings named in
// `changed` taken from `own` instead. Accepting the anti-cheat risk
// (own.filtersRiskAccepted) is always the game's own.
struct GameProfile {
    bool usesGlobal = true;
    QSet<QString> changed;
    GameSettings own;

    // Nothing of the game's own: it follows the global settings throughout.
    bool isEmpty() const { return usesGlobal && changed.isEmpty() && !own.filtersRiskAccepted; }
    bool operator==(const GameProfile &other) const;
};

// What a game gets: the global settings (or nothing), with its changes.
GameSettings effectiveSettings(const GameSettings &global, const GameProfile &profile);

// A profile changing every setting in which `settings` differs from `base`.
GameProfile profileFrom(const GameSettings &settings, const GameSettings &base = {});

} // namespace gfxtune::settings
