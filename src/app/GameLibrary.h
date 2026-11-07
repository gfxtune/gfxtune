// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include "GameListModel.h"
#include "settings/SettingsStore.h"
#include "steam/SteamLibrary.h"
#include "steam/SteamWatcher.h"

#include <QDateTime>
#include <QFutureWatcher>
#include <QObject>
#include <QTimer>
#include <QtQmlIntegration>

#include <memory>
#include <optional>

namespace gfxtune
{

class InspectionCache;

// Keeps the game list current: scans Steam in the background, rescans when
// Steam's files change, and checks which game is running.
class GameLibrary : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")
    Q_PROPERTY(gfxtune::GameFilterModel *games READ filtered CONSTANT)
    Q_PROPERTY(bool loaded READ loaded NOTIFY stateChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY stateChanged)
    Q_PROPERTY(bool steamFound READ steamFound NOTIFY stateChanged)
    Q_PROPERTY(int gameCount READ gameCount NOTIFY stateChanged)
    Q_PROPERTY(bool steamRunning READ steamRunning NOTIFY stateChanged)
    // How many installed games follow the global settings.
    Q_PROPERTY(int gamesUsingGlobal READ gamesUsingGlobal NOTIFY profilesChanged)

public:
    GameLibrary(steam::ScanOptions options, const settings::SettingsStore *store, QObject *parent = nullptr,
                QString procRoot = QStringLiteral("/proc"));
    ~GameLibrary() override;

    // First scan, then watching. Separate from construction so tests and the
    // window can connect first.
    void start();
    Q_INVOKABLE void refresh();

    GameListModel *model() { return &m_model; }
    GameFilterModel *filtered() { return &m_filtered; }
    bool loaded() const { return m_loaded; }
    bool scanning() const { return m_scanning; }
    bool steamFound() const { return m_scan.steamFound; }
    int gameCount() const { return int(m_model.games().size()); }
    const steam::SteamScan &lastScan() const { return m_scan; }
    const steam::Game *game(quint32 appId) const { return m_model.find(appId); }

    // Call after gfxtune's own settings files change.
    void settingsChanged();
    int gamesUsingGlobal() const { return m_gamesUsingGlobal; }
    // Whether the game needs gfxtune-run in its launch options: the global
    // settings with its own changes have something to do in it, and gfxtune
    // does not hold back from it for its anti-cheat (see settings::addsItselfTo()).
    bool wantsLaunchLine(quint32 appId) const;
    bool holdsBackForAntiCheat(quint32 appId) const;

    // How often running games and Steam itself are checked.
    void setPollInterval(int ms) { m_runningPoll.setInterval(ms); }

    bool steamRunning() const { return m_steamRunning; }
    bool isRunning(quint32 appId) const { return m_running.contains(appId); }
    QString procRoot() const { return m_procRoot; }
    // What gfxtune-run needs to know about the game at launch.
    settings::GameFacts factsFor(quint32 appId) const;
    // The game's name, or a placeholder for one that is not installed.
    Q_INVOKABLE QString gameName(int appId) const;
    const settings::SettingsStore *store() const { return m_store; }

    // Where a game stands with gfxtune-run in its Steam launch options.
    enum class LaunchState {
        NotNeeded,       // no gfxtune settings, no line
        Added,           // settings, and the line is in place
        WaitingForSteam, // settings, line to be added once Steam is closed
        WaitingToRemove, // no settings, line to be removed once Steam is closed
        Problem,         // Steam's file could not be updated
        HeldBack,        // no line: anti-cheat, with in-game settings for all games
    };
    LaunchState launchState(quint32 appId) const;
    QString launchLineProblem() const { return m_launchLineProblem; }

Q_SIGNALS:
    void stateChanged();
    // The details of the games changed (after a scan).
    void gamesChanged();
    // A game's own settings or the global ones changed.
    void profilesChanged();
    // A game started or stopped.
    void runningChanged();

private:
    void runScan(bool inspect);
    void scanFinished();
    void pollRunning();
    void saveFacts();
    void reloadProfiles();
    void syncLaunchLines();

    steam::ScanOptions m_options;
    const settings::SettingsStore *m_store;
    QString m_procRoot;
    std::shared_ptr<InspectionCache> m_cache;

    GameListModel m_model;
    GameFilterModel m_filtered;
    steam::SteamWatcher m_watcher;
    QTimer m_runningPoll;
    QFutureWatcher<steam::SteamScan> m_future;

    steam::SteamScan m_scan;
    QSet<quint32> m_running;
    settings::GameSettings m_global;
    QHash<quint32, settings::GameProfile> m_profiles; // installed games with files
    int m_gamesUsingGlobal = 0;
    bool m_steamRunning = false;
    QString m_launchLineProblem;
    // Steam's file as the last successful sync left it, and the games that
    // had the line then: while neither changes there is nothing to do.
    struct Synced {
        QSet<quint32> wanted;
        QDateTime modified;
        qint64 size = -1;
    };
    std::optional<Synced> m_synced;
    QSet<QString> m_reportedWarnings; // Steam file problems already logged
    bool m_loaded = false;
    bool m_scanning = false;
    bool m_inspecting = false;
    bool m_rescanPending = false;
};

} // namespace gfxtune
