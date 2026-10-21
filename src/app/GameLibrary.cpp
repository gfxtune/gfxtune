// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "GameLibrary.h"

#include "steam/RunningGames.h"

#include <QHash>
#include <QMutex>
#include <QtConcurrentRun>

namespace gfxtune
{

namespace
{
constexpr int RunningPollMs = 3000;
}

// Reading a game's folder is the slow part of a scan. The result only
// changes when the game is updated or moved, so it is kept between scans for
// as long as gfxtune runs (in memory only).
class InspectionCache
{
public:
    steam::GameFileReport inspect(const steam::Game &game, const QStringList &executables, bool windows)
    {
        const QString key = game.installPath + QLatin1Char('\n') + game.buildId + QLatin1Char('\n')
            + (windows ? QLatin1Char('w') : QLatin1Char('l')) + executables.join(QLatin1Char('\n'));
        {
            QMutexLocker lock(&m_mutex);
            const auto it = m_entries.constFind(game.appId);
            if (it != m_entries.constEnd() && it->key == key) {
                return it->report;
            }
        }
        const steam::GameFileReport report = steam::inspectGameFiles(game.installPath, executables, windows);
        QMutexLocker lock(&m_mutex);
        m_entries.insert(game.appId, {key, report});
        return report;
    }

private:
    struct Entry {
        QString key;
        steam::GameFileReport report;
    };
    QMutex m_mutex;
    QHash<quint32, Entry> m_entries;
};

GameLibrary::GameLibrary(steam::ScanOptions options, const settings::SettingsStore *store, QObject *parent, QString procRoot)
    : QObject(parent)
    , m_options(std::move(options))
    , m_store(store)
    , m_procRoot(std::move(procRoot))
    , m_cache(std::make_shared<InspectionCache>())
    , m_watcher(this)
{
    m_filtered.setSourceModel(&m_model);
    connect(&m_future, &QFutureWatcher<steam::SteamScan>::finished, this, &GameLibrary::scanFinished);
    connect(&m_watcher, &steam::SteamWatcher::changed, this, &GameLibrary::refresh);
    m_runningPoll.setInterval(RunningPollMs);
    connect(&m_runningPoll, &QTimer::timeout, this, &GameLibrary::pollRunning);
}

GameLibrary::~GameLibrary()
{
    m_future.waitForFinished();
}

void GameLibrary::start()
{
    refresh();
    pollRunning();
    m_runningPoll.start();
}

void GameLibrary::refresh()
{
    if (m_scanning) {
        m_rescanPending = true;
        return;
    }
    // The first time, list the games quickly, then fill in what their folders
    // say. Later scans reuse the cache and need only one pass.
    runScan(m_loaded);
}

void GameLibrary::runScan(bool inspect)
{
    m_scanning = true;
    m_inspecting = inspect;
    Q_EMIT stateChanged();

    steam::ScanOptions options = m_options;
    options.inspectGameFiles = inspect;
    std::shared_ptr<InspectionCache> cache = m_cache;
    options.inspector = [cache](const steam::Game &game, const QStringList &executables, bool windows) {
        return cache->inspect(game, executables, windows);
    };
    m_future.setFuture(QtConcurrent::run([options] { return steam::scanSteam(options); }));
}

void GameLibrary::scanFinished()
{
    m_scan = m_future.result();
    m_model.setGames(m_scan.games);
    m_model.setCustomized(m_store->customizedGames());
    m_watcher.setPaths(m_scan.watchPaths);
    m_loaded = true;
    Q_EMIT gamesChanged();

    if (!m_inspecting) {
        runScan(true);
        return;
    }
    m_scanning = false;
    Q_EMIT stateChanged();
    if (m_rescanPending) {
        m_rescanPending = false;
        refresh();
    }
}

void GameLibrary::pollRunning()
{
    m_model.setRunning(steam::runningSteamApps(m_procRoot));
}

void GameLibrary::settingsChanged()
{
    m_model.setCustomized(m_store->customizedGames());
}

} // namespace gfxtune
