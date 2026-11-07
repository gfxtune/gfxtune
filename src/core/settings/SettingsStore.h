// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include "GameSettings.h"
#include "Profile.h"

#include <QString>
#include <QStringList>

namespace gfxtune::settings
{

// gfxtune's own files, readable INI written atomically so a crash never
// leaves half a file: <base>/global.ini with the settings for all games, and
// one file per game in <base>/games/ with what is the game's own: its
// changes, whether it follows the global settings, the accepted anti-cheat
// warning and what gfxtune found out about it. Missing files mean defaults.
class SettingsStore
{
public:
    explicit SettingsStore(const QString &baseDir);

    // $XDG_CONFIG_HOME/gfxtune, usually ~/.config/gfxtune.
    static QString defaultBaseDir();

    QString baseDir() const { return m_baseDir; }
    QString pathFor(quint32 appId) const;
    QString globalPath() const;

    // Unreadable or out-of-range values fall back (to the global setting, or
    // to the default) and are listed in `warnings`; a damaged file never
    // stops the game list.
    GameSettings loadGlobal(QStringList *warnings = nullptr) const;
    bool saveGlobal(const GameSettings &settings, QString *error = nullptr) const;
    GameProfile loadProfile(quint32 appId, QStringList *warnings = nullptr) const;
    // `gameName` is written for people reading the file; it is not read back.
    bool saveProfile(quint32 appId, const GameProfile &profile, const QString &gameName, QString *error = nullptr) const;
    // What the game gets: the global settings with its own changes.
    GameSettings load(quint32 appId, QStringList *warnings = nullptr) const;
    bool remove(quint32 appId, QString *error = nullptr) const;

    // What gfxtune found out about the game, for gfxtune-run, which never
    // scans Steam. Kept in the game's file, made for it if need be.
    GameFacts loadFacts(quint32 appId) const;
    bool saveFacts(quint32 appId, const GameFacts &facts, const QString &gameName = {}, QString *error = nullptr) const;

    // Deletes the whole gfxtune folder, and nothing outside it.
    bool removeAll(QString *error = nullptr) const;

private:
    bool write(quint32 appId, const GameProfile &profile, const GameFacts &facts, const QString &gameName, QString *error) const;

    QString m_baseDir;
};

} // namespace gfxtune::settings
