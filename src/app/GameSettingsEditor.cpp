// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "GameSettingsEditor.h"

#include "GameLibrary.h"

#include "Logging.h"
#include "launch/LaunchLine.h"
#include "settings/Profile.h"

#include <QClipboard>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QVariantMap>

#include <utility>

namespace gfxtune
{

using settings::GameSettings;

GameSettingsEditor::GameSettingsEditor(GameLibrary *library, settings::SettingsStore *store, settings::SystemInfo system, QObject *parent,
                                       int saveDelayMs)
    : QObject(parent)
    , m_library(library)
    , m_store(store)
    , m_system(system)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(saveDelayMs);
    connect(&m_saveTimer, &QTimer::timeout, this, &GameSettingsEditor::flush);
    connect(m_library, &GameLibrary::gamesChanged, this, [this] {
        // A rescan can change what is known about the game, or remove it.
        Q_EMIT gameChanged();
        updateAvailability();
        Q_EMIT launchChanged();
    });
    connect(m_library, &GameLibrary::stateChanged, this, &GameSettingsEditor::launchChanged);
    // A game that just started has a fresh last-launch report.
    connect(m_library, &GameLibrary::runningChanged, this, &GameSettingsEditor::reloadLastLaunch);
    // The layer reports once the game has its first frame, a few seconds in.
    m_reportPoll.setInterval(2000);
    connect(&m_reportPoll, &QTimer::timeout, this, [this] {
        const auto report = launch::readFilterReport(m_store->baseDir(), m_appId);
        const bool changed = report.has_value() != m_filterReport.has_value()
            || (report
                && (report->time != m_filterReport->time || report->running != m_filterReport->running
                    || report->detail != m_filterReport->detail));
        if (changed) {
            m_filterReport = report;
            Q_EMIT launchChanged();
        }
        reloadSessions(); // the layer updates the running session every five seconds
        if (!m_library->isRunning(m_appId)) {
            m_reportPoll.stop();
        }
    });
}

GameSettingsEditor::~GameSettingsEditor()
{
    flush();
}

void GameSettingsEditor::setAppId(int appId)
{
    if (appId < 0 || (std::cmp_equal(appId, m_appId) && !m_global)) {
        return;
    }
    flush();
    m_appId = quint32(appId);
    m_global = false;
    load();
    reloadLastLaunch();
    reloadSessions();
    Q_EMIT gameChanged();
}

void GameSettingsEditor::selectGlobal()
{
    if (m_global) {
        return;
    }
    flush();
    m_appId = 0;
    m_global = true;
    load();
    reloadLastLaunch();
    reloadSessions();
    Q_EMIT gameChanged();
}

int GameSettingsEditor::launchState() const
{
    return int(m_library->launchState(m_appId));
}

QString GameSettingsEditor::launchLine() const
{
    const steam::Game *game = m_library->game(m_appId);
    return launch::withHelper(game ? game->launchOptions : QString());
}

QString GameSettingsEditor::launchProblem() const
{
    return m_library->launchLineProblem();
}

bool GameSettingsEditor::steamRunning() const
{
    return m_library->steamRunning();
}

QString GameSettingsEditor::lastLaunchTime() const
{
    return m_lastLaunch ? QLocale().toString(m_lastLaunch->time.toLocalTime(), QLocale::ShortFormat) : QString();
}

void GameSettingsEditor::copyLaunchLine() const
{
    if (QClipboard *clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(launchLine());
    }
}

void GameSettingsEditor::reloadLastLaunch()
{
    m_lastLaunch = m_appId != 0 ? launch::readLastLaunch(m_store->baseDir(), m_appId) : std::nullopt;
    m_filterReport = m_appId != 0 ? launch::readFilterReport(m_store->baseDir(), m_appId) : std::nullopt;
    if (m_appId != 0 && m_library->isRunning(m_appId)) {
        m_reportPoll.start();
    } else {
        m_reportPoll.stop();
    }
    Q_EMIT launchChanged();
}

QString GameSettingsEditor::lastLaunchFilters() const
{
    if (!m_lastLaunch || !m_lastLaunch->applied.contains(QStringLiteral("In-game filters"))) {
        return {};
    }
    // A report from before this launch is about an earlier one.
    if (m_filterReport && m_filterReport->time >= m_lastLaunch->time.addSecs(-1)) {
        return m_filterReport->running ? QStringLiteral("Drawn in the game (%1)").arg(m_filterReport->detail)
                                       : QStringLiteral("Not drawn: %1").arg(m_filterReport->detail);
    }
    if (m_library->isRunning(m_appId)) {
        return QStringLiteral("Waiting for the game's first frame");
    }
    return QStringLiteral("Not drawn: the game never showed a Vulkan picture (it may draw with OpenGL)");
}

bool GameSettingsEditor::hasGame() const
{
    return m_library->game(m_appId) != nullptr;
}

QString GameSettingsEditor::gameName() const
{
    const steam::Game *game = m_library->game(m_appId);
    return game ? game->name : QString();
}

QString GameSettingsEditor::runsAs() const
{
    const steam::Game *game = m_library->game(m_appId);
    if (!game) {
        return {};
    }
    switch (game->runtime) {
    case steam::Runtime::Native:
        return QStringLiteral("Runs natively");
    case steam::Runtime::Proton:
        return game->tool.displayName.isEmpty() ? QStringLiteral("Runs through Proton (Steam's default version)")
                                                : QStringLiteral("Runs through %1").arg(game->tool.displayName);
    case steam::Runtime::Unknown:
        break;
    }
    return QStringLiteral("Steam has not said how this game runs yet");
}

QString GameSettingsEditor::graphics() const
{
    const steam::Game *game = m_library->game(m_appId);
    if (!game) {
        return {};
    }
    const QString apis = steam::apiNames(game->apis);
    return apis.isEmpty() ? QStringLiteral("Graphics system not detected") : apis;
}

QUrl GameSettingsEditor::cover() const
{
    const steam::Game *game = m_library->game(m_appId);
    return game && !game->coverPath.isEmpty() ? QUrl::fromLocalFile(game->coverPath) : QUrl();
}

void GameSettingsEditor::setAnisotropicFiltering(int level)
{
    if (settings::offered(GameSettings::AnisotropicLevels, level)) {
        edit(QStringLiteral("anisotropicFiltering"), [level](GameSettings &s) { s.anisotropicFiltering = level; });
    }
}

void GameSettingsEditor::setFrameCapEnabled(bool on)
{
    edit(QStringLiteral("frameCap"), [on](GameSettings &s) { s.frameCapEnabled = on; });
}

void GameSettingsEditor::setFrameCap(int fps)
{
    const int value = qBound(GameSettings::MinFrameCap, fps, GameSettings::MaxFrameCap);
    edit(QStringLiteral("frameCap"), [value](GameSettings &s) { s.frameCap = value; });
}

void GameSettingsEditor::setVerticalSync(int mode)
{
    if (mode >= int(settings::VerticalSync::GameDefault) && mode <= int(settings::VerticalSync::Adaptive)) {
        edit(QStringLiteral("verticalSync"), [mode](GameSettings &s) { s.verticalSync = settings::VerticalSync(mode); });
    }
}

void GameSettingsEditor::setFsr4(int choice)
{
    if (choice >= int(settings::Fsr4::ProtonDefault) && choice <= int(settings::Fsr4::Off)) {
        edit(QStringLiteral("fsr4"), [choice](GameSettings &s) { s.fsr4 = settings::Fsr4(choice); });
    }
}

QString GameSettingsEditor::fsr4ProtonChoice() const
{
    const steam::Game *game = m_library->game(m_appId);
    switch (game ? game->tool.fsr4 : steam::Fsr4Support::Unknown) {
    case steam::Fsr4Support::BuiltIn:
        return QStringLiteral("Proton decides (on)");
    case steam::Fsr4Support::OnRequest:
    case steam::Fsr4Support::None:
        return QStringLiteral("Proton decides (off)");
    case steam::Fsr4Support::Unknown:
        break;
    }
    return QStringLiteral("Proton decides");
}

void GameSettingsEditor::setGamescope(bool on)
{
    edit(QStringLiteral("gamescope"), [on](GameSettings &s) { s.gamescope = on; });
}

void GameSettingsEditor::setUpscaleFilter(int filter)
{
    if (filter >= int(settings::UpscaleFilter::Fsr) && filter <= int(settings::UpscaleFilter::Pixel)) {
        edit(QStringLiteral("upscaleFilter"), [filter](GameSettings &s) { s.upscaleFilter = settings::UpscaleFilter(filter); });
    }
}

void GameSettingsEditor::setScalingMode(int mode)
{
    if (mode >= int(settings::ScalingMode::Auto) && mode <= int(settings::ScalingMode::Stretch)) {
        edit(QStringLiteral("scalingMode"), [mode](GameSettings &s) { s.scalingMode = settings::ScalingMode(mode); });
    }
}

void GameSettingsEditor::setRenderScale(int percent)
{
    if (settings::offered(GameSettings::RenderScales, percent)) {
        edit(QStringLiteral("renderScale"), [percent](GameSettings &s) { s.renderScale = percent; });
    }
}

void GameSettingsEditor::setSharpness(int value)
{
    const int bounded = qBound(0, value, GameSettings::MaxSharpness);
    edit(QStringLiteral("sharpness"), [bounded](GameSettings &s) { s.sharpness = bounded; });
}

void GameSettingsEditor::setLodBias(int hundredths)
{
    if (settings::offered(GameSettings::LodBiasLevels, hundredths)) {
        edit(QStringLiteral("lodBias"), [hundredths](GameSettings &s) { s.lodBias = hundredths; });
    }
}

void GameSettingsEditor::setAntiAliasing(int mode)
{
    // A choice, not an amount: an unknown method is ignored.
    if (mode == filters::AntiAliasing.clamp(mode)) {
        editFilters(QStringLiteral("antiAliasing"), [mode](filters::Values &v) { v.antiAliasing = mode; });
    }
}

void GameSettingsEditor::setMeasurePerformance(bool on)
{
    // Measuring runs inside the game like the filters: the same lock.
    if (m_availability.measurePerformance.available && !m_availability.filtersLocked) {
        edit(QStringLiteral("measurePerformance"), [on](GameSettings &s) { s.measurePerformance = on; });
    }
}

void GameSettingsEditor::setGameMode(bool on)
{
    edit(QStringLiteral("gameMode"), [on](GameSettings &s) { s.gameMode = on; });
}

void GameSettingsEditor::setMangoHud(bool on)
{
    edit(QStringLiteral("mangoHud"), [on](GameSettings &s) { s.mangoHud = on; });
}

QVariantList GameSettingsEditor::sessions() const
{
    // Readings of 0 could not be taken.
    const auto reading = [](double value, const QString &unit) {
        return value > 0 ? QStringLiteral("%1 %2").arg(qRound(value)).arg(unit) : QStringLiteral("—");
    };
    const QString degrees = QStringLiteral("°C");
    QVariantList list;
    for (const performance::Session &s : m_sessions) {
        const int minutes = std::max(qRound(s.seconds / 60.0), 1);
        QString played =
            minutes >= 60 ? QStringLiteral("%1 h %2 min").arg(minutes / 60).arg(minutes % 60) : QStringLiteral("%1 min").arg(minutes);
        if (s.running) {
            played += QStringLiteral(" so far");
        }
        QString hotspot = reading(s.hotspot, degrees);
        if (s.hotspot > 0 && s.hotspotMax > 0) {
            hotspot += QStringLiteral(" (%1)").arg(qRound(s.hotspotMax));
        }
        QVariantMap row;
        row.insert(QStringLiteral("date"), QLocale().toString(s.start.toLocalTime(), QLocale::ShortFormat));
        row.insert(QStringLiteral("played"), played);
        row.insert(QStringLiteral("averageFps"), QString::number(qRound(s.averageFps)));
        row.insert(QStringLiteral("low1"), QString::number(qRound(s.low1Fps)));
        row.insert(QStringLiteral("low01"), QString::number(qRound(s.low01Fps)));
        row.insert(QStringLiteral("temperature"), reading(s.edge, degrees));
        row.insert(QStringLiteral("hotspot"), hotspot);
        row.insert(QStringLiteral("power"), reading(s.power, QStringLiteral("W")));
        row.insert(QStringLiteral("running"), s.running);
        list.append(row);
    }
    return list;
}

std::function<bool(qint64)> GameSettingsEditor::processAlive() const
{
    return [proc = m_library->procRoot()](qint64 pid) { return QFileInfo::exists(proc + QStringLiteral("/%1").arg(pid)); };
}

void GameSettingsEditor::reloadSessions()
{
    m_sessions = m_appId != 0 ? performance::loadHistory(performance::performanceDir(m_store->baseDir(), m_appId), processAlive())
                              : QList<performance::Session>{};
    Q_EMIT sessionsChanged();
}

void GameSettingsEditor::clearSessions()
{
    if (m_appId != 0) {
        performance::clearHistory(performance::performanceDir(m_store->baseDir(), m_appId), processAlive());
    }
    reloadSessions();
}

void GameSettingsEditor::setFiltersEnabled(bool on)
{
    editFilters(QStringLiteral("filtersEnabled"), [on](filters::Values &v) { v.enabled = on; });
}

void GameSettingsEditor::setFilterSharpening(int value)
{
    editFilters(QStringLiteral("filterSharpening"), [value](filters::Values &v) { v.sharpening = filters::Sharpening.clamp(value); });
}

void GameSettingsEditor::setFilterBrightness(int value)
{
    editFilters(QStringLiteral("filterBrightness"), [value](filters::Values &v) { v.brightness = filters::Brightness.clamp(value); });
}

void GameSettingsEditor::setFilterContrast(int value)
{
    editFilters(QStringLiteral("filterContrast"), [value](filters::Values &v) { v.contrast = filters::Contrast.clamp(value); });
}

void GameSettingsEditor::setFilterGamma(int value)
{
    editFilters(QStringLiteral("filterGamma"), [value](filters::Values &v) { v.gamma = filters::Gamma.clamp(value); });
}

void GameSettingsEditor::setFilterSaturation(int value)
{
    editFilters(QStringLiteral("filterSaturation"), [value](filters::Values &v) { v.saturation = filters::Saturation.clamp(value); });
}

void GameSettingsEditor::setFilterVibrance(int value)
{
    editFilters(QStringLiteral("filterVibrance"), [value](filters::Values &v) { v.vibrance = filters::Vibrance.clamp(value); });
}

void GameSettingsEditor::setFilterTemperature(int value)
{
    editFilters(QStringLiteral("filterTemperature"), [value](filters::Values &v) { v.temperature = filters::Temperature.clamp(value); });
}

void GameSettingsEditor::resetFilterColours()
{
    const filters::Values neutral;
    setFilterBrightness(neutral.brightness);
    setFilterContrast(neutral.contrast);
    setFilterGamma(neutral.gamma);
    setFilterSaturation(neutral.saturation);
    setFilterVibrance(neutral.vibrance);
    setFilterTemperature(neutral.temperature);
}

void GameSettingsEditor::acceptFiltersRisk()
{
    if (m_global || m_appId == 0 || m_profile.own.filtersRiskAccepted) {
        return;
    }
    m_profile.own.filtersRiskAccepted = true;
    changed();
    flush(); // a decision like this is saved at once
}

void GameSettingsEditor::editFilters(const QString &setting, const std::function<void(filters::Values &)> &change)
{
    if (!m_availability.filters.available || m_availability.filtersLocked) {
        return;
    }
    edit(setting, [&change](GameSettings &s) { change(s.filters); });
}

GameSettings GameSettingsEditor::base() const
{
    return m_profile.usesGlobal ? m_globalSettings : settings::untouchedSettings();
}

bool GameSettingsEditor::customized() const
{
    return m_global ? !m_globalSettings.isDefault() : !m_profile.changed.isEmpty();
}

void GameSettingsEditor::setUsesGlobal(bool on)
{
    if (m_global || m_appId == 0 || on == m_profile.usesGlobal) {
        return;
    }
    m_profile.usesGlobal = on;
    changed();
}

QStringList GameSettingsEditor::changedSettings() const
{
    QStringList ids;
    for (const settings::SettingKind &kind : settings::settingKinds()) {
        if (!m_global && m_profile.changed.contains(kind.id)) {
            ids.append(kind.id);
        }
    }
    return ids;
}

QVariantMap GameSettingsEditor::baseValues() const
{
    const GameSettings b = base();
    const filters::Values &f = b.filters;
    return {
        {QStringLiteral("anisotropicFiltering"), b.anisotropicFiltering},
        {QStringLiteral("lodBias"), b.lodBias},
        {QStringLiteral("frameCapEnabled"), b.frameCapEnabled},
        {QStringLiteral("frameCap"), b.frameCap},
        {QStringLiteral("verticalSync"), int(b.verticalSync)},
        {QStringLiteral("fsr4"), int(b.fsr4)},
        {QStringLiteral("gamescope"), b.gamescope},
        {QStringLiteral("upscaleFilter"), int(b.upscaleFilter)},
        {QStringLiteral("scalingMode"), int(b.scalingMode)},
        {QStringLiteral("renderScale"), b.renderScale},
        {QStringLiteral("sharpness"), b.sharpness},
        {QStringLiteral("filtersEnabled"), f.enabled},
        {QStringLiteral("antiAliasing"), f.antiAliasing},
        {QStringLiteral("filterSharpening"), f.sharpening},
        {QStringLiteral("filterBrightness"), f.brightness},
        {QStringLiteral("filterContrast"), f.contrast},
        {QStringLiteral("filterGamma"), f.gamma},
        {QStringLiteral("filterSaturation"), f.saturation},
        {QStringLiteral("filterVibrance"), f.vibrance},
        {QStringLiteral("filterTemperature"), f.temperature},
        {QStringLiteral("measurePerformance"), b.measurePerformance},
        {QStringLiteral("gameMode"), b.gameMode},
        {QStringLiteral("mangoHud"), b.mangoHud},
    };
}

void GameSettingsEditor::useBase(const QString &setting)
{
    if (!m_global && m_profile.changed.remove(setting)) {
        changed();
    }
}

void GameSettingsEditor::resetToDefaults()
{
    if (m_global) {
        if (!m_globalSettings.isDefault()) {
            m_globalSettings = GameSettings{};
            changed();
        }
    } else if (m_appId != 0 && !m_profile.changed.isEmpty()) {
        m_profile.changed.clear();
        changed();
    }
    flush();
}

bool GameSettingsEditor::removeAllSettings()
{
    // A pending save would bring a file back.
    m_saveTimer.stop();
    m_unsaved = false;
    QString error;
    const bool ok = m_store->removeAll(&error);
    setProblem(ok ? QString() : error);
    load();
    reloadLastLaunch();
    Q_EMIT settingsChanged();
    updateAvailability();
    m_library->settingsChanged();
    return ok;
}

void GameSettingsEditor::flush()
{
    m_saveTimer.stop();
    if (!m_unsaved || (!m_global && m_appId == 0)) {
        return;
    }
    m_unsaved = false;
    QString error;
    const bool saved =
        m_global ? m_store->saveGlobal(m_globalSettings, &error) : m_store->saveProfile(m_appId, m_profile, gameName(), &error);
    if (!saved) {
        qCWarning(lcSettings).noquote() << "Could not save the settings:" << error;
    }
    setProblem(saved ? QString() : QStringLiteral("Could not save the settings: %1").arg(error));
    m_library->settingsChanged();
}

void GameSettingsEditor::edit(const QString &setting, const std::function<void(GameSettings &)> &change)
{
    if (!m_global && m_appId == 0) {
        return;
    }
    GameSettings next = m_settings;
    change(next);
    if (next == m_settings) {
        return;
    }
    if (m_global) {
        m_globalSettings = next;
    } else if (const settings::SettingKind *kind = settings::findSetting(setting)) {
        // Set back to what the game has anyway: no longer a change.
        kind->copy(next, m_profile.own);
        if (settings::sameSetting(*kind, next, base())) {
            m_profile.changed.remove(setting);
        } else {
            m_profile.changed.insert(setting);
        }
    }
    changed();
}

// After an edit: what the page shows follows, and a save is due.
void GameSettingsEditor::changed()
{
    m_settings = m_global ? m_globalSettings : settings::effectiveSettings(m_globalSettings, m_profile);
    m_unsaved = true;
    Q_EMIT settingsChanged();
    updateAvailability();
    m_saveTimer.start();
}

void GameSettingsEditor::load()
{
    QStringList warnings;
    m_globalSettings = m_store->loadGlobal(&warnings);
    m_profile = m_appId != 0 ? m_store->loadProfile(m_appId, &warnings) : settings::GameProfile{};
    m_settings = m_global ? m_globalSettings : settings::effectiveSettings(m_globalSettings, m_profile);
    for (const QString &warning : std::as_const(warnings)) {
        qCWarning(lcSettings).noquote() << warning;
    }
    setProblem(warnings.isEmpty()
                   ? QString()
                   : QStringLiteral("Some saved values could not be read and were reset: %1").arg(warnings.join(QStringLiteral("; "))));
    Q_EMIT settingsChanged();
    updateAvailability();
}

void GameSettingsEditor::updateAvailability()
{
    if (m_global) {
        m_availability = settings::availabilityForAll(m_settings, m_system);
    } else if (const steam::Game *game = m_library->game(m_appId)) {
        m_availability = settings::availabilityFor(*game, m_settings, m_system);
    } else {
        m_availability = settings::Availability{};
    }
    Q_EMIT availabilityChanged();
}

void GameSettingsEditor::setProblem(const QString &problem)
{
    if (problem != m_problem) {
        m_problem = problem;
        Q_EMIT problemChanged();
    }
}

} // namespace gfxtune
