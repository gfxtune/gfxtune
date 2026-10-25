// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "GameSettingsEditor.h"

#include "GameLibrary.h"

#include "launch/LaunchLine.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QLocale>

namespace gfxtune
{

using settings::GameSettings;

GameSettingsEditor::GameSettingsEditor(GameLibrary *library, settings::SettingsStore *store, settings::SystemInfo system,
                                       QObject *parent, int saveDelayMs)
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
    connect(m_library, &GameLibrary::settingsRewritten, this, [this](quint32 appId) {
        if (appId == m_appId) {
            m_saveTimer.stop();
            m_unsaved = false;
            load();
        }
    });
    // The layer reports once the game has its first frame, a few seconds in.
    m_reportPoll.setInterval(2000);
    connect(&m_reportPoll, &QTimer::timeout, this, [this] {
        const auto report = launch::readFilterReport(m_store->baseDir(), m_appId);
        const bool changed = report.has_value() != m_filterReport.has_value()
            || (report && (report->time != m_filterReport->time || report->running != m_filterReport->running || report->detail != m_filterReport->detail));
        if (changed) {
            m_filterReport = report;
            Q_EMIT launchChanged();
        }
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
    if (appId < 0 || quint32(appId) == m_appId) {
        return;
    }
    flush();
    m_appId = quint32(appId);
    load();
    reloadLastLaunch();
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
    if (GameSettings::AnisotropicLevels.contains(level)) {
        edit([level](GameSettings &s) { s.anisotropicFiltering = level; });
    }
}

void GameSettingsEditor::setFrameCapEnabled(bool on)
{
    edit([on](GameSettings &s) { s.frameCapEnabled = on; });
}

void GameSettingsEditor::setFrameCap(int fps)
{
    const int value = qBound(GameSettings::MinFrameCap, fps, GameSettings::MaxFrameCap);
    edit([value](GameSettings &s) { s.frameCap = value; });
}

void GameSettingsEditor::setVerticalSync(int mode)
{
    if (mode >= int(settings::VerticalSync::GameDefault) && mode <= int(settings::VerticalSync::Adaptive)) {
        edit([mode](GameSettings &s) { s.verticalSync = settings::VerticalSync(mode); });
    }
}

void GameSettingsEditor::setFsr4(int choice)
{
    if (choice >= int(settings::Fsr4::ProtonDefault) && choice <= int(settings::Fsr4::Off)) {
        edit([choice](GameSettings &s) { s.fsr4 = settings::Fsr4(choice); });
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
    edit([on](GameSettings &s) { s.gamescope = on; });
}

void GameSettingsEditor::setUpscaleFilter(int filter)
{
    if (filter >= int(settings::UpscaleFilter::Fsr) && filter <= int(settings::UpscaleFilter::Pixel)) {
        edit([filter](GameSettings &s) { s.upscaleFilter = settings::UpscaleFilter(filter); });
    }
}

void GameSettingsEditor::setScalingMode(int mode)
{
    if (mode >= int(settings::ScalingMode::Auto) && mode <= int(settings::ScalingMode::Stretch)) {
        edit([mode](GameSettings &s) { s.scalingMode = settings::ScalingMode(mode); });
    }
}

void GameSettingsEditor::setRenderScale(int percent)
{
    if (GameSettings::RenderScales.contains(percent)) {
        edit([percent](GameSettings &s) { s.renderScale = percent; });
    }
}

void GameSettingsEditor::setSharpness(int value)
{
    const int bounded = qBound(0, value, GameSettings::MaxSharpness);
    edit([bounded](GameSettings &s) { s.sharpness = bounded; });
}

void GameSettingsEditor::setFiltersEnabled(bool on)
{
    editFilters([on](filters::Values &v) { v.enabled = on; });
}

void GameSettingsEditor::setFilterSharpening(int value)
{
    editFilters([value](filters::Values &v) { v.sharpening = filters::Sharpening.clamp(value); });
}

void GameSettingsEditor::setFilterBrightness(int value)
{
    editFilters([value](filters::Values &v) { v.brightness = filters::Brightness.clamp(value); });
}

void GameSettingsEditor::setFilterContrast(int value)
{
    editFilters([value](filters::Values &v) { v.contrast = filters::Contrast.clamp(value); });
}

void GameSettingsEditor::setFilterGamma(int value)
{
    editFilters([value](filters::Values &v) { v.gamma = filters::Gamma.clamp(value); });
}

void GameSettingsEditor::setFilterSaturation(int value)
{
    editFilters([value](filters::Values &v) { v.saturation = filters::Saturation.clamp(value); });
}

void GameSettingsEditor::setFilterVibrance(int value)
{
    editFilters([value](filters::Values &v) { v.vibrance = filters::Vibrance.clamp(value); });
}

void GameSettingsEditor::setFilterTemperature(int value)
{
    editFilters([value](filters::Values &v) { v.temperature = filters::Temperature.clamp(value); });
}

void GameSettingsEditor::resetFilterColours()
{
    editFilters([](filters::Values &v) {
        const filters::Values neutral;
        v.brightness = neutral.brightness;
        v.contrast = neutral.contrast;
        v.gamma = neutral.gamma;
        v.saturation = neutral.saturation;
        v.vibrance = neutral.vibrance;
        v.temperature = neutral.temperature;
    });
}

void GameSettingsEditor::acceptFiltersRisk()
{
    edit([](GameSettings &s) { s.filtersRiskAccepted = true; });
    flush(); // a decision like this is saved at once
}

void GameSettingsEditor::editFilters(const std::function<void(filters::Values &)> &change)
{
    if (!m_availability.filters.available || m_availability.filtersLocked) {
        return;
    }
    edit([&change](GameSettings &s) { change(s.filters); });
}

void GameSettingsEditor::resetToDefaults()
{
    edit([](GameSettings &s) { s = GameSettings{}; });
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
    if (!m_unsaved || m_appId == 0) {
        return;
    }
    m_unsaved = false;
    QString error;
    if (!m_store->save(m_appId, m_settings, gameName(), &error)) {
        setProblem(QStringLiteral("Could not save the settings: %1").arg(error));
    } else {
        setProblem({});
    }
    m_library->settingsChanged();
}

void GameSettingsEditor::edit(const std::function<void(GameSettings &)> &change)
{
    if (m_appId == 0) {
        return;
    }
    GameSettings next = m_settings;
    change(next);
    if (next == m_settings) {
        return;
    }
    m_settings = next;
    m_unsaved = true;
    Q_EMIT settingsChanged();
    updateAvailability();
    m_saveTimer.start();
}

void GameSettingsEditor::load()
{
    QStringList warnings;
    m_settings = m_appId != 0 ? m_store->load(m_appId, &warnings) : GameSettings{};
    setProblem(warnings.isEmpty() ? QString()
                                  : QStringLiteral("Some saved values could not be read and were reset: %1").arg(warnings.join(QStringLiteral("; "))));
    Q_EMIT settingsChanged();
    updateAvailability();
}

void GameSettingsEditor::updateAvailability()
{
    const steam::Game *game = m_library->game(m_appId);
    m_availability = game ? settings::availabilityFor(*game, m_settings, m_system) : settings::Availability{};
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
