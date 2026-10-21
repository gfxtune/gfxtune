// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

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
    Q_PROPERTY(bool fsr4Upgrade READ fsr4Upgrade WRITE setFsr4Upgrade NOTIFY settingsChanged)
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
    Q_PROPERTY(bool gamescopeAvailable READ gamescopeAvailable NOTIFY availabilityChanged)
    Q_PROPERTY(QString gamescopeNote READ gamescopeNote NOTIFY availabilityChanged)

    Q_PROPERTY(QString problem READ problem NOTIFY problemChanged)

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
    bool fsr4Upgrade() const { return m_settings.fsr4Upgrade; }
    void setFsr4Upgrade(bool on);
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
    bool fsr4Available() const { return m_availability.fsr4Upgrade.available; }
    QString fsr4Note() const { return m_availability.fsr4Upgrade.note; }
    bool gamescopeAvailable() const { return m_availability.gamescope.available; }
    QString gamescopeNote() const { return m_availability.gamescope.note; }

    QString problem() const { return m_problem; }

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

private:
    void edit(const std::function<void(settings::GameSettings &)> &change);
    void load();
    void updateAvailability();
    void setProblem(const QString &problem);

    GameLibrary *m_library;
    settings::SettingsStore *m_store;
    settings::SystemInfo m_system;
    QTimer m_saveTimer;

    quint32 m_appId = 0;
    settings::GameSettings m_settings;
    bool m_unsaved = false;
    settings::Availability m_availability;
    QString m_problem;
};

} // namespace gfxtune
