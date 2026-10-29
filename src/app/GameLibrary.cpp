// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "GameLibrary.h"

#include "settings/Availability.h"

#include "launch/LaunchLine.h"
#include "launch/LaunchLines.h"
#include "steam/RunningGames.h"
#include "steam/SteamProcess.h"

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
    lockFiltersForAntiCheat();
    Q_EMIT stateChanged();
    if (m_rescanPending) {
        m_rescanPending = false;
        refresh();
        return;
    }
    syncLaunchLines();
}

settings::GameFacts GameLibrary::factsFor(quint32 appId) const
{
    const steam::Game *found = game(appId);
    return found ? settings::factsOf(*found) : settings::GameFacts{};
}

QString GameLibrary::gameName(int appId) const
{
    const steam::Game *found = appId > 0 ? game(quint32(appId)) : nullptr;
    return found ? found->name : QStringLiteral("Game %1").arg(appId);
}

// gfxtune-run reads what the scan found from each game's settings file.
// Filters stay locked in games with anti-cheat until the user accepts the
// risk; a game that gained anti-cheat in an update after filters were set
// gets them switched off (the values stay) until the user decides.
void GameLibrary::lockFiltersForAntiCheat()
{
    for (const steam::Game &game : std::as_const(m_scan.games)) {
        m_store->saveFacts(game.appId, settings::factsOf(game));
        if (game.antiCheat.isEmpty()) {
            continue;
        }
        settings::GameSettings s = m_store->load(game.appId);
        if (!s.filters.active() || s.filtersRiskAccepted) {
            continue;
        }
        s.filters.enabled = false;
        if (m_store->save(game.appId, s, game.name)) {
            Q_EMIT settingsRewritten(game.appId);
        }
    }
}

void GameLibrary::pollRunning()
{
    const QSet<quint32> running = steam::runningSteamApps(m_procRoot);
    if (running != m_running) {
        m_running = running;
        m_model.setRunning(running);
        Q_EMIT runningChanged();
    }
    const bool steam = steam::isSteamRunning(m_procRoot);
    if (steam != m_steamRunning) {
        m_steamRunning = steam;
        Q_EMIT stateChanged();
        if (!steam) {
            syncLaunchLines(); // Steam just closed: catch up on waiting changes
        }
    }
}

void GameLibrary::settingsChanged()
{
    m_model.setCustomized(m_store->customizedGames());
    syncLaunchLines();
}

void GameLibrary::syncLaunchLines()
{
    // Steam rewrites its settings from memory on exit, so edits made while
    // it runs would be lost. They wait instead.
    if (!m_loaded || m_scanning || m_steamRunning || !m_scan.steamFound || m_scan.localConfigPath.isEmpty()) {
        return;
    }
    QList<launch::LineChange> done;
    QString error;
    const bool ok = launch::syncLaunchLines(m_scan.localConfigPath, m_store->customizedGames(), &done, &error);
    const QString problem = ok ? QString() : error;
    if (problem != m_launchLineProblem) {
        m_launchLineProblem = problem;
        Q_EMIT gamesChanged();
    }
    if (!done.isEmpty()) {
        refresh(); // show the new lines without waiting for the file watch
    }
}

GameLibrary::LaunchState GameLibrary::launchState(quint32 appId) const
{
    const steam::Game *game = m_model.find(appId);
    const bool lineThere = game && launch::hasHelper(game->launchOptions);
    const bool wanted = m_store->customizedGames().contains(appId);
    if (wanted && lineThere) {
        return LaunchState::Added;
    }
    if (!wanted && !lineThere) {
        return LaunchState::NotNeeded;
    }
    if (!m_launchLineProblem.isEmpty() && !m_steamRunning) {
        return LaunchState::Problem;
    }
    return wanted ? LaunchState::WaitingForSteam : LaunchState::WaitingToRemove;
}

} // namespace gfxtune
