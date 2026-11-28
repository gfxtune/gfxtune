// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "GameLibrary.h"

#include "Logging.h"
#include "framegen/LsfgVk.h"
#include "settings/Availability.h"

#include "launch/LastLaunch.h"
#include "launch/LaunchLine.h"
#include "launch/LaunchLines.h"
#include "steam/RunningGames.h"
#include "steam/SteamProcess.h"

#include <KLocalizedString>
#include <QDir>
#include <QFileInfo>
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
            const QMutexLocker lock(&m_mutex);
            const auto it = m_entries.constFind(game.appId);
            if (it != m_entries.constEnd() && it->key == key) {
                return it->report;
            }
        }
        const steam::GameFileReport report = steam::inspectGameFiles(game.installPath, executables, windows);
        const QMutexLocker lock(&m_mutex);
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
    const std::shared_ptr<InspectionCache> cache = m_cache;
    options.inspector = [cache](const steam::Game &game, const QStringList &executables, bool windows) {
        return cache->inspect(game, executables, windows);
    };
    m_future.setFuture(QtConcurrent::run([options] { return steam::scanSteam(options); }));
}

void GameLibrary::scanFinished()
{
    m_scan = m_future.result();
    // For gfxtune-run, which does not read Steam's files.
    QString error;
    if (!framegen::writeDllNote(m_store->baseDir(), m_scan.losslessScaling.dll, &error)) {
        qCWarning(lcSettings) << "Could not write" << framegen::dllNotePath(m_store->baseDir()) << error;
    }
    // gfxtune's own record of games seen drawing with OpenGL.
    for (steam::Game &game : m_scan.games) {
        game.openGlSeen = m_store->loadFacts(game.appId).openGlSeen;
    }
    // Each problem once, not again with every scan.
    for (const QString &warning : std::as_const(m_scan.warnings)) {
        if (!m_reportedWarnings.contains(warning)) {
            m_reportedWarnings.insert(warning);
            qCWarning(lcSteam).noquote() << warning;
        }
    }
    m_model.setGames(m_scan.games);
    reloadProfiles();
    m_watcher.setPaths(m_scan.watchPaths);
    m_loaded = true;
    Q_EMIT gamesChanged();

    if (!m_inspecting) {
        runScan(true);
        return;
    }
    m_scanning = false;
    saveFacts();
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
    return found ? found->name : i18n("Game %1", QString::number(appId));
}

// gfxtune-run reads what the scan found from each game's settings file:
// anti-cheat keeps the filters and measuring out of a game until the user
// accepts the risk for it, also when an update added it.
void GameLibrary::saveFacts()
{
    for (const steam::Game &game : std::as_const(m_scan.games)) {
        QString error;
        if (!m_store->saveFacts(game.appId, settings::factsOf(game), game.name, &error)) {
            qCWarning(lcSettings).noquote() << "Could not save what was found in" << game.name << "(" << error << ")";
        }
    }
    reloadProfiles();
}

void GameLibrary::reloadProfiles()
{
    m_global = m_store->loadGlobal();
    m_profiles.clear();
    QHash<quint32, QString> notes;
    int usingGlobal = 0;
    for (const steam::Game &game : std::as_const(m_scan.games)) {
        const settings::GameProfile profile = m_store->loadProfile(game.appId);
        if (!profile.isEmpty()) {
            m_profiles.insert(game.appId, profile);
        }
        const qsizetype changes = profile.changed.size();
        if (!profile.usesGlobal) {
            notes.insert(game.appId, i18n("own settings only"));
        } else if (changes > 0) {
            notes.insert(game.appId, i18np("1 change", "%1 changes", changes));
        }
        usingGlobal += profile.usesGlobal ? 1 : 0;
    }
    m_model.setSettingsNotes(notes);
    m_gamesUsingGlobal = usingGlobal;
    updateFrameGenerationFiles();
    Q_EMIT profilesChanged();
}

// gfxtune-run writes a game's lsfg-vk file when the game starts, and
// lsfg-vk follows the file while the game runs: a change made here is
// written into it. A game set back to off keeps its file as it was, since
// gfxtune-run no longer points lsfg-vk at it.
void GameLibrary::updateFrameGenerationFiles()
{
    const QString base = m_store->baseDir();
    const QString dll = framegen::readDllNote(base);
    const QStringList folders = QDir(base + QStringLiteral("/lsfg-vk")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &folder : folders) {
        bool ok = false;
        const quint32 appId = folder.toUInt(&ok);
        const QString path = framegen::configPath(base, appId);
        if (!ok || !QFileInfo::exists(path)) {
            continue;
        }
        const settings::GameSettings s = settings::effectiveSettings(m_global, m_profiles.value(appId));
        QString error;
        if (s.frameGeneration > 0 && !framegen::writeConfig(path, framegen::configText(s, dll), &error)) {
            qCWarning(lcSettings) << "Could not write" << path << error;
        }
    }
}

bool GameLibrary::wantsLaunchLine(quint32 appId) const
{
    const steam::Game *found = game(appId);
    return found && settings::addsItselfTo(m_global, m_profiles.value(appId), settings::factsOf(*found));
}

bool GameLibrary::holdsBackForAntiCheat(quint32 appId) const
{
    const steam::Game *found = game(appId);
    return found && settings::holdsBackForAntiCheat(m_global, m_profiles.value(appId), settings::factsOf(*found));
}

void GameLibrary::pollRunning()
{
    const QHash<quint32, qint64> reapers = steam::steamGameReapers(m_procRoot);
    QSet<quint32> running;
    for (auto it = reapers.cbegin(); it != reapers.cend(); ++it) {
        running.insert(it.key());
    }
    if (running != m_running) {
        m_running = running;
        m_model.setRunning(running);
        Q_EMIT runningChanged();
    }
    learnGraphics(reapers);
    const bool steam = steam::isSteamRunning(m_procRoot);
    if (steam != m_steamRunning) {
        m_steamRunning = steam;
        Q_EMIT stateChanged();
        if (!steam) {
            syncLaunchLines(); // Steam just closed: catch up on waiting changes
        }
    }
}

// A native game that runs for a minute with gfxtune's layer in it and shows
// no Vulkan picture draws with OpenGL. From then on its page says so and the
// layer, which cannot reach it, stays out (settings::GameFacts::openGlSeen).
void GameLibrary::learnGraphics(const QHash<quint32, qint64> &reapers)
{
    bool learned = false;
    const QDateTime now = QDateTime::currentDateTime();
    for (auto it = reapers.cbegin(); it != reapers.cend(); ++it) {
        const quint32 appId = it.key();
        const steam::Game *found = game(appId);
        if (!found || found->runtime != steam::Runtime::Native || found->openGlSeen) {
            continue;
        }
        const std::optional<QDateTime> started = steam::processStartTime(it.value(), m_procRoot);
        // Each start is judged once, after its first minute.
        if (!started || started->secsTo(now) < launch::LearningSeconds || m_judged.value(appId) == *started) {
            continue;
        }
        m_judged.insert(appId, *started);
        const std::optional<launch::LastLaunch> launch = launch::readLastLaunch(m_store->baseDir(), appId);
        if (!launch
            || !launch::showedNoVulkanPicture(*launch, *started, now, launch::sawVulkanPicture(m_store->baseDir(), appId, launch->time))) {
            continue;
        }
        settings::GameFacts facts = settings::factsOf(*found);
        facts.openGlSeen = true;
        QString error;
        if (!m_store->saveFacts(appId, facts, found->name, &error)) {
            qCWarning(lcSettings).noquote() << "Could not save what was found in" << found->name << "(" << error << ")";
            continue;
        }
        qCDebug(lcLaunch).noquote() << found->name << "showed no Vulkan picture in its first minute: it draws with OpenGL";
        learned = true;
    }
    if (learned) {
        refresh(); // its page and its launch line follow
    }
}

void GameLibrary::checkGraphicsAgain(quint32 appId)
{
    const steam::Game *found = game(appId);
    if (!found || !found->openGlSeen) {
        return;
    }
    settings::GameFacts facts = settings::factsOf(*found);
    facts.openGlSeen = false;
    QString error;
    if (!m_store->saveFacts(appId, facts, found->name, &error)) {
        qCWarning(lcSettings).noquote() << "Could not save what was found in" << found->name << "(" << error << ")";
        return;
    }
    // The run going on now was judged already: the next start decides.
    refresh();
}

void GameLibrary::settingsChanged()
{
    reloadProfiles();
    syncLaunchLines();
}

void GameLibrary::syncLaunchLines()
{
    // Steam rewrites its settings from memory on exit, so edits made while
    // it runs would be lost. They wait instead.
    if (!m_loaded || m_scanning || m_steamRunning || !m_scan.steamFound || m_scan.localConfigPath.isEmpty()) {
        return;
    }
    QSet<quint32> wanted;
    for (const steam::Game &game : std::as_const(m_scan.games)) {
        if (wantsLaunchLine(game.appId)) {
            wanted.insert(game.appId);
        }
    }
    const QString &path = m_scan.localConfigPath;
    const QFileInfo before(path);
    if (m_synced && m_synced->wanted == wanted && before.exists() && before.lastModified() == m_synced->modified
        && before.size() == m_synced->size) {
        return;
    }
    QList<launch::LineChange> done;
    QString error;
    const bool ok = launch::syncLaunchLines(path, wanted, &done, &error);
    const QFileInfo after(path);
    m_synced.reset();
    if (ok && after.exists()) {
        m_synced = Synced{wanted, after.lastModified(), after.size()};
    }
    const QString problem = ok ? QString() : error;
    if (problem != m_launchLineProblem) {
        m_launchLineProblem = problem;
        if (!problem.isEmpty()) {
            qCWarning(lcLaunch).noquote() << "Could not update Steam's launch options:" << problem;
        }
        Q_EMIT gamesChanged();
    }
    for (const launch::LineChange &change : std::as_const(done)) {
        qCDebug(lcLaunch).noquote() << "Launch options of" << change.appId << "now:" << change.after;
    }
    if (!done.isEmpty()) {
        refresh(); // show the new lines without waiting for the file watch
    }
}

GameLibrary::LaunchState GameLibrary::launchState(quint32 appId) const
{
    const steam::Game *game = m_model.find(appId);
    const bool lineThere = game && launch::hasHelper(game->launchOptions);
    const bool wanted = wantsLaunchLine(appId);
    if (wanted && lineThere) {
        return LaunchState::Added;
    }
    if (!wanted && !lineThere) {
        return holdsBackForAntiCheat(appId) ? LaunchState::HeldBack : LaunchState::NotNeeded;
    }
    if (!m_launchLineProblem.isEmpty() && !m_steamRunning) {
        return LaunchState::Problem;
    }
    return wanted ? LaunchState::WaitingForSteam : LaunchState::WaitingToRemove;
}

} // namespace gfxtune
