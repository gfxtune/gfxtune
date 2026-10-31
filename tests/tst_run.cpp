// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

// Runs the real gfxtune-run program the way Steam would, with a script
// standing in for the game and another standing in for gamescope.

#include "launch/LastLaunch.h"
#include "settings/SettingsStore.h"
#include "support/Fixtures.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

using namespace gfxtune;
using settings::GameProfile;
using settings::GameSettings;

namespace
{

// Records its arguments (one per line, so spaces inside them show) and the
// variables gfxtune sets.
const QByteArray GameScript = "#!/bin/sh\n"
    "{\n"
    "  for a in \"$@\"; do printf 'arg=%s\\n' \"$a\"; done\n"
    "  for v in RADV_TEX_ANISO MESA_VK_WSI_PRESENT_MODE DXVK_CONFIG VKD3D_FRAME_RATE PROTON_FSR4_UPGRADE \\\n"
    "           GFXTUNE_FILTERS GFXTUNE_FILTERS_SETTINGS GFXTUNE_GLOBAL_SETTINGS GFXTUNE_FILTERS_REPORT \\\n"
    "           GFXTUNE_PERF_DIR vblank_mode; do\n"
    "    eval \"printf '%s=%s\\n' $v \\\"\\${$v-unset}\\\"\"\n"
    "  done\n"
    "} > \"$TEST_OUT\"\n";

// Notes that it ran, then starts the rest, like gamemoderun and mangohud.
QByteArray wrapperScript(const QByteArray &name)
{
    return "#!/bin/sh\necho " + name + " >> \"$TEST_OUT.wrappers\"\nexec \"$@\"\n";
}

// Records its own arguments, then starts what follows "--".
const QByteArray GamescopeScript = "#!/bin/sh\n"
    "printf '%s\\n' \"$*\" > \"$TEST_OUT.gamescope\"\n"
    "while [ \"$1\" != \"--\" ]; do shift; done\n"
    "shift\n"
    "exec \"$@\"\n";

} // namespace

class RunTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_out;

    QString path(const QString &name) const { return m_dir.filePath(name); }

    settings::SettingsStore store() const { return settings::SettingsStore(path(QStringLiteral("config/gfxtune"))); }

    QProcessEnvironment environment(const QString &appId) const
    {
        QProcessEnvironment env;
        env.insert(QStringLiteral("HOME"), path(QStringLiteral("home")));
        env.insert(QStringLiteral("XDG_CONFIG_HOME"), path(QStringLiteral("config")));
        env.insert(QStringLiteral("PATH"), path(QStringLiteral("bin")) + QStringLiteral(":/usr/bin:/bin"));
        env.insert(QStringLiteral("TEST_OUT"), m_out);
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        if (!appId.isEmpty()) {
            env.insert(QStringLiteral("SteamAppId"), appId);
        }
        return env;
    }

    int run(const QProcessEnvironment &env, const QStringList &arguments)
    {
        QFile::remove(m_out);
        QFile::remove(m_out + QStringLiteral(".gamescope"));
        QFile::remove(m_out + QStringLiteral(".wrappers"));
        QProcess process;
        process.setProcessEnvironment(env);
        process.start(QStringLiteral(GFXTUNE_RUN_PATH), arguments);
        if (!process.waitForFinished(15000)) {
            return -1;
        }
        return process.exitCode();
    }

    QMap<QString, QString> gameSaw() const
    {
        QMap<QString, QString> seen;
        QFile file(m_out);
        if (!file.open(QIODevice::ReadOnly)) {
            return seen;
        }
        int argument = 0;
        for (const QByteArray &line : file.readAll().split('\n')) {
            const qsizetype eq = line.indexOf('=');
            if (eq <= 0) {
                continue;
            }
            QString key = QString::fromUtf8(line.left(eq));
            if (key == QLatin1String("arg")) {
                key = QStringLiteral("arg%1").arg(argument++);
            }
            seen.insert(key, QString::fromUtf8(line.mid(eq + 1)));
        }
        return seen;
    }

    QString gamescopeSaw() const
    {
        QFile file(m_out + QStringLiteral(".gamescope"));
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
    }

    QStringList wrappersSaw() const
    {
        QFile file(m_out + QStringLiteral(".wrappers"));
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts) : QStringList();
    }

    QStringList gameCommand() const { return {path(QStringLiteral("game.sh")), QStringLiteral("-dx12"), QStringLiteral("two words")}; }

private Q_SLOTS:
    void initTestCase()
    {
        m_out = path(QStringLiteral("out.txt"));
        QVERIFY(test::writeFile(path(QStringLiteral("game.sh")), GameScript, true));
        QVERIFY(test::writeFile(path(QStringLiteral("bin/gamescope")), GamescopeScript, true));
        QVERIFY(test::writeFile(path(QStringLiteral("bin/gamemoderun")), wrapperScript("gamemoderun"), true));
        QVERIFY(test::writeFile(path(QStringLiteral("bin/mangohud")), wrapperScript("mangohud"), true));
        QVERIFY(QDir().mkpath(path(QStringLiteral("home"))));
    }

    void startsTheGameUnchangedWithoutAnId()
    {
        GameSettings s;
        s.anisotropicFiltering = 16;
        QVERIFY(test::saveGame(store(), 1001, s));
        QCOMPARE(run(environment({}), gameCommand()), 0);
        const auto seen = gameSaw();
        QCOMPARE(seen.value(QStringLiteral("arg0")), QStringLiteral("-dx12"));
        QCOMPARE(seen.value(QStringLiteral("arg1")), QStringLiteral("two words"));
        QCOMPARE(seen.value(QStringLiteral("RADV_TEX_ANISO")), QStringLiteral("unset"));
    }

    void startsTheGameUnchangedWithNothingToDo()
    {
        // Measuring switched off for all games, and nothing else set.
        GameSettings quiet;
        quiet.measurePerformance = false;
        QVERIFY(store().saveGlobal(quiet));
        QCOMPARE(run(environment(QStringLiteral("2002")), gameCommand()), 0);
        QCOMPARE(gameSaw().value(QStringLiteral("RADV_TEX_ANISO")), QStringLiteral("unset"));
        QCOMPARE(gameSaw().value(QStringLiteral("GFXTUNE_FILTERS")), QStringLiteral("unset"));
        QVERIFY(!launch::readLastLaunch(store().baseDir(), 2002));
        QVERIFY(store().saveGlobal(GameSettings{}));
    }

    // Every game follows the global settings, with its own changes on top;
    // the layer reads the global file first, then the game's.
    void appliesTheGlobalSettings()
    {
        GameSettings global;
        global.anisotropicFiltering = 8;
        global.filters.sharpening = 30;
        QVERIFY(store().saveGlobal(global));

        // A game without a file of its own.
        QCOMPARE(run(environment(QStringLiteral("2003")), gameCommand()), 0);
        QCOMPARE(gameSaw().value(QStringLiteral("RADV_TEX_ANISO")), QStringLiteral("8"));
        QCOMPARE(gameSaw().value(QStringLiteral("GFXTUNE_FILTERS")), QStringLiteral("1"));
        QCOMPARE(gameSaw().value(QStringLiteral("GFXTUNE_GLOBAL_SETTINGS")), store().globalPath());
        QCOMPARE(gameSaw().value(QStringLiteral("GFXTUNE_FILTERS_SETTINGS")), store().pathFor(2003));

        // A change of its own wins.
        GameProfile profile;
        profile.changed = {QStringLiteral("anisotropicFiltering")};
        profile.own.anisotropicFiltering = 2;
        QVERIFY(store().saveProfile(2003, profile, {}));
        QCOMPARE(run(environment(QStringLiteral("2003")), gameCommand()), 0);
        QCOMPARE(gameSaw().value(QStringLiteral("RADV_TEX_ANISO")), QStringLiteral("2"));

        // Out of the global settings, with a change of its own: neither the
        // global setting nor the global file.
        profile.usesGlobal = false;
        profile.changed = {QStringLiteral("filterContrast")};
        profile.own.filters.contrast = 10;
        QVERIFY(store().saveProfile(2003, profile, {}));
        QCOMPARE(run(environment(QStringLiteral("2003")), gameCommand()), 0);
        QCOMPARE(gameSaw().value(QStringLiteral("RADV_TEX_ANISO")), QStringLiteral("unset"));
        QCOMPARE(gameSaw().value(QStringLiteral("GFXTUNE_FILTERS")), QStringLiteral("1"));
        QCOMPARE(gameSaw().value(QStringLiteral("GFXTUNE_GLOBAL_SETTINGS")), QStringLiteral("unset"));
        QCOMPARE(gameSaw().value(QStringLiteral("GFXTUNE_PERF_DIR")), QStringLiteral("unset")); // not measured

        // Out of them with nothing of its own: started as Steam starts it.
        profile.changed.clear();
        QVERIFY(store().saveProfile(2003, profile, {}));
        QCOMPARE(run(environment(QStringLiteral("2003")), gameCommand()), 0);
        QCOMPARE(gameSaw().value(QStringLiteral("GFXTUNE_FILTERS")), QStringLiteral("unset"));

        QVERIFY(store().remove(2003));
        QVERIFY(store().saveGlobal(GameSettings{}));
    }

    void appliesTheGamesSettings()
    {
        GameSettings s;
        s.measurePerformance = false; // the variables, not the layer
        s.anisotropicFiltering = 8;
        s.verticalSync = settings::VerticalSync::Off;
        s.frameCapEnabled = true;
        s.frameCap = 90;
        s.fsr4 = settings::Fsr4::On;
        QVERIFY(test::saveGame(store(), 1001, s));
        QCOMPARE(run(environment(QStringLiteral("1001")), gameCommand()), 0);
        const auto seen = gameSaw();
        QCOMPARE(seen.value(QStringLiteral("arg0")), QStringLiteral("-dx12"));
        QCOMPARE(seen.value(QStringLiteral("arg1")), QStringLiteral("two words"));
        QCOMPARE(seen.value(QStringLiteral("RADV_TEX_ANISO")), QStringLiteral("8"));
        QCOMPARE(seen.value(QStringLiteral("MESA_VK_WSI_PRESENT_MODE")), QStringLiteral("immediate"));
        QCOMPARE(seen.value(QStringLiteral("VKD3D_FRAME_RATE")), QStringLiteral("90"));
        QCOMPARE(seen.value(QStringLiteral("PROTON_FSR4_UPGRADE")), QStringLiteral("1"));
        QVERIFY(seen.value(QStringLiteral("DXVK_CONFIG")).contains(QLatin1String("dxgi.maxFrameRate = 90")));
        QVERIFY(gamescopeSaw().isEmpty());

        const auto last = launch::readLastLaunch(store().baseDir(), 1001);
        QVERIFY(last);
        QVERIFY(last->applied.contains(QStringLiteral("FSR 4 on")));
        QVERIFY(qAbs(last->time.secsTo(QDateTime::currentDateTime())) < 60);
    }

    void keepsVariablesFromSteamsLaunchOptions()
    {
        GameSettings s;
        s.fsr4 = settings::Fsr4::On;
        QVERIFY(test::saveGame(store(), 1001, s));
        QProcessEnvironment env = environment(QStringLiteral("1001"));
        env.insert(QStringLiteral("PROTON_FSR4_UPGRADE"), QStringLiteral("4.0.2"));
        QCOMPARE(run(env, gameCommand()), 0);
        QCOMPARE(gameSaw().value(QStringLiteral("PROTON_FSR4_UPGRADE")), QStringLiteral("4.0.2"));
        QCOMPARE(launch::readLastLaunch(store().baseDir(), 1001)->keptFromSteam, QStringList{QStringLiteral("PROTON_FSR4_UPGRADE")});
    }

    void wrapsTheGameInGamescope()
    {
        GameSettings s;
        s.measurePerformance = false; // the variables, not the layer
        s.gamescope = true;
        s.renderScale = 50;
        s.frameCapEnabled = true;
        s.frameCap = 60;
        QVERIFY(test::saveGame(store(), 1001, s));
        QCOMPARE(run(environment(QStringLiteral("1001")), gameCommand()), 0);
        const QString args = gamescopeSaw();
        QVERIFY2(args.startsWith(QLatin1String("-W ")), qPrintable(args));
        QVERIFY(args.contains(QLatin1String(" -r 60 ")));
        QVERIFY(args.endsWith(QLatin1String("-f -- ") + path(QStringLiteral("game.sh")) + QLatin1String(" -dx12 two words")));
        // The game still ran, with its arguments intact, and no in-game cap.
        QCOMPARE(gameSaw().value(QStringLiteral("arg1")), QStringLiteral("two words"));
        QCOMPARE(gameSaw().value(QStringLiteral("VKD3D_FRAME_RATE")), QStringLiteral("unset"));
    }

    void capsProtonGamesInsideTheGameInGamescope()
    {
        // A stand-in for Proton: drops its verb and starts the game.
        const QString proton = path(QStringLiteral("tools/Proton 11.0/proton"));
        QVERIFY(test::writeFile(proton, "#!/bin/sh\nshift\nexec \"$@\"\n", true));
        GameSettings s;
        s.measurePerformance = false; // the variables, not the layer
        s.gamescope = true;
        s.frameCapEnabled = true;
        s.frameCap = 60;
        QVERIFY(test::saveGame(store(), 1001, s));
        QCOMPARE(run(environment(QStringLiteral("1001")), QStringList{proton, QStringLiteral("waitforexitandrun")} + gameCommand()), 0);
        // gamescope follows the monitor; DXVK and VKD3D-Proton hold the cap.
        const QString args = gamescopeSaw();
        QVERIFY2(args.startsWith(QLatin1String("-W ")), qPrintable(args));
        QVERIFY2(!args.contains(QLatin1String(" -r ")), qPrintable(args));
        QCOMPARE(gameSaw().value(QStringLiteral("VKD3D_FRAME_RATE")), QStringLiteral("60"));
        QVERIFY(gameSaw().value(QStringLiteral("DXVK_CONFIG")).contains(QLatin1String("dxgi.maxFrameRate = 60")));
        QVERIFY(launch::readLastLaunch(store().baseDir(), 1001)->applied.contains(QStringLiteral("Frame rate cap 60")));
    }

    void asksForTheFilterLayer()
    {
        GameSettings s;
        s.filters.sharpening = 35;
        QVERIFY(test::saveGame(store(), 1001, s));
        QCOMPARE(run(environment(QStringLiteral("1001")), gameCommand()), 0);
        QCOMPARE(gameSaw().value(QStringLiteral("GFXTUNE_FILTERS")), QStringLiteral("1"));
        QCOMPARE(gameSaw().value(QStringLiteral("GFXTUNE_FILTERS_SETTINGS")), store().pathFor(1001));
        QCOMPARE(gameSaw().value(QStringLiteral("GFXTUNE_FILTERS_REPORT")), launch::filterReportPath(store().baseDir(), 1001));
    }

    // Measuring is on for every game by default: a game with no settings at
    // all still starts through gfxtune to be measured.
    void measuresAGameWithoutSettings()
    {
        QVERIFY(store().remove(2004));
        QCOMPARE(run(environment(QStringLiteral("2004")), gameCommand()), 0);
        QCOMPARE(gameSaw().value(QStringLiteral("GFXTUNE_FILTERS")), QStringLiteral("1"));
        QCOMPARE(gameSaw().value(QStringLiteral("GFXTUNE_PERF_DIR")), store().baseDir() + QStringLiteral("/performance/2004"));
        QCOMPARE(gameSaw().value(QStringLiteral("RADV_TEX_ANISO")), QStringLiteral("unset"));
        QVERIFY(launch::readLastLaunch(store().baseDir(), 2004)->applied.contains(QStringLiteral("Performance measuring")));
    }

    void measuresIntoTheGamesFolder()
    {
        GameSettings s;
        s.frameCapEnabled = true; // measuring is on by default
        QVERIFY(test::saveGame(store(), 1001, s));
        const QString dir = store().baseDir() + QStringLiteral("/performance/1001");
        QDir(dir).removeRecursively();
        QCOMPARE(run(environment(QStringLiteral("1001")), gameCommand()), 0);
        QCOMPARE(gameSaw().value(QStringLiteral("GFXTUNE_FILTERS")), QStringLiteral("1"));
        QCOMPARE(gameSaw().value(QStringLiteral("GFXTUNE_PERF_DIR")), dir);
        QVERIFY(QFileInfo(dir).isDir()); // the layer never creates folders

        s.measurePerformance = false;
        QVERIFY(test::saveGame(store(), 1001, s));
        QCOMPARE(run(environment(QStringLiteral("1001")), gameCommand()), 0);
        QCOMPARE(gameSaw().value(QStringLiteral("GFXTUNE_PERF_DIR")), QStringLiteral("unset"));
        QCOMPARE(gameSaw().value(QStringLiteral("GFXTUNE_FILTERS")), QStringLiteral("unset"));
    }

    // gfxtune-run knows of anti-cheat only from what the app saved.
    void keepsTheLayerOutOfAntiCheatGames()
    {
        GameSettings s;
        s.filters.sharpening = 30;
        QVERIFY(test::saveGame(store(), 1004, s));
        QVERIFY(store().saveFacts(1004, settings::GameFacts{{QStringLiteral("Easy Anti-Cheat")}, false}));
        const QString dir = store().baseDir() + QStringLiteral("/performance/1004");
        QFile::remove(launch::lastLaunchPath(store().baseDir(), 1004));

        // Filters and measuring locked, nothing else: the game starts as Steam starts it.
        QCOMPARE(run(environment(QStringLiteral("1004")), gameCommand()), 0);
        QCOMPARE(gameSaw().value(QStringLiteral("GFXTUNE_FILTERS")), QStringLiteral("unset"));
        QVERIFY(!launch::readLastLaunch(store().baseDir(), 1004));

        // With a setting applied from outside the game, that applies, and the
        // report says what stays locked.
        s.anisotropicFiltering = 8;
        QVERIFY(test::saveGame(store(), 1004, s));
        QCOMPARE(run(environment(QStringLiteral("1004")), gameCommand()), 0);
        QCOMPARE(gameSaw().value(QStringLiteral("RADV_TEX_ANISO")), QStringLiteral("8"));
        QCOMPARE(gameSaw().value(QStringLiteral("GFXTUNE_FILTERS")), QStringLiteral("unset"));
        QCOMPARE(gameSaw().value(QStringLiteral("GFXTUNE_PERF_DIR")), QStringLiteral("unset"));
        QVERIFY(!QFileInfo::exists(dir));
        const auto last = launch::readLastLaunch(store().baseDir(), 1004);
        QVERIFY(last);
        QVERIFY(last->notApplied.contains(QStringLiteral("In-game filters and measuring: locked until the anti-cheat risk is accepted")));

        s.filtersRiskAccepted = true;
        QVERIFY(test::saveGame(store(), 1004, s));
        QCOMPARE(run(environment(QStringLiteral("1004")), gameCommand()), 0);
        QCOMPARE(gameSaw().value(QStringLiteral("GFXTUNE_FILTERS")), QStringLiteral("1"));
        QCOMPARE(gameSaw().value(QStringLiteral("GFXTUNE_PERF_DIR")), dir);
    }

    void startsThroughGameModeAndMangoHud()
    {
        GameSettings s;
        s.gameMode = true;
        s.mangoHud = true;
        s.verticalSync = settings::VerticalSync::Off;
        s.measurePerformance = false;
        QVERIFY(test::saveGame(store(), 1001, s));
        QCOMPARE(run(environment(QStringLiteral("1001")), gameCommand()), 0);
        QCOMPARE(wrappersSaw(), (QStringList{QStringLiteral("gamemoderun"), QStringLiteral("mangohud")}));
        QCOMPARE(gameSaw().value(QStringLiteral("arg1")), QStringLiteral("two words"));
        QCOMPARE(gameSaw().value(QStringLiteral("vblank_mode")), QStringLiteral("0"));

        // Inside gamescope, gamescope shows MangoHud through mangoapp.
        s.gamescope = true;
        QVERIFY(test::saveGame(store(), 1001, s));
        QVERIFY(test::writeFile(path(QStringLiteral("bin/mangoapp")), "#!/bin/sh\n", true));
        QCOMPARE(run(environment(QStringLiteral("1001")), gameCommand()), 0);
        QCOMPARE(wrappersSaw(), QStringList{QStringLiteral("gamemoderun")});
        QVERIFY2(gamescopeSaw().contains(QLatin1String(" --mangoapp -f -- ")), qPrintable(gamescopeSaw()));
        QVERIFY(QFile::remove(path(QStringLiteral("bin/mangoapp"))));
        QVERIFY(gameSaw().contains(QStringLiteral("arg0")));
    }

    void neverStartsASecondGamescope()
    {
        GameSettings s;
        s.gamescope = true;
        QVERIFY(test::saveGame(store(), 1001, s));
        QProcessEnvironment env = environment(QStringLiteral("1001"));
        env.insert(QStringLiteral("GAMESCOPE_WAYLAND_DISPLAY"), QStringLiteral("gamescope-0"));
        QCOMPARE(run(env, gameCommand()), 0);
        QVERIFY(gamescopeSaw().isEmpty());
        QVERIFY(gameSaw().contains(QStringLiteral("arg0")));
    }

    void startsTheGameWhenGamescopeIsMissing()
    {
        GameSettings s;
        s.gamescope = true;
        QVERIFY(test::saveGame(store(), 1001, s));
        QProcessEnvironment env = environment(QStringLiteral("1001"));
        // A search path with no gamescope at all, real or fake.
        QVERIFY(QDir().mkpath(path(QStringLiteral("empty-bin"))));
        env.insert(QStringLiteral("PATH"), path(QStringLiteral("empty-bin")));
        QCOMPARE(run(env, gameCommand()), 0);
        QVERIFY(gameSaw().contains(QStringLiteral("arg0")));
        QVERIFY(!launch::readLastLaunch(store().baseDir(), 1001)->notApplied.isEmpty());
    }

    void startsTheGameWithDamagedSettings()
    {
        QVERIFY(test::writeFile(store().pathFor(1001), QByteArray("\x00\xFF[[[garbage", 12)));
        QCOMPARE(run(environment(QStringLiteral("1001")), gameCommand()), 0);
        QVERIFY(gameSaw().contains(QStringLiteral("arg0")));
    }

    void reportsMisuse()
    {
        QCOMPARE(run(environment(QStringLiteral("1001")), {}), 2);
        QCOMPARE(run(environment(QStringLiteral("1001")), {path(QStringLiteral("no-such-game"))}), 127);
    }
};

QTEST_GUILESS_MAIN(RunTest)
#include "tst_run.moc"
