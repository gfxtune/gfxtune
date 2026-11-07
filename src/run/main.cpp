// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

// gfxtune-run: sits in a game's Steam launch options as
// "gfxtune-run %command%", applies the game's gfxtune settings and then
// replaces itself with the game. Whatever goes wrong, the game still starts.

#include "launch/LastLaunch.h"
#include "launch/LaunchPlan.h"
#include "performance/Performance.h"
#include "settings/SettingsStore.h"

#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QProcessEnvironment>
#include <QScreen>
#include <QStandardPaths>

#include <cstdio>
#include <vector>

#include <unistd.h>

using namespace gfxtune;

namespace
{

[[noreturn]] void runCommand(char *const command[])
{
    execvp(command[0], command);
    std::perror("gfxtune-run");
    _exit(127);
}

quint32 steamAppId()
{
    // Steam sets SteamAppId for every game it starts; the others are fallbacks.
    for (const char *name : {"SteamAppId", "SteamGameId", "STEAM_COMPAT_APP_ID"}) {
        bool ok = false;
        const quint32 id = qEnvironmentVariable(name).toUInt(&ok);
        if (ok && id != 0) {
            return id;
        }
    }
    return 0;
}

// The main monitor in physical pixels. Needs a connection to the display,
// so it is only made when gamescope will run.
std::optional<launch::Screen> mainScreen()
{
    int argc = 1;
    char name[] = "gfxtune-run";
    char *argv[] = {name, nullptr};
    // Our own arguments only: the game's must never reach Qt's parser.
    QGuiApplication app(argc, argv);
    const QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        return std::nullopt;
    }
    const qreal ratio = screen->devicePixelRatio();
    return launch::Screen{qRound(screen->size().width() * ratio), qRound(screen->size().height() * ratio), qRound(screen->refreshRate())};
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc < 2) {
        std::fprintf(stderr,
                     "Usage: gfxtune-run COMMAND [ARGUMENTS...]\n"
                     "gfxtune adds this to a game's Steam launch options as: gfxtune-run %%command%%\n");
        return 2;
    }
    char *const *command = argv + 1;

    const quint32 appId = steamAppId();
    if (appId == 0) {
        runCommand(command);
    }

    const settings::SettingsStore store(settings::SettingsStore::defaultBaseDir());
    const settings::GameProfile profile = store.loadProfile(appId);
    const settings::GameSettings gameSettings = settings::effectiveSettings(store.loadGlobal(), profile);
    const settings::GameFacts facts = store.loadFacts(appId);
    if (!settings::needsGfxtuneRun(gameSettings, facts)) {
        runCommand(command);
    }

    launch::LaunchContext context;
    const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (const QString &key : environment.keys()) {
        context.environment.insert(key, environment.value(key));
    }
    context.gamescopeInstalled = !QStandardPaths::findExecutable(QStringLiteral("gamescope")).isEmpty();
    context.gameModeInstalled = !QStandardPaths::findExecutable(QStringLiteral("gamemoderun")).isEmpty();
    context.mangoHudInstalled = !QStandardPaths::findExecutable(QStringLiteral("mangohud")).isEmpty();
    context.mangoAppInstalled = !QStandardPaths::findExecutable(QStringLiteral("mangoapp")).isEmpty();
    context.facts = facts;
    QStringList commandLine;
    for (char *const *argument = command; *argument; ++argument) {
        commandLine.append(QString::fromLocal8Bit(*argument));
    }
    context.proton = launch::runsThroughProton(commandLine);
    context.filterSettingsPath = store.pathFor(appId);
    if (profile.usesGlobal) {
        context.globalSettingsPath = store.globalPath();
    }
    context.filterReportPath = launch::filterReportPath(store.baseDir(), appId);
    // The layer writes its measurements here; it never creates folders.
    const QString performanceDir = performance::performanceDir(store.baseDir(), appId);
    if (gameSettings.measurePerformance && settings::layerWanted(gameSettings, context.facts) && QDir().mkpath(performanceDir)) {
        context.performanceDir = performanceDir;
    }
    if (launch::needsScreen(gameSettings) && context.gamescopeInstalled
        && !context.environment.contains(QStringLiteral("GAMESCOPE_WAYLAND_DISPLAY"))) {
        context.screen = mainScreen();
    }

    const launch::LaunchPlan plan = launch::planLaunch(gameSettings, context);
    launch::writeLastLaunch(store.baseDir(), appId, plan, QDateTime::currentDateTime());

    for (const auto &[name, value] : plan.environment) {
        qputenv(name.toLocal8Bit().constData(), value.toLocal8Bit());
    }
    if (plan.wrapper.isEmpty()) {
        runCommand(command);
    }

    std::vector<QByteArray> storage;
    std::vector<char *> full;
    for (const QString &part : plan.wrapper) {
        storage.push_back(part.toLocal8Bit());
    }
    for (QByteArray &part : storage) {
        full.push_back(part.data());
    }
    for (char *const *arg = command; *arg; ++arg) {
        full.push_back(*arg);
    }
    full.push_back(nullptr);
    execvp(full[0], full.data());
    // The wrapper could not start; start the game without it.
    std::fprintf(stderr, "gfxtune-run: could not start %s; starting the game without it\n", full[0]);
    runCommand(command);
}
