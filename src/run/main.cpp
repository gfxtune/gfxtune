// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

// gfxtune-run: sits in a game's Steam launch options as
// "gfxtune-run %command%", applies the game's gfxtune settings and then
// replaces itself with the game. Whatever goes wrong, the game still starts.

#include "launch/LastLaunch.h"
#include "launch/LaunchPlan.h"
#include "settings/SettingsStore.h"

#include <QDateTime>
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
    return launch::Screen{qRound(screen->size().width() * ratio), qRound(screen->size().height() * ratio),
                          qRound(screen->refreshRate())};
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc < 2) {
        std::fprintf(stderr, "Usage: gfxtune-run COMMAND [ARGUMENTS...]\n"
                             "gfxtune adds this to a game's Steam launch options as: gfxtune-run %%command%%\n");
        return 2;
    }
    char *const *command = argv + 1;

    const quint32 appId = steamAppId();
    if (appId == 0) {
        runCommand(command);
    }

    const settings::SettingsStore store(settings::SettingsStore::defaultBaseDir());
    const settings::GameSettings gameSettings = store.load(appId);
    if (gameSettings.isDefault()) {
        runCommand(command);
    }

    launch::LaunchContext context;
    const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (const QString &key : environment.keys()) {
        context.environment.insert(key, environment.value(key));
    }
    context.gamescopeInstalled = !QStandardPaths::findExecutable(QStringLiteral("gamescope")).isEmpty();
    QStringList commandLine;
    for (char *const *argument = command; *argument; ++argument) {
        commandLine.append(QString::fromLocal8Bit(*argument));
    }
    context.proton = launch::runsThroughProton(commandLine);
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
    std::perror("gfxtune-run: gamescope");
    runCommand(command);
}
