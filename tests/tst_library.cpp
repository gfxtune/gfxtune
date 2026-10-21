// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "GameLibrary.h"
#include "GameSettingsEditor.h"
#include "support/FakeSteam.h"
#include "support/Fixtures.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

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

        editor.setAppId(1001); // DirectX 12 with FSR 3.1
        QVERIFY(!editor.frameCapAvailable());
        QVERIFY(editor.fsr4Available());
        QSignalSpy availability(&editor, &GameSettingsEditor::availabilityChanged);
        editor.setGamescope(true);
        QVERIFY(availability.count() >= 1);
        QVERIFY(editor.frameCapAvailable());
        QVERIFY(!editor.frameCapNote().isEmpty());

        editor.setAppId(1003); // native Vulkan
        QVERIFY(!editor.fsr4Available());
        QVERIFY(!editor.fsr4Note().isEmpty());
    }

    void resetAndRemoveAll()
    {
        Setup setup;
        GameLibrary library(setup.steam.options(), &setup.store, nullptr, setup.proc.path());
        GameSettingsEditor editor(&library, &setup.store, SystemInfo{true}, nullptr, 50);
        library.start();
        QVERIFY(waitUntilIdle(library));

        editor.setAppId(1001);
        editor.setFsr4Upgrade(true);
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

    void reportsSaveFailures()
    {
        Setup setup;
        GameLibrary library(setup.steam.options(), &setup.store, nullptr, setup.proc.path());
        GameSettingsEditor editor(&library, &setup.store, SystemInfo{true}, nullptr, 50);
        library.start();
        QVERIFY(waitUntilIdle(library));
        QVERIFY(test::writeFile(setup.store.baseDir() + s("/games"), "a file where a folder belongs"));
        editor.setAppId(1001);
        editor.setFsr4Upgrade(true);
        editor.flush();
        QVERIFY(editor.problem().startsWith(s("Could not save")));
    }
};

QTEST_GUILESS_MAIN(LibraryTest)
#include "tst_library.moc"
