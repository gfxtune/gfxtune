// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "steam/SteamLibrary.h"
#include "support/FakeSteam.h"
#include "support/Fixtures.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace gfxtune;
using namespace gfxtune::steam;
using test::section;
using test::value;
using vdf::Node;
using test::FakeSteam;
using test::file;
using test::OlderAccount;
using test::RecentAccount;
using test::RecentAccountFolder;

namespace
{

QString s(const char *text)
{
    return QString::fromUtf8(text);
}

const Game *findGame(const SteamScan &scan, quint32 appId)
{
    for (const Game &game : scan.games) {
        if (game.appId == appId) {
            return &game;
        }
    }
    return nullptr;
}

} // namespace

class SteamScanTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void findsSteamAndLibraries()
    {
        FakeSteam steam;
        steam.build();
        const SteamScan scan = scanSteam(steam.options());
        QVERIFY(scan.steamFound);
        QCOMPARE(scan.steamRoot, QFileInfo(steam.root).canonicalFilePath());
        QCOMPARE(scan.libraries.size(), 3);
        QVERIFY(scan.libraries[0].available);
        QCOMPARE(scan.libraries[1].path, QFileInfo(steam.lib2).canonicalFilePath());
        QVERIFY(scan.libraries[1].available);
        QCOMPARE(scan.libraries[2].path, s("/does/not/exist"));
        QVERIFY(!scan.libraries[2].available);
        QCOMPARE(scan.accountId, RecentAccountFolder);
    }

    void listsOnlyInstalledGamesSortedByName()
    {
        FakeSteam steam;
        steam.build();
        const SteamScan scan = scanSteam(steam.options());
        QStringList names;
        for (const Game &game : scan.games) {
            names << game.name;
        }
        QCOMPARE(names, (QStringList{s("App 1010"), s("Ashfall Protocol"), s("Copper Coast"), s("Hollow Orbit"), s("Lanternfall"),
                                     s("Neon Drift"), s("Stonewake")}));
        // The damaged manifest is reported, not fatal.
        QVERIFY(scan.warnings.filter(s("appmanifest_1012.acf")).size() == 1);
    }

    void resolvesHowEachGameRuns()
    {
        FakeSteam steam;
        steam.build();
        const SteamScan scan = scanSteam(steam.options());

        const Game *ashfall = findGame(scan, 1001);
        QCOMPARE(ashfall->runtime, Runtime::Proton);
        QCOMPARE(ashfall->tool.name, s("GE-Proton10-26"));
        QCOMPARE(ashfall->tool.displayName, s("GE-Proton10-26"));
        QCOMPARE(ashfall->tool.installPath, steam.systemTools + s("/ge-proton"));

        const Game *neon = findGame(scan, 1002);
        QCOMPARE(neon->runtime, Runtime::Proton);
        QCOMPARE(neon->tool.name, s("proton-9"));
        QCOMPARE(neon->tool.displayName, s("Proton 9.0")); // from the installed tool's manifest
        QCOMPARE(neon->tool.installPath, QFileInfo(steam.lib2).canonicalFilePath() + s("/steamapps/common/Proton 9.0"));

        const Game *stonewake = findGame(scan, 1003);
        QCOMPARE(stonewake->runtime, Runtime::Native); // Linux runtime tools are not Proton
        QVERIFY(stonewake->tool.name.isEmpty());

        const Game *hollow = findGame(scan, 1004);
        QCOMPARE(hollow->runtime, Runtime::Proton);
        QCOMPARE(hollow->tool.name, s("proton_experimental"));
        QCOMPARE(hollow->tool.displayName, s("Proton Experimental"));
        QVERIFY(hollow->tool.installPath.isEmpty()); // not installed

        const Game *copper = findGame(scan, 1005);
        QCOMPARE(copper->runtime, Runtime::Proton);
        QCOMPARE(copper->tool.name, s("proton_experimental"));

        QCOMPARE(findGame(scan, 1009)->runtime, Runtime::Proton);  // no cache entry, has a prefix
        QCOMPARE(findGame(scan, 1010)->runtime, Runtime::Unknown); // no cache entry, no prefix
    }

    void readsGraphicsAndFsrFromGameFiles()
    {
        FakeSteam steam;
        steam.build();
        const SteamScan scan = scanSteam(steam.options());
        QCOMPARE(findGame(scan, 1001)->apis, GraphicsApis(GraphicsApi::DirectX12));
        QVERIFY(findGame(scan, 1001)->fsr31Dx12);
        QCOMPARE(findGame(scan, 1002)->apis, GraphicsApis(GraphicsApi::DirectX11));
        QVERIFY(!findGame(scan, 1002)->fsr31Dx12);
        QCOMPARE(findGame(scan, 1003)->apis, GraphicsApis(GraphicsApi::Vulkan));
        QCOMPARE(findGame(scan, 1004)->apis, GraphicsApis());
    }

    void skipsGameFilesWhenAsked()
    {
        FakeSteam steam;
        steam.build();
        ScanOptions options = steam.options();
        options.inspectGameFiles = false;
        const SteamScan scan = scanSteam(options);
        QCOMPARE(findGame(scan, 1001)->apis, GraphicsApis());
        QVERIFY(!findGame(scan, 1001)->fsr31Dx12);
    }

    void readsLaunchOptionsOfTheRecentAccount()
    {
        FakeSteam steam;
        steam.build();
        const SteamScan scan = scanSteam(steam.options());
        QCOMPARE(findGame(scan, 1001)->launchOptions, s("gamemoderun %command% -dx12 \"quoted arg\""));
        QVERIFY(findGame(scan, 1003)->launchOptions.isEmpty());
    }

    void explainsWhyAppsAreLeftOut()
    {
        FakeSteam steam;
        steam.build();
        steam.game(steam.lib2, 1001, s("Ashfall Protocol"), s("Ashfall Copy"));
        const SteamScan scan = scanSteam(steam.options());
        QHash<quint32, QString> reasons;
        for (const SkippedApp &app : scan.skipped) {
            reasons.insert(app.appId, app.reason);
        }
        QVERIFY(reasons.value(1006).startsWith(s("not fully installed")));
        QVERIFY(reasons.value(1008).contains(s("Music")));
        QVERIFY(reasons.value(1011).startsWith(s("its folder is missing")));
        QVERIFY(reasons.value(2805730).contains(s("Steam tool")));
        QVERIFY(reasons.value(228980).contains(s("redistributables")));
        QVERIFY(reasons.value(1001).contains(s("two libraries")));
        // Every installed app is either listed or explained.
        QCOMPARE(scan.games.size() + scan.skipped.size(), 7 + 6);
    }

    void fallsBackToNewestSignInWithoutMostRecent()
    {
        FakeSteam steam;
        steam.build();
        steam.text(steam.root + s("/config/loginusers.vdf"), file({section(s("users"), {
            section(QString::number(OlderAccount), {value(s("Timestamp"), s("1800000000"))}),
            section(QString::number(RecentAccount), {value(s("Timestamp"), s("1700000000"))}),
            value(s("junk"), s("x")),
        })}));
        const SteamScan scan = scanSteam(steam.options());
        QCOMPARE(scan.accountId, quint32(OlderAccount & 0xFFFFFFFFu));
        QVERIFY(findGame(scan, 1001)->launchOptions.isEmpty()); // that account has no settings file
    }

    void worksWithoutSteamsCache()
    {
        FakeSteam steam;
        steam.build();
        QVERIFY(QFile::remove(steam.root + s("/appcache/appinfo.vdf")));
        const SteamScan scan = scanSteam(steam.options());
        // Without type information the soundtrack cannot be told apart.
        QVERIFY(findGame(scan, 1008));
        QCOMPARE(findGame(scan, 1001)->runtime, Runtime::Proton); // chosen by the user
        QCOMPARE(findGame(scan, 1004)->runtime, Runtime::Unknown);
        QCOMPARE(findGame(scan, 1009)->runtime, Runtime::Proton);
        // Graphics still found by scanning, without launch entries.
        QCOMPARE(findGame(scan, 1001)->apis, GraphicsApis(GraphicsApi::DirectX12));
    }

    void reportsADamagedCacheAndCarriesOn()
    {
        FakeSteam steam;
        steam.build();
        QVERIFY(test::writeFile(steam.root + s("/appcache/appinfo.vdf"), QByteArray("not a cache at all")));
        const SteamScan scan = scanSteam(steam.options());
        QVERIFY(scan.warnings.filter(s("appinfo.vdf")).size() == 1);
        QVERIFY(findGame(scan, 1001));
    }

    void readsOldLibraryListFormat()
    {
        FakeSteam steam;
        steam.build();
        steam.text(steam.root + s("/steamapps/libraryfolders.vdf"), file({section(s("LibraryFolders"), {
            value(s("TimeNextStatsReport"), s("1")),
            value(s("1"), steam.lib2),
        })}));
        const SteamScan scan = scanSteam(steam.options());
        QCOMPARE(scan.libraries.size(), 2);
        QVERIFY(findGame(scan, 1002));
    }

    void survivesDamagedLibraryList()
    {
        FakeSteam steam;
        steam.build();
        QVERIFY(test::writeFile(steam.root + s("/steamapps/libraryfolders.vdf"), "\"libraryfolders\" { \"0\" {"));
        const SteamScan scan = scanSteam(steam.options());
        QCOMPARE(scan.libraries.size(), 1);
        QVERIFY(findGame(scan, 1001));
        QVERIFY(!findGame(scan, 1002)); // its library is no longer known
        QVERIFY(scan.warnings.filter(s("libraryfolders.vdf")).size() == 1);
    }

    void listsAGameOnceWhenTwoLibrariesClaimIt()
    {
        FakeSteam steam;
        steam.build();
        steam.game(steam.lib2, 1001, s("Ashfall Protocol"), s("Ashfall Copy"));
        const SteamScan scan = scanSteam(steam.options());
        int count = 0;
        for (const Game &game : scan.games) {
            count += game.appId == 1001 ? 1 : 0;
        }
        QCOMPARE(count, 1);
    }

    void handlesMissingSteam()
    {
        QTemporaryDir dir;
        ScanOptions options;
        options.homePath = dir.path();
        const SteamScan scan = scanSteam(options);
        QVERIFY(!scan.steamFound);
        QVERIFY(scan.games.isEmpty());
    }

    void handlesEmptySteam()
    {
        QTemporaryDir dir;
        QDir().mkpath(dir.filePath(s(".local/share/Steam/steamapps")));
        ScanOptions options;
        options.homePath = dir.path();
        const SteamScan scan = scanSteam(options);
        QVERIFY(scan.steamFound);
        QVERIFY(scan.games.isEmpty());
        QCOMPARE(scan.accountId, 0u);
        QVERIFY(scan.warnings.isEmpty());
    }

    void watchesTheFilesThatMatter()
    {
        FakeSteam steam;
        steam.build();
        const SteamScan scan = scanSteam(steam.options());
        const QString root = scan.steamRoot;
        QVERIFY(scan.watchPaths.contains(root + s("/steamapps")));
        QVERIFY(scan.watchPaths.contains(QFileInfo(steam.lib2).canonicalFilePath() + s("/steamapps")));
        QVERIFY(scan.watchPaths.contains(root + s("/config")));
        QVERIFY(scan.watchPaths.contains(root + s("/appcache")));
        QVERIFY(scan.watchPaths.contains(root + s("/userdata/%1/config").arg(RecentAccountFolder)));
        QVERIFY(scan.watchPaths.contains(steam.systemTools));
        for (const QString &path : scan.watchPaths) {
            QVERIFY2(QFileInfo::exists(path), qPrintable(path));
        }
    }
};

QTEST_GUILESS_MAIN(SteamScanTest)
#include "tst_steamscan.moc"
