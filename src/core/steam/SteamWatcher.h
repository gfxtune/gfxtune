// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QStringList>
#include <QTimer>

namespace gfxtune::steam
{

// Reports when Steam's files change, so the game list can be read again.
// Steam rewrites several files in a burst; changes within `quietPeriod` of
// each other are reported once.
class SteamWatcher : public QObject
{
    Q_OBJECT

public:
    explicit SteamWatcher(QObject *parent = nullptr, int quietPeriodMs = 750);

    // Replaces what is watched. Call again after each scan: files that Steam
    // replaces by renaming drop out of the watch.
    void setPaths(const QStringList &paths);
    QStringList paths() const;

Q_SIGNALS:
    void changed();

private:
    QFileSystemWatcher m_watcher;
    QTimer m_quiet;
};

} // namespace gfxtune::steam
