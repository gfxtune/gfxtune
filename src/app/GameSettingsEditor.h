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

    // In-game filters (see src/filters/Filters.h for the ranges).
    Q_PROPERTY(bool filtersEnabled READ filtersEnabled WRITE setFiltersEnabled NOTIFY settingsChanged)
    Q_PROPERTY(int filterSharpening READ filterSharpening WRITE setFilterSharpening NOTIFY settingsChanged)
    Q_PROPERTY(int filterBrightness READ filterBrightness WRITE setFilterBrightness NOTIFY settingsChanged)
    Q_PROPERTY(int filterContrast READ filterContrast WRITE setFilterContrast NOTIFY settingsChanged)
    Q_PROPERTY(int filterGamma READ filterGamma WRITE setFilterGamma NOTIFY settingsChanged)
    Q_PROPERTY(int filterSaturation READ filterSaturation WRITE setFilterSaturation NOTIFY settingsChanged)
    Q_PROPERTY(int filterVibrance READ filterVibrance WRITE setFilterVibrance NOTIFY settingsChanged)
    Q_PROPERTY(int filterTemperature READ filterTemperature WRITE setFilterTemperature NOTIFY settingsChanged)
    Q_PROPERTY(bool filterColoursNeutral READ filterColoursNeutral NOTIFY settingsChanged)

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
    Q_PROPERTY(bool filtersAvailable READ filtersAvailable NOTIFY availabilityChanged)
    Q_PROPERTY(QString filtersNote READ filtersNote NOTIFY availabilityChanged)
    Q_PROPERTY(bool filtersLocked READ filtersLocked NOTIFY availabilityChanged)
    Q_PROPERTY(QString filtersWarning READ filtersWarning NOTIFY availabilityChanged)

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
    // What the filter layer reported from inside the game, in words.
    Q_PROPERTY(QString lastLaunchFilters READ lastLaunchFilters NOTIFY launchChanged)

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
    bool customized() const { return m_settings.affectsGame(); }

    bool filtersEnabled() const { return m_settings.filters.enabled; }
    void setFiltersEnabled(bool on);
    int filterSharpening() const { return m_settings.filters.sharpening; }
    void setFilterSharpening(int value);
    int filterBrightness() const { return m_settings.filters.brightness; }
    void setFilterBrightness(int value);
    int filterContrast() const { return m_settings.filters.contrast; }
    void setFilterContrast(int value);
    int filterGamma() const { return m_settings.filters.gamma; }
    void setFilterGamma(int value);
    int filterSaturation() const { return m_settings.filters.saturation; }
    void setFilterSaturation(int value);
    int filterVibrance() const { return m_settings.filters.vibrance; }
    void setFilterVibrance(int value);
    int filterTemperature() const { return m_settings.filters.temperature; }
    void setFilterTemperature(int value);
    bool filterColoursNeutral() const { return m_settings.filters.colourNeutral(); }
    // Unlocks the filters in a game with anti-cheat: the user takes the risk.
    Q_INVOKABLE void acceptFiltersRisk();
    Q_INVOKABLE void resetFilterColours();

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
    bool filtersAvailable() const { return m_availability.filters.available; }
    QString filtersNote() const { return m_availability.filters.note; }
    bool filtersLocked() const { return m_availability.filtersLocked; }
    QString filtersWarning() const { return m_availability.filtersWarning; }

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
    QString lastLaunchFilters() const;
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
    // Changes a filter value, unless the filters are unavailable or locked.
    void editFilters(const std::function<void(filters::Values &)> &change);
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
    std::optional<launch::FilterReport> m_filterReport;
    // While the selected game runs, its filter report can still arrive.
    QTimer m_reportPoll;
};

} // namespace gfxtune
