// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include "launch/LastLaunch.h"
#include "settings/Availability.h"
#include "settings/GameSettings.h"
#include "settings/SettingsStore.h"

#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QtQmlIntegration>

#include <functional>

namespace gfxtune
{

class GameLibrary;

// The settings page's view of one game. Edits are saved shortly after the
// last change, and before switching to another game or quitting.
class GameSettingsEditor : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")

    Q_PROPERTY(int appId READ appId WRITE setAppId NOTIFY gameChanged)
    Q_PROPERTY(bool hasGame READ hasGame NOTIFY gameChanged)
    Q_PROPERTY(QString gameName READ gameName NOTIFY gameChanged)
    Q_PROPERTY(QString runsAs READ runsAs NOTIFY gameChanged)
    Q_PROPERTY(QString graphics READ graphics NOTIFY gameChanged)
    Q_PROPERTY(QUrl cover READ cover NOTIFY gameChanged)

    Q_PROPERTY(int anisotropicFiltering READ anisotropicFiltering WRITE setAnisotropicFiltering NOTIFY settingsChanged)
    Q_PROPERTY(bool frameCapEnabled READ frameCapEnabled WRITE setFrameCapEnabled NOTIFY settingsChanged)
    Q_PROPERTY(int frameCap READ frameCap WRITE setFrameCap NOTIFY settingsChanged)
    Q_PROPERTY(int verticalSync READ verticalSync WRITE setVerticalSync NOTIFY settingsChanged)
    Q_PROPERTY(int fsr4 READ fsr4 WRITE setFsr4 NOTIFY settingsChanged)
    Q_PROPERTY(bool gamescope READ gamescope WRITE setGamescope NOTIFY settingsChanged)
    Q_PROPERTY(int upscaleFilter READ upscaleFilter WRITE setUpscaleFilter NOTIFY settingsChanged)
    Q_PROPERTY(int scalingMode READ scalingMode WRITE setScalingMode NOTIFY settingsChanged)
    Q_PROPERTY(int renderScale READ renderScale WRITE setRenderScale NOTIFY settingsChanged)
    Q_PROPERTY(int sharpness READ sharpness WRITE setSharpness NOTIFY settingsChanged)
    Q_PROPERTY(bool customized READ customized NOTIFY settingsChanged)

    Q_PROPERTY(bool anisotropicAvailable READ anisotropicAvailable NOTIFY availabilityChanged)
    Q_PROPERTY(QString anisotropicNote READ anisotropicNote NOTIFY availabilityChanged)
    Q_PROPERTY(bool verticalSyncAvailable READ verticalSyncAvailable NOTIFY availabilityChanged)
    Q_PROPERTY(QString verticalSyncNote READ verticalSyncNote NOTIFY availabilityChanged)
    Q_PROPERTY(bool frameCapAvailable READ frameCapAvailable NOTIFY availabilityChanged)
    Q_PROPERTY(QString frameCapNote READ frameCapNote NOTIFY availabilityChanged)
    Q_PROPERTY(bool fsr4Available READ fsr4Available NOTIFY availabilityChanged)
    Q_PROPERTY(QString fsr4Note READ fsr4Note NOTIFY availabilityChanged)
    // The name of the "leave it to Proton" choice, with what Proton does.
    Q_PROPERTY(QString fsr4ProtonChoice READ fsr4ProtonChoice NOTIFY availabilityChanged)
    Q_PROPERTY(bool gamescopeAvailable READ gamescopeAvailable NOTIFY availabilityChanged)
    Q_PROPERTY(QString gamescopeNote READ gamescopeNote NOTIFY availabilityChanged)

    Q_PROPERTY(QString problem READ problem NOTIFY problemChanged)

    // gfxtune-run in Steam's launch options, and the last start.
    Q_PROPERTY(int launchState READ launchState NOTIFY launchChanged)
    Q_PROPERTY(QString launchLine READ launchLine NOTIFY launchChanged)
    Q_PROPERTY(QString launchProblem READ launchProblem NOTIFY launchChanged)
    Q_PROPERTY(bool steamRunning READ steamRunning NOTIFY launchChanged)
    Q_PROPERTY(bool hasLastLaunch READ hasLastLaunch NOTIFY launchChanged)
    Q_PROPERTY(QString lastLaunchTime READ lastLaunchTime NOTIFY launchChanged)
    Q_PROPERTY(QString lastLaunchApplied READ lastLaunchApplied NOTIFY launchChanged)
    Q_PROPERTY(QString lastLaunchKept READ lastLaunchKept NOTIFY launchChanged)
    Q_PROPERTY(QString lastLaunchNotApplied READ lastLaunchNotApplied NOTIFY launchChanged)

public:
    GameSettingsEditor(GameLibrary *library, settings::SettingsStore *store, settings::SystemInfo system,
                       QObject *parent = nullptr, int saveDelayMs = 400);
    ~GameSettingsEditor() override;

    int appId() const { return int(m_appId); }
    void setAppId(int appId);
    bool hasGame() const;
    QString gameName() const;
    QString runsAs() const;
    QString graphics() const;
    QUrl cover() const;

    int anisotropicFiltering() const { return m_settings.anisotropicFiltering; }
    void setAnisotropicFiltering(int level);
    bool frameCapEnabled() const { return m_settings.frameCapEnabled; }
    void setFrameCapEnabled(bool on);
    int frameCap() const { return m_settings.frameCap; }
    void setFrameCap(int fps);
    int verticalSync() const { return int(m_settings.verticalSync); }
    void setVerticalSync(int mode);
    int fsr4() const { return int(m_settings.fsr4); }
    void setFsr4(int choice);
    bool gamescope() const { return m_settings.gamescope; }
    void setGamescope(bool on);
    int upscaleFilter() const { return int(m_settings.upscaleFilter); }
    void setUpscaleFilter(int filter);
    int scalingMode() const { return int(m_settings.scalingMode); }
    void setScalingMode(int mode);
    int renderScale() const { return m_settings.renderScale; }
    void setRenderScale(int percent);
    int sharpness() const { return m_settings.sharpness; }
    void setSharpness(int value);
    bool customized() const { return !m_settings.isDefault(); }

    bool anisotropicAvailable() const { return m_availability.anisotropicFiltering.available; }
    QString anisotropicNote() const { return m_availability.anisotropicFiltering.note; }
    bool verticalSyncAvailable() const { return m_availability.verticalSync.available; }
    QString verticalSyncNote() const { return m_availability.verticalSync.note; }
    bool frameCapAvailable() const { return m_availability.frameCap.available; }
    QString frameCapNote() const { return m_availability.frameCap.note; }
    bool fsr4Available() const { return m_availability.fsr4.available; }
    QString fsr4Note() const { return m_availability.fsr4.note; }
    QString fsr4ProtonChoice() const;
    bool gamescopeAvailable() const { return m_availability.gamescope.available; }
    QString gamescopeNote() const { return m_availability.gamescope.note; }

    QString problem() const { return m_problem; }

    int launchState() const;
    QString launchLine() const;
    QString launchProblem() const;
    bool steamRunning() const;
    bool hasLastLaunch() const { return m_lastLaunch.has_value(); }
    QString lastLaunchTime() const;
    QString lastLaunchApplied() const { return m_lastLaunch ? m_lastLaunch->applied.join(QStringLiteral(", ")) : QString(); }
    QString lastLaunchKept() const { return m_lastLaunch ? m_lastLaunch->keptFromSteam.join(QStringLiteral(", ")) : QString(); }
    QString lastLaunchNotApplied() const { return m_lastLaunch ? m_lastLaunch->notApplied.join(QStringLiteral("; ")) : QString(); }
    Q_INVOKABLE void copyLaunchLine() const;

    const settings::GameSettings &current() const { return m_settings; }

    Q_INVOKABLE void resetToDefaults();
    Q_INVOKABLE bool removeAllSettings();
    // Writes a pending change now.
    Q_INVOKABLE void flush();

Q_SIGNALS:
    void gameChanged();
    void settingsChanged();
    void availabilityChanged();
    void problemChanged();
    void launchChanged();

private:
    void edit(const std::function<void(settings::GameSettings &)> &change);
    void load();
    void updateAvailability();
    void setProblem(const QString &problem);
    void reloadLastLaunch();

    GameLibrary *m_library;
    settings::SettingsStore *m_store;
    settings::SystemInfo m_system;
    QTimer m_saveTimer;

    quint32 m_appId = 0;
    settings::GameSettings m_settings;
    bool m_unsaved = false;
    settings::Availability m_availability;
    QString m_problem;
    std::optional<launch::LastLaunch> m_lastLaunch;
};

} // namespace gfxtune
