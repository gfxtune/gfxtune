// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include "GameSettings.h"

#include <QSet>
#include <QString>
#include <QStringList>

namespace gfxtune::settings
{

// gfxtune's own files: one readable INI file per game in <base>/games/,
// written atomically so a crash never leaves half a file. A game with only
// default settings has no file.
class SettingsStore
{
public:
    explicit SettingsStore(QString baseDir);

    // $XDG_CONFIG_HOME/gfxtune, usually ~/.config/gfxtune.
    static QString defaultBaseDir();

    QString baseDir() const { return m_baseDir; }
    QString pathFor(quint32 appId) const;

    // Unreadable or out-of-range values fall back to their defaults and are
    // listed in `warnings`; a damaged file never stops the game list.
    GameSettings load(quint32 appId, QStringList *warnings = nullptr) const;

    // `gameName` is written for people reading the file; it is not read back.
    bool save(quint32 appId, const GameSettings &settings, const QString &gameName, QString *error = nullptr) const;
    bool remove(quint32 appId, QString *error = nullptr) const;

    // What gfxtune found out about the game, kept next to its settings.
    // Written only for games that have a settings file.
    GameFacts loadFacts(quint32 appId) const;
    bool saveFacts(quint32 appId, const GameFacts &facts, QString *error = nullptr) const;
    // Games whose settings change how they start or look.
    QSet<quint32> customizedGames() const;

    // Deletes the whole gfxtune folder, and nothing outside it.
    bool removeAll(QString *error = nullptr) const;

private:
    QString m_baseDir;
};

} // namespace gfxtune::settings
