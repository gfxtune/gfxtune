// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include "GameListModel.h"
#include "settings/SettingsStore.h"
#include "steam/SteamLibrary.h"
#include "steam/SteamWatcher.h"

#include <QFutureWatcher>
#include <QObject>
#include <QTimer>
#include <QtQmlIntegration>

#include <memory>

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

Q_SIGNALS:
    void stateChanged();
    // The details of the games changed (after a scan).
    void gamesChanged();

private:
    void runScan(bool inspect);
    void scanFinished();
    void pollRunning();

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
    bool m_loaded = false;
    bool m_scanning = false;
    bool m_inspecting = false;
    bool m_rescanPending = false;
};

} // namespace gfxtune
