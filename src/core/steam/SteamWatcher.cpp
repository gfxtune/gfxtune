// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "SteamWatcher.h"

namespace gfxtune::steam
{

SteamWatcher::SteamWatcher(QObject *parent, int quietPeriodMs)
    : QObject(parent)
{
    m_quiet.setSingleShot(true);
    m_quiet.setInterval(quietPeriodMs);
    connect(&m_quiet, &QTimer::timeout, this, &SteamWatcher::changed);
    const auto restart = [this] { m_quiet.start(); };
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, restart);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, restart);
}

void SteamWatcher::setPaths(const QStringList &paths)
{
    const QStringList current = m_watcher.files() + m_watcher.directories();
    if (!current.isEmpty()) {
        m_watcher.removePaths(current);
    }
    if (!paths.isEmpty()) {
        m_watcher.addPaths(paths);
    }
}

QStringList SteamWatcher::paths() const
{
    return m_watcher.files() + m_watcher.directories();
}

} // namespace gfxtune::steam
