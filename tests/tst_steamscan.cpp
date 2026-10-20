// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "steam/SteamLibrary.h"
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

namespace
{

// 64-bit Steam IDs for two made-up accounts; userdata/ uses the low 32 bits.
constexpr quint64 OlderAccount = 76561198000000001ull;
constexpr quint64 RecentAccount = 76561198000000002ull;
constexpr quint32 RecentAccountFolder = quint32(RecentAccount & 0xFFFFFFFFu);

Node file(std::vector<Node> children)
{
    Node root;
    root.isSection = true;
    root.children = std::move(children);
    return root;
}

QString s(const char *text)
{
    return QString::fromUtf8(text);
}

Node manifest(quint32 appId, const QString &name, const QString &installDir, const QString &stateFlags = QStringLiteral("4"))
{
    return file({section(s("AppState"), {
        value(s("appid"), QString::number(appId)),
        value(s("name"), name),
        value(s("StateFlags"), stateFlags),
        value(s("installdir"), installDir),
    })});
}

Node appInfo(const QString &type, const QString &osList, std::vector<Node> launch = {})
{
    std::vector<Node> entries;
    int index = 0;
    for (Node &entry : launch) {
        entry.key = QString::number(index++);
        entries.push_back(std::move(entry));
    }
    return section({}, {
        section(s("common"), {value(s("type"), type), value(s("oslist"), osList)}),
        section(s("config"), {section(s("launch"), std::move(entries))}),
    });
}

Node launchEntry(const QString &executable, const QString &osList)
{
    return section({}, {value(s("executable"), executable), section(s("config"), {value(s("oslist"), osList)})});
}

// A made-up Steam installation:
//   home/.local/share/Steam            the Steam folder, also reached through ~/.steam/steam
//   lib2/                              a second library
//   /does/not/exist                    a library on a drive that is not mounted
class FakeSteam
{
public:
    QTemporaryDir dir;
    QString home;
    QString root;
    QString lib2;
    QString systemTools;

    FakeSteam()
    {
        home = dir.filePath(s("home"));
        root = home + s("/.local/share/Steam");
        lib2 = dir.filePath(s("lib2"));
        systemTools = dir.filePath(s("system-tools"));
        QDir().mkpath(root + s("/steamapps"));
        QDir().mkpath(lib2 + s("/steamapps"));
        QDir().mkpath(home + s("/.steam"));
        QFile::link(root, home + s("/.steam/steam"));
    }

    ScanOptions options() const
    {
        ScanOptions o;
        o.homePath = home;
        o.compatToolDirs = {systemTools};
        return o;
    }

    void text(const QString &path, const Node &contents) { QVERIFY(test::writeFile(path, test::writeText(contents))); }

    void game(const QString &library, quint32 appId, const QString &name, const QString &installDir,
              const QString &stateFlags = QStringLiteral("4"))
    {
        text(library + s("/steamapps/appmanifest_%1.acf").arg(appId), manifest(appId, name, installDir, stateFlags));
        QDir().mkpath(library + s("/steamapps/common/") + installDir);
    }

    void build()
    {
        text(root + s("/steamapps/libraryfolders.vdf"), file({section(s("libraryfolders"), {
            section(s("0"), {value(s("path"), root)}),
            section(s("1"), {value(s("path"), lib2)}),
            section(s("2"), {value(s("path"), s("/does/not/exist"))}),
            value(s("contentstatsid"), s("-1")),
        })}));

        // 1001: Windows only, Proton chosen by the user (a custom build), DirectX 12, FSR 3.1.
        game(root, 1001, s("Ashfall Protocol"), s("Ashfall Protocol"));
        QVERIFY(test::writeFile(root + s("/steamapps/common/Ashfall Protocol/bin/ashfall.EXE"), test::writePe(true, {s("d3d12.dll")})));
        QVERIFY(test::writeFile(root + s("/steamapps/common/Ashfall Protocol/bin/amd_fidelityfx_dx12.dll"), "x"));

        // 1002: Windows only, Proton picked by Valve for this game, DirectX 11 behind a launcher.
        game(lib2, 1002, s("Neon Drift"), s("NeonDrift"));
        QVERIFY(test::writeFile(lib2 + s("/steamapps/common/NeonDrift/Launcher.exe"), test::writePe(true, {s("kernel32.dll")})));
        QVERIFY(test::writeFile(lib2 + s("/steamapps/common/NeonDrift/Game/NeonDrift.exe"),
                                test::writePe(true, {s("UnityPlayer.dll")}) + QByteArray(100000, '\0')));
        QVERIFY(test::writeFile(lib2 + s("/steamapps/common/NeonDrift/Game/UnityPlayer.dll"), test::writePe(true, {s("d3d11.dll")})));

        // 1003: has a Linux version, runs natively with Vulkan.
        game(lib2, 1003, s("Stonewake"), s("Stonewake"));
        QVERIFY(test::writeFile(lib2 + s("/steamapps/common/Stonewake/stonewake"), test::writeElf(true, {s("libvulkan.so.1")}), true));

        // 1004: Windows only, uses the default tool for all other titles.
        game(root, 1004, s("Hollow Orbit"), s("Hollow Orbit"));

        // 1005: has a Linux version, but the user forced Proton.
        game(root, 1005, s("Copper Coast"), s("Copper Coast"));

        // 1006: still downloading.
        game(root, 1006, s("Kestrel Tactics"), s("Kestrel Tactics"), s("1026"));

        // 1008: a soundtrack, not a game.
        game(root, 1008, s("Ashfall Protocol Soundtrack"), s("Ashfall OST"));

        // 1009 and 1010: missing from Steam's cache; 1009 has a Proton prefix.
        game(lib2, 1009, s("Lanternfall"), s("Lanternfall"));
        QDir().mkpath(lib2 + s("/steamapps/compatdata/1009/pfx"));
        game(lib2, 1010, QString(), s("Mystery"));

        // A manifest whose folder was deleted, a damaged manifest and Steam's
        // own tools.
        text(root + s("/steamapps/appmanifest_1011.acf"), manifest(1011, s("Gone"), s("Gone")));
        QVERIFY(test::writeFile(root + s("/steamapps/appmanifest_1012.acf"), "\"AppState\" { \"appid\" "));
        game(root, 228980, s("Steamworks Common Redistributables"), s("Steamworks Shared"));
        game(lib2, 2805730, s("Proton 9.0"), s("Proton 9.0"));
        QVERIFY(test::writeFile(lib2 + s("/steamapps/common/Proton 9.0/toolmanifest.vdf"), "\"manifest\" { }"));

        // A custom Proton build installed by a system package.
        text(systemTools + s("/ge-proton/compatibilitytool.vdf"), file({section(s("compatibilitytools"), {
            section(s("compat_tools"), {section(s("GE-Proton10-26"), {
                value(s("install_path"), s(".")),
                value(s("display_name"), s("GE-Proton10-26")),
                value(s("from_oslist"), s("windows")),
                value(s("to_oslist"), s("linux")),
            })}),
        })}));

        text(root + s("/config/config.vdf"), file({section(s("InstallConfigStore"), {section(s("Software"), {
            section(s("Valve"), {section(s("Steam"), {section(s("CompatToolMapping"), {
                section(s("0"), {value(s("name"), s("proton_experimental")), value(s("config"), {}), value(s("priority"), s("75"))}),
                section(s("1001"), {value(s("name"), s("GE-Proton10-26")), value(s("priority"), s("250"))}),
                section(s("1005"), {value(s("name"), s("proton_experimental")), value(s("priority"), s("250"))}),
                section(s("1003"), {value(s("name"), {}), value(s("priority"), s("250"))}),
            })})}),
        })})}));

        text(root + s("/config/loginusers.vdf"), file({section(s("users"), {
            section(QString::number(OlderAccount), {value(s("AccountName"), s("first")), value(s("MostRecent"), s("0")), value(s("Timestamp"), s("1800000000"))}),
            section(QString::number(RecentAccount), {value(s("AccountName"), s("second")), value(s("MostRecent"), s("1")), value(s("Timestamp"), s("1700000000"))}),
        })}));

        // Steam writes "apps"; check the reader does not depend on the case.
        text(root + s("/userdata/%1/config/localconfig.vdf").arg(RecentAccountFolder), file({section(s("UserLocalConfigStore"), {
            section(s("Software"), {section(s("Valve"), {section(s("Steam"), {section(s("Apps"), {
                section(s("1001"), {value(s("LaunchOptions"), s("gamemoderun %command% -dx12 \"quoted arg\""))}),
                section(s("1003"), {value(s("LastPlayed"), s("1700000000"))}),
            })})})}),
        })}));

        const QList<QPair<quint32, Node>> apps{
            {1001, appInfo(s("Game"), s("windows"), {launchEntry(s("Bin\\Ashfall.exe"), s("windows"))})},
            {1002, appInfo(s("game"), s("windows"), {launchEntry(s("Launcher.exe"), {})})},
            {1003, appInfo(s("Game"), s("windows,linux"), {launchEntry(s("Stonewake.exe"), s("windows")), launchEntry(s("stonewake"), s("linux"))})},
            {1004, appInfo(s("Game"), s("windows"))},
            {1005, appInfo(s("Game"), s("windows,linux"))},
            {1008, appInfo(s("Music"), s("windows,linux"))},
            {891390, section({}, {section(s("extended"), {
                section(s("compat_tools"), {
                    section(s("proton_9"), {value(s("appid"), s("2805730")), value(s("from_oslist"), s("windows")), value(s("to_oslist"), s("linux")), value(s("aliases"), s("proton-9,proton9"))}),
                    section(s("proton_experimental"), {value(s("appid"), s("1493710")), value(s("from_oslist"), s("windows")), value(s("to_oslist"), s("linux")), value(s("display_name"), s("Proton Experimental"))}),
                    section(s("steamlinuxruntime_sniper"), {value(s("appid"), s("1628350")), value(s("from_oslist"), s("linux")), value(s("to_oslist"), s("linux"))}),
                }),
                section(s("app_mappings"), {
                    section(s("1002"), {value(s("appid"), s("1002")), value(s("tool"), s("proton-9")), value(s("priority"), s("75"))}),
                    section(s("1003"), {value(s("appid"), s("1003")), value(s("tool"), s("steamlinuxruntime_sniper")), value(s("priority"), s("75"))}),
                }),
            })})},
        };
        QVERIFY(test::writeFile(root + s("/appcache/appinfo.vdf"), test::writeAppInfo(29, apps)));
    }
};

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
