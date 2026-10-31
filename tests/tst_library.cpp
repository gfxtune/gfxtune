// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "GameLibrary.h"
#include "GameSettingsEditor.h"
#include "launch/LastLaunch.h"
#include "launch/LaunchLine.h"
#include "performance/Performance.h"
#include "shared/FrameHistogram.h"
#include "support/FakeSteam.h"
#include "support/Fixtures.h"

#include <QDir>
#include <QFile>
#include <QLocale>
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
        QTRY_COMPARE(role(library, 1001, GameListModel::SettingsNoteRole).toString(), s("2 changes"));

        // Switching games saves the pending change at once.
        editor.setVerticalSync(int(settings::VerticalSync::Off));
        editor.setAppId(1002);
        QCOMPARE(setup.store.load(1001).verticalSync, settings::VerticalSync::Off);
        QCOMPARE(editor.anisotropicFiltering(), 0); // 1002 did not change it

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
        editor.setLodBias(-30);
        QVERIFY(!editor.customized());
        editor.setAntiAliasing(7);
        QCOMPARE(editor.antiAliasing(), 0); // no such method
        editor.setRenderScale(150); // supersampling
        QCOMPARE(editor.renderScale(), 150);
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

        editor.setAppId(1003); // native Vulkan: gfxtune's layer caps it while it measures
        QVERIFY(editor.measurePerformance());
        QVERIFY(editor.frameCapAvailable());
        QVERIFY(!editor.fsr4Available());
        QVERIFY(!editor.fsr4Note().isEmpty());
        // Without the layer only gamescope can.
        editor.setMeasurePerformance(false);
        QVERIFY(!editor.frameCapAvailable());
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
        editor.selectGlobal();
        editor.setAnisotropicFiltering(8);
        editor.flush();
        QVERIFY(QFile::exists(setup.store.globalPath()));
        editor.setAppId(1004);
        editor.setFrameCapEnabled(true);
        QVERIFY(editor.removeAllSettings());
        QVERIFY(!QFileInfo::exists(setup.store.baseDir()));
        QTest::qWait(200); // the pending save must not bring a setting back
        QVERIFY(setup.store.loadProfile(1004).isEmpty());
        QCOMPARE(setup.store.loadGlobal(), GameSettings{});
        QVERIFY(!editor.frameCapEnabled());
        QCOMPARE(role(library, 1002, GameListModel::SettingsNoteRole).toString(), QString());
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

        // By default every game is measured: with Steam closed each gets the
        // line at once, after the user's own options.
        QTRY_COMPARE_WITH_TIMEOUT(library.game(1001)->launchOptions, s("gamemoderun gfxtune-run %command% -dx12 \"quoted arg\""), 8000);
        QVERIFY(launch::hasHelper(library.game(1003)->launchOptions));
        // Not where anti-cheat keeps measuring out and nothing else applies.
        QVERIFY(!launch::hasHelper(library.game(1004)->launchOptions));
        QVERIFY(!library.wantsLaunchLine(1004));
        editor.setAppId(1001);
        QCOMPARE(editor.launchState(), int(State::Added));

        // Measuring off for all games: nothing left to do, the lines come out.
        editor.selectGlobal();
        editor.setMeasurePerformance(false);
        editor.flush();
        QTRY_COMPARE_WITH_TIMEOUT(library.game(1001)->launchOptions, s("gamemoderun %command% -dx12 \"quoted arg\""), 8000);
        QVERIFY(!launch::hasHelper(library.game(1003)->launchOptions));

        // A setting of the game's own brings it back.
        editor.setAppId(1001);
        QCOMPARE(editor.launchState(), int(State::NotNeeded));
        editor.setAnisotropicFiltering(16);
        editor.flush();
        QTRY_COMPARE_WITH_TIMEOUT(editor.launchState(), int(State::Added), 8000);
        QVERIFY(!launch::hasHelper(library.game(1003)->launchOptions));

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

        // A global filter does not bring the line into a game with anti-cheat,
        // where it stays locked.
        editor.selectGlobal();
        editor.setFilterSharpening(40);
        editor.flush();
        QTRY_VERIFY_WITH_TIMEOUT(launch::hasHelper(library.game(1003)->launchOptions), 8000);
        QVERIFY(!library.wantsLaunchLine(1004));
        QVERIFY(!launch::hasHelper(library.game(1004)->launchOptions));
        editor.setFilterSharpening(0);

        // A setting applied from outside the game reaches every game, anti-cheat
        // or not, but not one that stopped using the global settings.
        editor.setFsr4(int(settings::Fsr4::On));
        editor.flush();
        editor.setAppId(1001);
        editor.setUsesGlobal(false);
        editor.flush();
        QTRY_VERIFY_WITH_TIMEOUT(launch::hasHelper(library.game(1004)->launchOptions), 8000);
        QTRY_VERIFY_WITH_TIMEOUT(!launch::hasHelper(library.game(1001)->launchOptions), 8000);
        QVERIFY(launch::hasHelper(library.game(1003)->launchOptions));
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
        QVERIFY(QDir().mkpath(setup.store.pathFor(1001))); // a folder where the game's file belongs
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

    // Filters set while the game had no anti-cheat, then an update added
    // it: they stay set but locked, and come back once the risk is accepted.
    void locksFiltersWhenAGameGainsAntiCheat()
    {
        Setup setup;
        GameSettings before;
        before.filters.sharpening = 60;
        QVERIFY(test::saveGame(setup.store, 1004, before, s("Hollow Orbit")));

        GameLibrary library(setup.steam.options(), &setup.store, nullptr, setup.proc.path());
        GameSettingsEditor editor(&library, &setup.store, SystemInfo{true}, nullptr, 50);
        editor.setAppId(1004);
        library.start();
        QVERIFY(waitUntilIdle(library));

        // gfxtune-run learns of it from the game's file.
        QCOMPARE(setup.store.loadFacts(1004).antiCheat, QStringList{s("Easy Anti-Cheat")});
        QVERIFY(!settings::layerAllowed(setup.store.load(1004), setup.store.loadFacts(1004)));
        QVERIFY(editor.filtersLocked());
        QCOMPARE(editor.filterSharpening(), 60); // the values stay

        editor.acceptFiltersRisk();
        QVERIFY(!editor.filtersLocked());
        QVERIFY(settings::layerAllowed(setup.store.load(1004), setup.store.loadFacts(1004)));
        QCOMPARE(setup.store.load(1004).filters.sharpening, 60);
    }

    // The global page edits the settings for all games; a game's page edits
    // its own changes over them.
    void editorEditsGlobalSettingsAndAGamesChanges()
    {
        Setup setup;
        GameLibrary library(setup.steam.options(), &setup.store, nullptr, setup.proc.path());
        GameSettingsEditor editor(&library, &setup.store, SystemInfo{true}, nullptr, 50);
        library.start();
        QVERIFY(waitUntilIdle(library));
        const int games = library.gameCount();
        QCOMPARE(library.gamesUsingGlobal(), games);

        QSignalSpy pages(&editor, &GameSettingsEditor::gameChanged);
        editor.selectGlobal();
        QVERIFY(editor.isGlobal());
        QVERIFY(!editor.hasGame());
        QCOMPARE(pages.count(), 1);
        QVERIFY(editor.filtersAvailable());
        QVERIFY(!editor.filtersLocked());
        QVERIFY(editor.changedSettings().isEmpty());
        QVERIFY(!editor.customized());
        editor.setAnisotropicFiltering(16);
        editor.setFrameCapEnabled(true);
        editor.setFilterSaturation(20);
        QVERIFY(editor.customized());
        editor.acceptFiltersRisk(); // a game's own decision: nothing here
        editor.flush();
        GameSettings global = setup.store.loadGlobal();
        QCOMPARE(global.anisotropicFiltering, 16);
        QVERIFY(global.frameCapEnabled);
        QVERIFY(!global.filtersRiskAccepted);

        // A game follows them until it changes one.
        editor.setAppId(1003);
        QVERIFY(!editor.isGlobal());
        QCOMPARE(editor.anisotropicFiltering(), 16);
        QCOMPARE(editor.filterSaturation(), 20);
        QVERIFY(editor.changedSettings().isEmpty());
        QCOMPARE(editor.baseName(), s("Global"));
        QCOMPARE(editor.baseValues().value(s("anisotropicFiltering")).toInt(), 16);
        QCOMPARE(editor.baseValues().size(), 23);
        editor.setAnisotropicFiltering(4);
        editor.setFrameCap(90);
        QCOMPARE(editor.changedSettings(), (QStringList{s("anisotropicFiltering"), s("frameCap")}));
        // Back to the global value by hand: no longer a change.
        editor.setAnisotropicFiltering(16);
        QCOMPARE(editor.changedSettings(), QStringList{s("frameCap")});
        editor.setAnisotropicFiltering(4);
        editor.useBase(s("anisotropicFiltering"));
        QCOMPARE(editor.anisotropicFiltering(), 16);
        editor.useBase(s("nothing")); // ignored
        editor.flush();
        QCOMPARE(setup.store.loadProfile(1003).changed, QSet<QString>{s("frameCap")});
        QCOMPARE(role(library, 1003, GameListModel::SettingsNoteRole).toString(), s("1 change"));

        // Later global changes reach it, except what it changed.
        editor.selectGlobal();
        editor.setAnisotropicFiltering(8);
        editor.setFrameCap(144);
        editor.flush();
        editor.setAppId(1003);
        QCOMPARE(editor.anisotropicFiltering(), 8);
        QCOMPARE(editor.frameCap(), 90);

        // Out of the global settings: left alone, apart from its own changes.
        editor.setUsesGlobal(false);
        QVERIFY(!editor.usesGlobal());
        QCOMPARE(editor.anisotropicFiltering(), 0);
        QCOMPARE(editor.frameCap(), 90);
        QVERIFY(!editor.measurePerformance());
        QCOMPARE(editor.baseName(), s("Without gfxtune"));
        QCOMPARE(editor.baseValues().value(s("anisotropicFiltering")).toInt(), 0);
        editor.flush();
        QCOMPARE(library.gamesUsingGlobal(), games - 1);
        QCOMPARE(role(library, 1003, GameListModel::SettingsNoteRole).toString(), s("own settings only"));
        QCOMPARE(setup.store.load(1003).anisotropicFiltering, 0);
        editor.setUsesGlobal(true);
        editor.flush();
        QCOMPARE(library.gamesUsingGlobal(), games);

        // "Use Global Settings for Everything" drops the game's changes.
        editor.resetToDefaults();
        QVERIFY(editor.changedSettings().isEmpty());
        QCOMPARE(editor.frameCap(), 144);
        QVERIFY(!QFile::exists(setup.store.pathFor(1003)));

        // The global page's reset goes back to gfxtune's defaults.
        editor.selectGlobal();
        editor.resetToDefaults();
        QVERIFY(!QFile::exists(setup.store.globalPath()));
        QCOMPARE(editor.anisotropicFiltering(), 0);
        const qsizetype before = pages.count();
        editor.selectGlobal(); // already there: nothing happens
        QCOMPARE(pages.count(), before);
    }

    void reportsDamagedGlobalSettings()
    {
        Setup setup;
        QVERIFY(test::writeFile(setup.store.globalPath(), "[frames]\nframe_cap=-3\n"));
        GameLibrary library(setup.steam.options(), &setup.store, nullptr, setup.proc.path());
        GameSettingsEditor editor(&library, &setup.store, SystemInfo{true}, nullptr, 50);
        library.start();
        QVERIFY(waitUntilIdle(library));
        editor.selectGlobal();
        QVERIFY(!editor.problem().isEmpty());
        QCOMPARE(editor.frameCap(), GameSettings{}.frameCap);
    }

    void editorListsMeasuredSessions()
    {
        Setup setup;
        const QString dir = performance::performanceDir(setup.store.baseDir(), 1003);
        // Two finished sessions, one without the card's readings...
        QVERIFY(test::writeFile(dir + s("/history"),
                                "1789990000,3900.0,468000,120.00,95.40,80.60,60.4,74.6,88.0,210.4,97.0\n"
                                "1789900000,600.0,36000,60.00,50.00,45.00,0.0,0.0,0.0,0.0,0.0\n"));
        // ...and the game running now as process 4300.
        shared::FrameHistogram histogram;
        for (int i = 0; i < 1500 * 144; ++i) {
            histogram.add(1'000'000'000 / 144);
        }
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        QVERIFY(test::writeFile(dir + s("/session-4300"), "start=" + QByteArray::number(now - 1500) + "\nupdated=" + QByteArray::number(now)
                                                              + "\nhistogram=" + QByteArray::fromStdString(histogram.serialize()) + "\n"));
        QVERIFY(QDir().mkpath(setup.proc.filePath(s("4300"))));

        GameLibrary library(setup.steam.options(), &setup.store, nullptr, setup.proc.path());
        GameSettingsEditor editor(&library, &setup.store, SystemInfo{true}, nullptr, 50);
        library.start();
        QVERIFY(waitUntilIdle(library));
        QSignalSpy changed(&editor, &GameSettingsEditor::sessionsChanged);
        editor.setAppId(1003);
        QVERIFY(changed.count() >= 1);

        const QVariantList rows = editor.sessions();
        QCOMPARE(rows.size(), 3);
        const QVariantMap running = rows[0].toMap();
        QVERIFY(running.value(s("running")).toBool());
        QCOMPARE(running.value(s("played")).toString(), s("25 min so far"));
        QCOMPARE(running.value(s("averageFps")).toString(), s("144"));
        QCOMPARE(running.value(s("power")).toString(), s("—"));

        const QVariantMap full = rows[1].toMap();
        QVERIFY(!full.value(s("running")).toBool());
        QCOMPARE(full.value(s("date")).toString(), QLocale().toString(QDateTime::fromSecsSinceEpoch(1789990000).toLocalTime(), QLocale::ShortFormat));
        QCOMPARE(full.value(s("played")).toString(), s("1 h 5 min"));
        QCOMPARE(full.value(s("averageFps")).toString(), s("120"));
        QCOMPARE(full.value(s("low1")).toString(), s("95"));
        QCOMPARE(full.value(s("low01")).toString(), s("81"));
        QCOMPARE(full.value(s("temperature")).toString(), s("60 °C"));
        QCOMPARE(full.value(s("hotspot")).toString(), s("75 °C (88)"));
        QCOMPARE(full.value(s("power")).toString(), s("210 W"));

        const QVariantMap blind = rows[2].toMap();
        QCOMPARE(blind.value(s("played")).toString(), s("10 min"));
        QCOMPARE(blind.value(s("temperature")).toString(), s("—"));
        QCOMPARE(blind.value(s("hotspot")).toString(), s("—"));

        // Clearing keeps the session still being played.
        editor.clearSessions();
        QCOMPARE(editor.sessions().size(), 1);
        QVERIFY(editor.sessions().first().toMap().value(s("running")).toBool());

        // Every game has its own history.
        editor.setAppId(1001);
        QVERIFY(editor.sessions().isEmpty());
    }

    void editorLocksMeasuringWithAntiCheat()
    {
        Setup setup;
        GameLibrary library(setup.steam.options(), &setup.store, nullptr, setup.proc.path());
        GameSettingsEditor editor(&library, &setup.store, SystemInfo{true}, nullptr, 50);
        library.start();
        QVERIFY(waitUntilIdle(library));

        editor.setAppId(1004); // ships Easy Anti-Cheat
        QVERIFY(editor.filtersLocked());
        QVERIFY(editor.measureAvailable());
        QCOMPARE(editor.measureNote(), s("Locked until you accept the risk under In-game filters."));
        editor.setMeasurePerformance(false);
        QVERIFY(editor.measurePerformance()); // refused while locked
        editor.setAntiAliasing(1);
        QCOMPARE(editor.antiAliasing(), 0);

        editor.acceptFiltersRisk();
        QVERIFY(library.wantsLaunchLine(1004)); // measured now
        editor.setMeasurePerformance(false);
        QVERIFY(!editor.measurePerformance());
        QVERIFY(editor.customized());
        editor.flush();
        QVERIFY(!library.wantsLaunchLine(1004)); // nothing left to do in it
        editor.setAntiAliasing(1);
        QCOMPARE(editor.antiAliasing(), 1);
        editor.flush();
        QVERIFY(library.wantsLaunchLine(1004));
    }

    // gfxtune-run never scans Steam: what the scan found goes into the
    // game's settings file as soon as there is one.
    void editorSavesTheGamesFacts()
    {
        Setup setup;
        GameLibrary library(setup.steam.options(), &setup.store, nullptr, setup.proc.path());
        GameSettingsEditor editor(&library, &setup.store, SystemInfo{true}, nullptr, 50);
        library.start();
        QVERIFY(waitUntilIdle(library));

        // Saved with the scan, for every game that has any.
        QCOMPARE(setup.store.loadFacts(1004).antiCheat, QStringList{s("Easy Anti-Cheat")});
        QCOMPARE(library.factsFor(1004), setup.store.loadFacts(1004));
        QVERIFY(setup.store.loadProfile(1004).isEmpty());
        QVERIFY(!QFile::exists(setup.store.pathFor(1001)));
        editor.setAppId(1004);
        editor.acceptFiltersRisk(); // saved at once, next to them
        QVERIFY(setup.store.loadProfile(1004).own.filtersRiskAccepted);
        QCOMPARE(setup.store.loadFacts(1004), library.factsFor(1004));

        editor.setAppId(1001);
        editor.setGameMode(true);
        editor.flush();
        QCOMPARE(setup.store.loadFacts(1001), settings::GameFacts{});
        QCOMPARE(library.factsFor(9999), settings::GameFacts{}); // not installed
        QCOMPARE(library.gameName(1001), s("Ashfall Protocol"));
        QCOMPARE(library.gameName(9999), s("Game 9999"));
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
