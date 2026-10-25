// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "GameLibrary.h"
#include "GameSettingsEditor.h"
#include "launch/LastLaunch.h"
#include "launch/LaunchLine.h"
#include "support/FakeSteam.h"
#include "support/Fixtures.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

using namespace gfxtune;
using settings::GameSettings;
using settings::SettingsStore;
using settings::SystemInfo;

namespace
{

QString s(const char *text)
{
    return QString::fromUtf8(text);
}

int rowOf(GameLibrary &library, quint32 appId)
{
    return library.filtered()->rowOf(int(appId));
}

QVariant role(GameLibrary &library, quint32 appId, int role)
{
    const int row = rowOf(library, appId);
    return row < 0 ? QVariant() : library.filtered()->index(row, 0).data(role);
}

// A Steam folder, gfxtune's settings folder and an empty /proc, all made up.
struct Setup {
    test::FakeSteam steam;
    QTemporaryDir config;
    QTemporaryDir proc;
    SettingsStore store{config.filePath(s("gfxtune"))};

    Setup() { steam.build(); }
};

bool waitUntilIdle(GameLibrary &library)
{
    return QTest::qWaitFor([&] { return library.loaded() && !library.scanning(); }, 10000);
}

} // namespace

class LibraryTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void listsGamesThenFillsInDetails()
    {
        Setup setup;
        GameLibrary library(setup.steam.options(), &setup.store, nullptr, setup.proc.path());
        QSignalSpy state(&library, &GameLibrary::stateChanged);
        QVERIFY(!library.loaded());
        library.start();
        QVERIFY(waitUntilIdle(library));
        QVERIFY(library.steamFound());
        QCOMPARE(library.gameCount(), 7);
        // Filled in by the second, slower pass.
        QCOMPARE(library.game(1001)->apis, steam::GraphicsApis(steam::GraphicsApi::DirectX12));
        QVERIFY(state.count() >= 2);
    }

    void picksUpInstallsAndRemovals()
    {
        Setup setup;
        GameLibrary library(setup.steam.options(), &setup.store, nullptr, setup.proc.path());
        library.start();
        QVERIFY(waitUntilIdle(library));

        setup.steam.game(setup.steam.root, 1100, s("Brand New"), s("Brand New"));
        QTRY_VERIFY_WITH_TIMEOUT(library.game(1100), 10000);

        QVERIFY(QFile::remove(setup.steam.root + s("/steamapps/appmanifest_1100.acf")));
        QTRY_VERIFY_WITH_TIMEOUT(!library.game(1100), 10000);
    }

    void reportsRunningGames()
    {
        Setup setup;
        GameLibrary library(setup.steam.options(), &setup.store, nullptr, setup.proc.path());
        library.start();
        QVERIFY(waitUntilIdle(library));
        QCOMPARE(role(library, 1003, GameListModel::RunningRole).toBool(), false);

        const QString pid = setup.proc.filePath(s("4242"));
        QVERIFY(test::writeFile(pid + s("/comm"), "reaper\n"));
        QVERIFY(test::writeFile(pid + s("/cmdline"), QByteArray("reaper\0SteamLaunch\0AppId=1003\0--\0./stonewake\0", 45)));
        QTRY_VERIFY_WITH_TIMEOUT(role(library, 1003, GameListModel::RunningRole).toBool(), 8000);
    }

    void handlesMissingSteam()
    {
        QTemporaryDir home;
        QTemporaryDir config;
        SettingsStore store(config.filePath(s("gfxtune")));
        steam::ScanOptions options;
        options.homePath = home.path();
        GameLibrary library(options, &store);
        library.start();
        QVERIFY(waitUntilIdle(library));
        QVERIFY(!library.steamFound());
        QCOMPARE(library.gameCount(), 0);
    }

    void editorSavesAfterAPauseAndOnSwitch()
    {
        Setup setup;
        GameLibrary library(setup.steam.options(), &setup.store, nullptr, setup.proc.path());
        GameSettingsEditor editor(&library, &setup.store, SystemInfo{true}, nullptr, 100);
        library.start();
        QVERIFY(waitUntilIdle(library));

        editor.setAppId(1001);
        QVERIFY(editor.hasGame());
        QCOMPARE(editor.gameName(), s("Ashfall Protocol"));
        QCOMPARE(editor.runsAs(), s("Runs through GE-Proton10-26"));
        QCOMPARE(editor.graphics(), s("DirectX 12"));
        QVERIFY(!editor.customized());

        editor.setAnisotropicFiltering(16);
        editor.setFrameCap(144);
        QVERIFY(editor.customized());
        QVERIFY(!QFile::exists(setup.store.pathFor(1001))); // not yet: waits for a pause
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(setup.store.pathFor(1001)), 3000);
        QTRY_VERIFY(role(library, 1001, GameListModel::CustomizedRole).toBool());

        // Switching games saves the pending change at once.
        editor.setVerticalSync(int(settings::VerticalSync::Off));
        editor.setAppId(1002);
        QCOMPARE(setup.store.load(1001).verticalSync, settings::VerticalSync::Off);
        QCOMPARE(editor.anisotropicFiltering(), 0); // 1002 has its own settings

        editor.setAppId(1001);
        QCOMPARE(editor.anisotropicFiltering(), 16);
        QCOMPARE(editor.frameCap(), 144);
    }

    void editorRejectsValuesThatAreNotOffered()
    {
        Setup setup;
        GameLibrary library(setup.steam.options(), &setup.store, nullptr, setup.proc.path());
        GameSettingsEditor editor(&library, &setup.store, SystemInfo{true}, nullptr, 50);
        library.start();
        QVERIFY(waitUntilIdle(library));
        editor.setAppId(1001);

        editor.setAnisotropicFiltering(3);
        editor.setRenderScale(66);
        editor.setVerticalSync(99);
        editor.setUpscaleFilter(-1);
        editor.setScalingMode(17);
        QVERIFY(!editor.customized());
        editor.setFrameCap(5000);
        QCOMPARE(editor.frameCap(), GameSettings::MaxFrameCap);
        editor.setSharpness(-4);
        QCOMPARE(editor.sharpness(), 0);
        editor.setAppId(-5); // ignored
        QCOMPARE(editor.appId(), 1001);
    }

    void editorUpdatesWhatApplies()
    {
        Setup setup;
        GameLibrary library(setup.steam.options(), &setup.store, nullptr, setup.proc.path());
        GameSettingsEditor editor(&library, &setup.store, SystemInfo{true}, nullptr, 50);
        library.start();
        QVERIFY(waitUntilIdle(library));

        editor.setAppId(1001); // DirectX 12 with FSR 3.1: capped by VKD3D-Proton
        QVERIFY(editor.frameCapAvailable());
        QVERIFY(editor.fsr4Available());

        editor.setAppId(1003); // native Vulkan: only gamescope can cap it
        QVERIFY(!editor.frameCapAvailable());
        QVERIFY(!editor.fsr4Available());
        QVERIFY(!editor.fsr4Note().isEmpty());
        QSignalSpy availability(&editor, &GameSettingsEditor::availabilityChanged);
        editor.setGamescope(true);
        QVERIFY(availability.count() >= 1);
        QVERIFY(editor.frameCapAvailable());
        QVERIFY(!editor.frameCapNote().isEmpty());
    }

    void resetAndRemoveAll()
    {
        Setup setup;
        GameLibrary library(setup.steam.options(), &setup.store, nullptr, setup.proc.path());
        GameSettingsEditor editor(&library, &setup.store, SystemInfo{true}, nullptr, 50);
        library.start();
        QVERIFY(waitUntilIdle(library));

        editor.setAppId(1001);
        editor.setFsr4(int(settings::Fsr4::On));
        editor.flush();
        QVERIFY(QFile::exists(setup.store.pathFor(1001)));
        editor.resetToDefaults();
        QVERIFY(!QFile::exists(setup.store.pathFor(1001)));
        QVERIFY(!editor.customized());

        editor.setAppId(1002);
        editor.setGamescope(true);
        editor.flush();
        editor.setAppId(1004);
        editor.setFrameCapEnabled(true); // still waiting to be saved
        QVERIFY(editor.removeAllSettings());
        QVERIFY(!QFileInfo::exists(setup.store.baseDir()));
        QTest::qWait(200); // the pending save must not bring a file back
        QVERIFY(!QFileInfo::exists(setup.store.baseDir()));
        QVERIFY(!editor.frameCapEnabled());
        QVERIFY(!role(library, 1002, GameListModel::CustomizedRole).toBool());
    }

    void reportsDamagedSettingsFiles()
    {
        Setup setup;
        QVERIFY(test::writeFile(setup.store.pathFor(1001), "[frames]\nframe_cap=-3\n"));
        GameLibrary library(setup.steam.options(), &setup.store, nullptr, setup.proc.path());
        GameSettingsEditor editor(&library, &setup.store, SystemInfo{true}, nullptr, 50);
        library.start();
        QVERIFY(waitUntilIdle(library));
        editor.setAppId(1001);
        QVERIFY(!editor.problem().isEmpty());
        editor.setAppId(1002);
        QVERIFY(editor.problem().isEmpty());
    }

    void addsAndRemovesItsLaunchLine()
    {
        Setup setup;
        GameLibrary library(setup.steam.options(), &setup.store, nullptr, setup.proc.path());
        library.setPollInterval(100);
        GameSettingsEditor editor(&library, &setup.store, SystemInfo{true}, nullptr, 50);
        library.start();
        QVERIFY(waitUntilIdle(library));
        using State = GameLibrary::LaunchState;

        // Steam closed: the line goes in at once, after the user's own options.
        editor.setAppId(1001);
        QCOMPARE(editor.launchState(), int(State::NotNeeded));
        editor.setAnisotropicFiltering(16);
        editor.flush();
        QTRY_COMPARE_WITH_TIMEOUT(editor.launchState(), int(State::Added), 8000);
        QCOMPARE(library.game(1001)->launchOptions, s("gamemoderun gfxtune-run %command% -dx12 \"quoted arg\""));

        // Steam opens: a reset waits, and the line stays for now.
        const QString pid = setup.proc.filePath(s("777"));
        QVERIFY(test::writeFile(pid + s("/comm"), "steam\n"));
        QVERIFY(test::writeFile(pid + s("/status"), "Uid:\t" + QByteArray::number(getuid()) + "\t0\t0\t0\n"));
        QTRY_VERIFY_WITH_TIMEOUT(library.steamRunning(), 5000);
        editor.resetToDefaults();
        QCOMPARE(editor.launchState(), int(State::WaitingToRemove));
        QTest::qWait(500);
        QVERIFY(launch::hasHelper(library.game(1001)->launchOptions));

        // Steam closes: the line comes out and the user's options are back.
        QVERIFY(QDir(pid).removeRecursively());
        QTRY_COMPARE_WITH_TIMEOUT(editor.launchState(), int(State::NotNeeded), 8000);
        QCOMPARE(library.game(1001)->launchOptions, s("gamemoderun %command% -dx12 \"quoted arg\""));
    }

    void offersTheLineToCopyWhileSteamIsOpen()
    {
        Setup setup;
        const QString pid = setup.proc.filePath(s("778"));
        QVERIFY(test::writeFile(pid + s("/comm"), "steam\n"));
        QVERIFY(test::writeFile(pid + s("/status"), "Uid:\t" + QByteArray::number(getuid()) + "\t0\t0\t0\n"));
        GameLibrary library(setup.steam.options(), &setup.store, nullptr, setup.proc.path());
        GameSettingsEditor editor(&library, &setup.store, SystemInfo{true}, nullptr, 50);
        library.start();
        QVERIFY(waitUntilIdle(library));
        QVERIFY(library.steamRunning());

        editor.setAppId(1004);
        editor.setFsr4(int(settings::Fsr4::On));
        editor.flush();
        QCOMPARE(editor.launchState(), int(GameLibrary::LaunchState::WaitingForSteam));
        QCOMPARE(editor.launchLine(), s("gfxtune-run %command%"));
        QTest::qWait(300);
        QVERIFY(library.game(1004)->launchOptions.isEmpty()); // Steam's file untouched
    }

    void showsTheLastLaunch()
    {
        Setup setup;
        GameLibrary library(setup.steam.options(), &setup.store, nullptr, setup.proc.path());
        library.setPollInterval(100);
        GameSettingsEditor editor(&library, &setup.store, SystemInfo{true}, nullptr, 50);
        library.start();
        QVERIFY(waitUntilIdle(library));
        editor.setAppId(1003);
        QVERIFY(!editor.hasLastLaunch());

        // The helper writes its report when the game starts.
        launch::LaunchPlan plan;
        plan.applied = {s("Anisotropic filtering 8×")};
        QVERIFY(launch::writeLastLaunch(setup.store.baseDir(), 1003, plan, QDateTime::currentDateTime()));
        QVERIFY(test::writeFile(setup.proc.filePath(s("4243/comm")), "reaper\n"));
        QVERIFY(test::writeFile(setup.proc.filePath(s("4243/cmdline")), QByteArray("reaper\0SteamLaunch\0AppId=1003\0", 30)));
        QTRY_VERIFY_WITH_TIMEOUT(editor.hasLastLaunch(), 5000);
        QCOMPARE(editor.lastLaunchApplied(), s("Anisotropic filtering 8×"));
        QVERIFY(!editor.lastLaunchTime().isEmpty());
    }

    void reportsSaveFailures()
    {
        Setup setup;
        GameLibrary library(setup.steam.options(), &setup.store, nullptr, setup.proc.path());
        GameSettingsEditor editor(&library, &setup.store, SystemInfo{true}, nullptr, 50);
        library.start();
        QVERIFY(waitUntilIdle(library));
        QVERIFY(test::writeFile(setup.store.baseDir() + s("/games"), "a file where a folder belongs"));
        editor.setAppId(1001);
        editor.setFsr4(int(settings::Fsr4::On));
        editor.flush();
        QVERIFY(editor.problem().startsWith(s("Could not save")));
    }

    void locksFiltersInGamesWithAntiCheat()
    {
        Setup setup;
        GameLibrary library(setup.steam.options(), &setup.store, nullptr, setup.proc.path());
        GameSettingsEditor editor(&library, &setup.store, SystemInfo{true}, nullptr, 50);
        library.start();
        QVERIFY(waitUntilIdle(library));

        editor.setAppId(1004); // ships Easy Anti-Cheat
        QVERIFY(editor.filtersAvailable());
        QVERIFY(editor.filtersLocked());
        QVERIFY(editor.filtersWarning().contains(s("Easy Anti-Cheat")));
        editor.setFilterSharpening(50);
        editor.setFiltersEnabled(false);
        QCOMPARE(editor.filterSharpening(), 0); // refused while locked
        QVERIFY(editor.filtersEnabled());

        editor.acceptFiltersRisk();
        QVERIFY(!editor.filtersLocked());
        QVERIFY(setup.store.load(1004).filtersRiskAccepted); // saved at once
        QVERIFY(!editor.customized()); // no launch line just for that
        editor.setFilterSharpening(50);
        editor.setFilterTemperature(250);
        QCOMPARE(editor.filterSharpening(), 50);
        QCOMPARE(editor.filterTemperature(), 100); // kept in range
        QVERIFY(editor.customized());

        editor.setFilterContrast(10);
        QVERIFY(!editor.filterColoursNeutral());
        editor.resetFilterColours();
        QVERIFY(editor.filterColoursNeutral());
        QCOMPARE(editor.filterSharpening(), 50); // sharpening is not a colour

        // Valve Anti-Cheat comes from Steam's data, before any file is read.
        editor.setAppId(1005);
        QVERIFY(editor.filtersLocked());
    }

    void switchesFiltersOffWhenAGameGainsAntiCheat()
    {
        Setup setup;
        // Filters set while the game had no anti-cheat; an update added it.
        GameSettings before;
        before.filters.sharpening = 60;
        QVERIFY(setup.store.save(1004, before, s("Hollow Orbit")));

        GameLibrary library(setup.steam.options(), &setup.store, nullptr, setup.proc.path());
        GameSettingsEditor editor(&library, &setup.store, SystemInfo{true}, nullptr, 50);
        editor.setAppId(1004);
        QSignalSpy rewritten(&library, &GameLibrary::settingsRewritten);
        library.start();
        QVERIFY(waitUntilIdle(library));

        QCOMPARE(rewritten.count(), 1);
        const GameSettings after = setup.store.load(1004);
        QVERIFY(!after.filters.enabled);
        QCOMPARE(after.filters.sharpening, 60); // the values stay
        QVERIFY(!editor.filtersEnabled());      // and the page shows it

        // Once accepted, the user's choice stands.
        editor.acceptFiltersRisk();
        editor.setFiltersEnabled(true);
        editor.flush();
        library.refresh();
        QVERIFY(waitUntilIdle(library));
        QCOMPARE(rewritten.count(), 1);
        QVERIFY(setup.store.load(1004).filters.enabled);
    }

    void reportsWhatTheFiltersDid()
    {
        Setup setup;
        GameLibrary library(setup.steam.options(), &setup.store, nullptr, setup.proc.path());
        library.setPollInterval(100);
        GameSettingsEditor editor(&library, &setup.store, SystemInfo{true}, nullptr, 50);
        library.start();
        QVERIFY(waitUntilIdle(library));

        const QDateTime started = QDateTime::currentDateTime().addSecs(-60);
        launch::LaunchPlan plan;
        plan.applied = {s("In-game filters")};
        QVERIFY(launch::writeLastLaunch(setup.store.baseDir(), 1003, plan, started));
        const auto writeReport = [&](qint64 time, const char *state, const char *detail) {
            return test::writeFile(launch::filterReportPath(setup.store.baseDir(), 1003),
                                   QByteArray("# from the layer\ntime=") + QByteArray::number(time) + "\nstate=" + state + "\ndetail=" + detail + "\n");
        };
        const auto reselect = [&] {
            editor.setAppId(1001);
            editor.setAppId(1003);
        };

        reselect();
        QVERIFY(editor.lastLaunchFilters().contains(s("never showed a Vulkan picture")));

        QVERIFY(writeReport(started.addSecs(5).toSecsSinceEpoch(), "running", "2560×1440, B8G8R8A8_SRGB"));
        reselect();
        QCOMPARE(editor.lastLaunchFilters(), s("Drawn in the game (2560×1440, B8G8R8A8_SRGB)"));

        QVERIFY(writeReport(started.addSecs(5).toSecsSinceEpoch(), "skipped", "the game shows HDR or a wide colour space"));
        reselect();
        QCOMPARE(editor.lastLaunchFilters(), s("Not drawn: the game shows HDR or a wide colour space"));

        // A report from an earlier launch does not count.
        QVERIFY(writeReport(started.addSecs(-3600).toSecsSinceEpoch(), "running", "old"));
        reselect();
        QVERIFY(editor.lastLaunchFilters().contains(s("never showed")));

        // While the game runs the report can still arrive; the page follows.
        QVERIFY(test::writeFile(setup.proc.filePath(s("4244/comm")), "reaper\n"));
        QVERIFY(test::writeFile(setup.proc.filePath(s("4244/cmdline")), QByteArray("reaper\0SteamLaunch\0AppId=1003\0", 30)));
        QTRY_COMPARE_WITH_TIMEOUT(editor.lastLaunchFilters(), s("Waiting for the game's first frame"), 5000);
        QVERIFY(writeReport(QDateTime::currentSecsSinceEpoch(), "running", "1920×1080, B8G8R8A8_UNORM"));
        QTRY_COMPARE_WITH_TIMEOUT(editor.lastLaunchFilters(), s("Drawn in the game (1920×1080, B8G8R8A8_UNORM)"), 5000);

        // Nothing to say when the filters were not part of the launch.
        plan.applied = {s("Anisotropic filtering 8×")};
        QVERIFY(launch::writeLastLaunch(setup.store.baseDir(), 1003, plan, QDateTime::currentDateTime()));
        reselect();
        QVERIFY(editor.lastLaunchFilters().isEmpty());
    }
};

QTEST_GUILESS_MAIN(LibraryTest)
#include "tst_library.moc"
