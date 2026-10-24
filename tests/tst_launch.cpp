// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "launch/LastLaunch.h"
#include "launch/LaunchLine.h"
#include "launch/LaunchLines.h"
#include "launch/LaunchPlan.h"
#include "steam/SteamProcess.h"
#include "support/Fixtures.h"
#include "vdf/TextVdf.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <unistd.h>

using namespace gfxtune;
using namespace gfxtune::launch;
using settings::GameSettings;
using settings::ScalingMode;
using settings::UpscaleFilter;
using settings::VerticalSync;

namespace
{

QString envValue(const LaunchPlan &plan, const QString &name)
{
    for (const auto &[key, value] : plan.environment) {
        if (key == name) {
            return value;
        }
    }
    return {};
}

bool setsEnv(const LaunchPlan &plan, const QString &name)
{
    for (const auto &[key, value] : plan.environment) {
        if (key == name) {
            return true;
        }
    }
    return false;
}

LaunchContext context(bool gamescopeInstalled = true, std::optional<Screen> screen = Screen{2560, 1440, 165})
{
    LaunchContext c;
    c.environment.insert(QStringLiteral("HOME"), QStringLiteral("/home/user"));
    c.gamescopeInstalled = gamescopeInstalled;
    c.screen = screen;
    return c;
}

} // namespace

class LaunchTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void addsTheHelperBeforeTheCommand_data()
    {
        QTest::addColumn<QString>("before");
        QTest::addColumn<QString>("after");
        QTest::newRow("empty") << QString() << QStringLiteral("gfxtune-run %command%");
        QTest::newRow("spaces only") << QStringLiteral("   ") << QStringLiteral("gfxtune-run %command%");
        QTest::newRow("bare command") << QStringLiteral("%command%") << QStringLiteral("gfxtune-run %command%");
        QTest::newRow("wrapper kept first") << QStringLiteral("gamemoderun %command%") << QStringLiteral("gamemoderun gfxtune-run %command%");
        QTest::newRow("variables and arguments")
            << QStringLiteral("PROTON_LOG=1 mangohud %command% -dx12 --skip-intro")
            << QStringLiteral("PROTON_LOG=1 mangohud gfxtune-run %command% -dx12 --skip-intro");
        QTest::newRow("plain arguments keep their meaning")
            << QStringLiteral(" -novid -high ") << QStringLiteral("gfxtune-run %command% -novid -high");
        QTest::newRow("already there") << QStringLiteral("gamemoderun gfxtune-run %command%") << QStringLiteral("gamemoderun gfxtune-run %command%");
        QTest::newRow("already there by path") << QStringLiteral("/usr/bin/gfxtune-run %command%") << QStringLiteral("/usr/bin/gfxtune-run %command%");
        QTest::newRow("user's own gamescope") << QStringLiteral("gamescope -f -- %command%") << QStringLiteral("gamescope -f -- gfxtune-run %command%");
    }

    void addsTheHelperBeforeTheCommand()
    {
        QFETCH(QString, before);
        QFETCH(QString, after);
        QCOMPARE(withHelper(before), after);
        QVERIFY(hasHelper(withHelper(before)));
    }

    void removesOnlyTheHelper_data()
    {
        QTest::addColumn<QString>("before");
        QTest::addColumn<QString>("after");
        QTest::newRow("only the helper") << QStringLiteral("gfxtune-run %command%") << QString();
        QTest::newRow("with a wrapper") << QStringLiteral("gamemoderun gfxtune-run %command%") << QStringLiteral("gamemoderun %command%");
        QTest::newRow("full path") << QStringLiteral("/usr/bin/gfxtune-run %command% -dx12") << QStringLiteral("%command% -dx12");
        QTest::newRow("extra spaces") << QStringLiteral("A=1 gfxtune-run   %command%") << QStringLiteral("A=1 %command%");
        QTest::newRow("not ours") << QStringLiteral("my-gfxtune-run %command%") << QStringLiteral("my-gfxtune-run %command%");
        QTest::newRow("absent") << QStringLiteral("gamemoderun %command%") << QStringLiteral("gamemoderun %command%");
    }

    void removesOnlyTheHelper()
    {
        QFETCH(QString, before);
        QFETCH(QString, after);
        QCOMPARE(withoutHelper(before), after);
        QVERIFY(!hasHelper(withoutHelper(before)));
    }

    void addingThenRemovingGivesTheLineBack()
    {
        for (const QString &line : {QString(), QStringLiteral("gamemoderun %command%"), QStringLiteral("A=1 B=\"x y\" %command% -arg")}) {
            QCOMPARE(withoutHelper(withHelper(line)), line);
        }
    }

    void findsWhatTheUserAlreadySet()
    {
        const OwnSettings own = ownSettings(QStringLiteral(
            "PROTON_FSR4_UPGRADE=1 RADV_TEX_ANISO=8 DXVK_CONFIG=\"dxgi.maxFrameRate = 60\" /usr/bin/gamescope -f -- %command% NOT_THIS=1"));
        QCOMPARE(own.environment, (QSet<QString>{QStringLiteral("PROTON_FSR4_UPGRADE"), QStringLiteral("RADV_TEX_ANISO"),
                                                 QStringLiteral("DXVK_CONFIG")}));
        QVERIFY(own.gamescope);

        const OwnSettings none = ownSettings(QStringLiteral("gamemoderun %command% -gamescope"));
        QVERIFY(none.environment.isEmpty());
        QVERIFY(!none.gamescope);
        QVERIFY(ownSettings({}).environment.isEmpty());
        // Damaged quoting must not crash or loop.
        ownSettings(QStringLiteral("A=\"unterminated %command%"));
        ownSettings(QStringLiteral("B=x\\"));
    }

    void plansNothingForDefaults()
    {
        const LaunchPlan plan = planLaunch(GameSettings{}, context());
        QVERIFY(plan.environment.isEmpty());
        QVERIFY(plan.wrapper.isEmpty());
        QVERIFY(plan.applied.isEmpty());
    }

    void plansDriverAndProtonSettings()
    {
        GameSettings s;
        s.anisotropicFiltering = 16;
        s.verticalSync = VerticalSync::Fast;
        s.frameCapEnabled = true;
        s.frameCap = 141;
        s.fsr4Upgrade = true;
        const LaunchPlan plan = planLaunch(s, context());
        QCOMPARE(envValue(plan, QStringLiteral("RADV_TEX_ANISO")), QStringLiteral("16"));
        QCOMPARE(envValue(plan, QStringLiteral("MESA_VK_WSI_PRESENT_MODE")), QStringLiteral("mailbox"));
        QCOMPARE(envValue(plan, QStringLiteral("VKD3D_FRAME_RATE")), QStringLiteral("141"));
        QCOMPARE(envValue(plan, QStringLiteral("PROTON_FSR4_UPGRADE")), QStringLiteral("1"));
        QCOMPARE(envValue(plan, QStringLiteral("DXVK_CONFIG")),
                 QStringLiteral("d3d11.samplerAnisotropy = 16; d3d9.samplerAnisotropy = 16; dxgi.maxFrameRate = 141; d3d9.maxFrameRate = 141"));
        QVERIFY(plan.wrapper.isEmpty());
        QCOMPARE(plan.applied.size(), 4);
    }

    void mapsEveryVerticalSyncMode()
    {
        const QList<std::pair<VerticalSync, QString>> modes{
            {VerticalSync::Off, QStringLiteral("immediate")},
            {VerticalSync::On, QStringLiteral("fifo")},
            {VerticalSync::Fast, QStringLiteral("mailbox")},
            {VerticalSync::Adaptive, QStringLiteral("relaxed")},
        };
        for (const auto &[mode, name] : modes) {
            GameSettings s;
            s.verticalSync = mode;
            QCOMPARE(envValue(planLaunch(s, context()), QStringLiteral("MESA_VK_WSI_PRESENT_MODE")), name);
        }
    }

    void leavesTheUsersOwnVariablesAlone()
    {
        GameSettings s;
        s.anisotropicFiltering = 4;
        s.frameCapEnabled = true;
        s.fsr4Upgrade = true;
        LaunchContext c = context();
        c.environment.insert(QStringLiteral("PROTON_FSR4_UPGRADE"), QStringLiteral("4.0.2"));
        c.environment.insert(QStringLiteral("DXVK_CONFIG"), QStringLiteral("dxgi.syncInterval = 0"));
        const LaunchPlan plan = planLaunch(s, c);
        QVERIFY(!setsEnv(plan, QStringLiteral("PROTON_FSR4_UPGRADE")));
        QVERIFY(!setsEnv(plan, QStringLiteral("DXVK_CONFIG")));
        QVERIFY(setsEnv(plan, QStringLiteral("RADV_TEX_ANISO")));
        QVERIFY(setsEnv(plan, QStringLiteral("VKD3D_FRAME_RATE")));
        QCOMPARE(plan.keptFromSteam, (QStringList{QStringLiteral("PROTON_FSR4_UPGRADE"), QStringLiteral("DXVK_CONFIG")}));
        QVERIFY(!plan.applied.contains(QStringLiteral("FSR 4 upgrade")));
        QVERIFY(plan.applied.contains(QStringLiteral("Frame rate cap 60"))); // through VKD3D-Proton still
    }

    void reportsACapThatNothingApplies()
    {
        GameSettings s;
        s.frameCapEnabled = true;
        LaunchContext c = context();
        c.environment.insert(QStringLiteral("DXVK_CONFIG"), QStringLiteral("x = 1"));
        c.environment.insert(QStringLiteral("VKD3D_FRAME_RATE"), QStringLiteral("30"));
        const LaunchPlan plan = planLaunch(s, c);
        QVERIFY(plan.applied.isEmpty());
        QCOMPARE(plan.keptFromSteam.size(), 2);
    }

    void wrapsTheGameInGamescope()
    {
        GameSettings s;
        s.gamescope = true;
        s.renderScale = 67;
        s.upscaleFilter = UpscaleFilter::Fsr;
        s.sharpness = 15;
        s.scalingMode = ScalingMode::Fit;
        s.frameCapEnabled = true;
        s.frameCap = 120;
        const LaunchPlan plan = planLaunch(s, context());
        QCOMPARE(plan.wrapper, (QStringList{QStringLiteral("gamescope"), QStringLiteral("-W"), QStringLiteral("2560"),
                                            QStringLiteral("-H"), QStringLiteral("1440"), QStringLiteral("-w"), QStringLiteral("1714"),
                                            QStringLiteral("-h"), QStringLiteral("964"), QStringLiteral("-r"), QStringLiteral("120"),
                                            QStringLiteral("-F"), QStringLiteral("fsr"), QStringLiteral("-S"), QStringLiteral("fit"),
                                            QStringLiteral("--sharpness"), QStringLiteral("5"), QStringLiteral("-f"), QStringLiteral("--")}));
        // Outside Proton, gamescope's screen is the cap; the in-game limiters stay off.
        QVERIFY(!setsEnv(plan, QStringLiteral("VKD3D_FRAME_RATE")));
        QVERIFY(!setsEnv(plan, QStringLiteral("DXVK_CONFIG")));
    }

    void capsProtonGamesInsideTheGameEvenInGamescope()
    {
        // gamescope's -r does not hold back a game with vertical sync off, so
        // under Proton the exact in-game limiters cap it and gamescope follows
        // the monitor.
        GameSettings s;
        s.gamescope = true;
        s.frameCapEnabled = true;
        s.frameCap = 60;
        LaunchContext c = context();
        c.proton = true;
        const LaunchPlan plan = planLaunch(s, c);
        QVERIFY(!plan.wrapper.isEmpty());
        QVERIFY(!plan.wrapper.contains(QStringLiteral("-r")));
        QVERIFY(setsEnv(plan, QStringLiteral("VKD3D_FRAME_RATE")));
        QVERIFY(setsEnv(plan, QStringLiteral("DXVK_CONFIG")));
        QVERIFY(plan.applied.contains(QStringLiteral("Frame rate cap 60")));

        // Without a cap, gamescope is never given a refresh rate either.
        s.frameCapEnabled = false;
        QVERIFY(!planLaunch(s, c).wrapper.contains(QStringLiteral("-r")));
    }

    void recognisesProtonCommands_data()
    {
        QTest::addColumn<QStringList>("command");
        QTest::addColumn<bool>("proton");
        const QString game = QStringLiteral("/games/Raft/Raft.exe");
        QTest::newRow("Proton") << QStringList{QStringLiteral("/steam/steamapps/common/Proton - Experimental/proton"), QStringLiteral("waitforexitandrun"), game} << true;
        QTest::newRow("behind the runtime")
            << QStringList{QStringLiteral("/steam/steamapps/common/SteamLinuxRuntime_sniper/_v2-entry-point"), QStringLiteral("--verb=waitforexitandrun"),
                           QStringLiteral("--"), QStringLiteral("/usr/share/steam/compatibilitytools.d/proton-cachyos/proton"), QStringLiteral("waitforexitandrun"), game}
            << true;
        QTest::newRow("native game") << QStringList{QStringLiteral("/games/Stardew Valley/StardewValley")} << false;
        QTest::newRow("native, runtime only")
            << QStringList{QStringLiteral("/steam/SteamLinuxRuntime_sniper/_v2-entry-point"), QStringLiteral("--verb=waitforexitandrun"), QStringLiteral("--"), QStringLiteral("/games/cs2/cs2.sh")}
            << false;
        QTest::newRow("a program named proton, no verb") << QStringList{QStringLiteral("/games/proton"), QStringLiteral("-fullscreen")} << false;
        QTest::newRow("proton only in a folder name") << QStringList{QStringLiteral("/games/proton/run"), QStringLiteral("run")} << false;
        QTest::newRow("verb before proton") << QStringList{QStringLiteral("waitforexitandrun"), QStringLiteral("/x/proton")} << false;
        QTest::newRow("nothing") << QStringList{} << false;
    }

    void recognisesProtonCommands()
    {
        QFETCH(QStringList, command);
        QFETCH(bool, proton);
        QCOMPARE(runsThroughProton(command), proton);
    }

    void leavesSharpnessOutForOtherFilters()
    {
        GameSettings s;
        s.gamescope = true;
        s.upscaleFilter = UpscaleFilter::Nearest;
        const LaunchPlan plan = planLaunch(s, context());
        QVERIFY(!plan.wrapper.contains(QStringLiteral("--sharpness")));
        QVERIFY(plan.wrapper.contains(QStringLiteral("nearest")));
        QVERIFY(!plan.wrapper.contains(QStringLiteral("-r")));
        // Native resolution: drawn at the screen size.
        QCOMPARE(plan.wrapper.at(plan.wrapper.indexOf(QStringLiteral("-w")) + 1), QStringLiteral("2560"));
    }

    void skipsGamescopeWhenItCannotRun_data()
    {
        QTest::addColumn<bool>("installed");
        QTest::addColumn<bool>("screenKnown");
        QTest::addColumn<bool>("insideGamescope");
        QTest::newRow("not installed") << false << true << false;
        QTest::newRow("screen unknown") << true << false << false;
        QTest::newRow("already inside gamescope") << true << true << true;
    }

    void skipsGamescopeWhenItCannotRun()
    {
        QFETCH(bool, installed);
        QFETCH(bool, screenKnown);
        QFETCH(bool, insideGamescope);
        GameSettings s;
        s.gamescope = true;
        s.frameCapEnabled = true;
        LaunchContext c = context(installed, screenKnown ? std::optional<Screen>(Screen{1920, 1080, 60}) : std::nullopt);
        if (insideGamescope) {
            c.environment.insert(QStringLiteral("GAMESCOPE_WAYLAND_DISPLAY"), QStringLiteral("gamescope-0"));
        }
        const LaunchPlan plan = planLaunch(s, c);
        QVERIFY(plan.wrapper.isEmpty());
        QVERIFY(!plan.notApplied.isEmpty() || plan.keptFromSteam.contains(QStringLiteral("gamescope")));
        // The cap falls back to the in-game limiters.
        QVERIFY(setsEnv(plan, QStringLiteral("VKD3D_FRAME_RATE")));
    }

    void keepsRenderSizesSane()
    {
        GameSettings s;
        s.gamescope = true;
        s.renderScale = 33;
        const LaunchPlan tiny = planLaunch(s, context(true, Screen{800, 480, 60}));
        QCOMPARE(tiny.wrapper.at(tiny.wrapper.indexOf(QStringLiteral("-w")) + 1), QStringLiteral("320"));
        QCOMPARE(tiny.wrapper.at(tiny.wrapper.indexOf(QStringLiteral("-h")) + 1), QStringLiteral("200"));
        s.renderScale = 59;
        const LaunchPlan odd = planLaunch(s, context(true, Screen{1920, 1080, 60}));
        QCOMPARE(odd.wrapper.at(odd.wrapper.indexOf(QStringLiteral("-w")) + 1).toInt() % 2, 0);
        QCOMPARE(odd.wrapper.at(odd.wrapper.indexOf(QStringLiteral("-h")) + 1).toInt() % 2, 0);
    }

    void recordsTheLastLaunch()
    {
        QTemporaryDir dir;
        QVERIFY(!readLastLaunch(dir.path(), 7));
        LaunchPlan plan;
        plan.applied = {QStringLiteral("Anisotropic filtering 16×"), QStringLiteral("FSR 4 upgrade")};
        plan.keptFromSteam = {QStringLiteral("DXVK_CONFIG")};
        plan.notApplied = {QStringLiteral("gamescope: not installed")};
        const QDateTime when = QDateTime(QDate(2026, 10, 21), QTime(18, 3), QTimeZone::UTC);
        QVERIFY(writeLastLaunch(dir.path(), 7, plan, when));
        const auto read = readLastLaunch(dir.path(), 7);
        QVERIFY(read);
        QCOMPARE(read->time, when);
        QCOMPARE(read->applied, plan.applied);
        QCOMPARE(read->keptFromSteam, plan.keptFromSteam);
        QCOMPARE(read->notApplied, plan.notApplied);

        QVERIFY(test::writeFile(lastLaunchPath(dir.path(), 8), "[launch]\ntime=yesterday\n"));
        QVERIFY(!readLastLaunch(dir.path(), 8));
    }

    void detectsTheUsersSteam()
    {
        QTemporaryDir proc;
        const uint me = uint(getuid());
        auto process = [&](const char *pid, const char *comm, uint uid) {
            test::writeFile(proc.filePath(QString::fromLatin1(pid) + QStringLiteral("/comm")), QByteArray(comm) + '\n');
            test::writeFile(proc.filePath(QString::fromLatin1(pid) + QStringLiteral("/status")),
                            "Name:\t" + QByteArray(comm) + "\nUid:\t" + QByteArray::number(uid) + "\t" + QByteArray::number(uid) + "\t0\t0\n");
        };
        process("10", "bash", me);
        process("11", "steamwebhelper", me);
        QVERIFY(!steam::isSteamRunning(proc.path(), me));
        process("12", "steam", me + 1); // someone else's Steam
        QVERIFY(!steam::isSteamRunning(proc.path(), me));
        process("13", "steam", me);
        QVERIFY(steam::isSteamRunning(proc.path(), me));
        QVERIFY(!steam::isSteamRunning(QStringLiteral("/does/not/exist"), me));
    }

    void plansLineChanges()
    {
        const QHash<quint32, QString> current{
            {1, QStringLiteral("gamemoderun %command%")},
            {2, QStringLiteral("gfxtune-run %command%")},
            {3, QStringLiteral("gamemoderun gfxtune-run %command% -x")},
            {4, QStringLiteral("-novid")},
        };
        const QList<LineChange> changes = planLineChanges(current, {1, 3, 9});
        QCOMPARE(changes.size(), 3);
        QCOMPARE(changes[0].appId, 1u);
        QCOMPARE(changes[0].after, QStringLiteral("gamemoderun gfxtune-run %command%"));
        QCOMPARE(changes[1].appId, 2u);
        QCOMPARE(changes[1].after, QString());
        QCOMPARE(changes[2].appId, 9u);
        QCOMPARE(changes[2].after, QStringLiteral("gfxtune-run %command%"));
        QVERIFY(planLineChanges(current, {3}).size() == 1); // only 2 loses it
    }

    void syncsSteamsFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("localconfig.vdf"));
        const QByteArray original =
            "\"UserLocalConfigStore\"\n{\n\t\"friends\"\n\t{\n\t\t\"12345\"\n\t\t{\n\t\t\t\"name\"\t\t\"someone\"\n\t\t}\n\t}\n"
            "\t\"Software\"\n\t{\n\t\t\"Valve\"\n\t\t{\n\t\t\t\"Steam\"\n\t\t\t{\n\t\t\t\t\"apps\"\n\t\t\t\t{\n"
            "\t\t\t\t\t\"1001\"\n\t\t\t\t\t{\n\t\t\t\t\t\t\"LastPlayed\"\t\t\"1700000000\"\n"
            "\t\t\t\t\t\t\"LaunchOptions\"\t\t\"gamemoderun %command%\"\n\t\t\t\t\t}\n"
            "\t\t\t\t\t\"1002\"\n\t\t\t\t\t{\n\t\t\t\t\t\t\"LastPlayed\"\t\t\"1700000001\"\n\t\t\t\t\t}\n"
            "\t\t\t\t}\n\t\t\t}\n\t\t}\n\t}\n}\n";
        QVERIFY(test::writeFile(path, original));
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);

        QList<LineChange> done;
        QString error;
        QVERIFY2(syncLaunchLines(path, {1001, 1002, 1003}, &done, &error), qPrintable(error));
        QCOMPARE(done.size(), 3);
        QHash<quint32, QString> now;
        QVERIFY(readLaunchOptions(path, now));
        QCOMPARE(now.value(1001), QStringLiteral("gamemoderun gfxtune-run %command%"));
        QCOMPARE(now.value(1002), QStringLiteral("gfxtune-run %command%"));
        QCOMPARE(now.value(1003), QStringLiteral("gfxtune-run %command%"));
        QCOMPARE(QFile::permissions(path) & test::FileModeBits, QFileDevice::ReadOwner | QFileDevice::WriteOwner);

        // Everything else in the file is untouched, friends included.
        vdf::Node tree;
        QVERIFY(vdf::readTextFile(path, tree));
        QCOMPARE(tree.find({u"UserLocalConfigStore", u"friends", u"12345", u"name"})->value, QStringLiteral("someone"));

        // Taking it out again gives the original file back, byte for byte.
        QVERIFY2(syncLaunchLines(path, {}, &done, &error), qPrintable(error));
        QCOMPARE(done.size(), 3);
        QFile back(path);
        QVERIFY(back.open(QIODevice::ReadOnly));
        QByteArray restored = back.readAll();
        // 1003 did not exist before; its emptied section stays, empty.
        restored.replace("\t\t\t\t\t\"1003\"\n\t\t\t\t\t{\n\t\t\t\t\t}\n", "");
        QCOMPARE(restored, original);
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files), QStringList{QStringLiteral("localconfig.vdf")});
    }

    void refusesToSyncADamagedFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("localconfig.vdf"));
        const QByteArray damaged = "\"UserLocalConfigStore\" { \"Software\" { ";
        QVERIFY(test::writeFile(path, damaged));
        QString error;
        QVERIFY(!syncLaunchLines(path, {1}, nullptr, &error));
        QVERIFY(!error.isEmpty());
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), damaged);
    }

    void handlesANewAccountWithoutAFile()
    {
        QTemporaryDir dir;
        QString error;
        const QString path = dir.filePath(QStringLiteral("missing.vdf"));
        QVERIFY(syncLaunchLines(path, {}, nullptr, &error)); // nothing to do
        QVERIFY(!syncLaunchLines(path, {1}, nullptr, &error)); // cannot create Steam's file
        QVERIFY(!QFile::exists(path));
    }
};

QTEST_GUILESS_MAIN(LaunchTest)
#include "tst_launch.moc"
