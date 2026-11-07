// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "FakeSteam.h"

#include "Fixtures.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTest>

namespace gfxtune::test
{

using vdf::Node;

namespace
{

QString s(const char *text)
{
    return QString::fromUtf8(text);
}

Node manifest(quint32 appId, const QString &name, const QString &installDir, const QString &stateFlags = QStringLiteral("4"))
{
    return file({section(s("AppState"),
                         {
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
    return section({},
                   {
                       section(s("common"), {value(s("type"), type), value(s("oslist"), osList)}),
                       section(s("config"), {section(s("launch"), std::move(entries))}),
                   });
}

Node launchEntry(const QString &executable, const QString &osList)
{
    return section({}, {value(s("executable"), executable), section(s("config"), {value(s("oslist"), osList)})});
}

} // namespace

Node file(std::vector<Node> children)
{
    Node root;
    root.isSection = true;
    root.children = std::move(children);
    return root;
}

FakeSteam::FakeSteam()
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

steam::ScanOptions FakeSteam::options() const
{
    steam::ScanOptions o;
    o.homePath = home;
    o.compatToolDirs = {systemTools};
    return o;
}

void FakeSteam::text(const QString &path, const Node &contents)
{
    QVERIFY(writeFile(path, writeText(contents)));
}

void FakeSteam::game(const QString &library, quint32 appId, const QString &name, const QString &installDir, const QString &stateFlags)
{
    text(library + s("/steamapps/appmanifest_%1.acf").arg(appId), manifest(appId, name, installDir, stateFlags));
    QDir().mkpath(library + s("/steamapps/common/") + installDir);
}

void FakeSteam::build()
{
    text(root + s("/steamapps/libraryfolders.vdf"),
         file({section(s("libraryfolders"),
                       {
                           section(s("0"), {value(s("path"), root)}),
                           section(s("1"), {value(s("path"), lib2)}),
                           section(s("2"), {value(s("path"), s("/does/not/exist"))}),
                           value(s("contentstatsid"), s("-1")),
                       })}));

    // 1001: Windows only, Proton chosen by the user (a custom build), DirectX 12, FSR 3.1.
    game(root, 1001, s("Ashfall Protocol"), s("Ashfall Protocol"));
    QVERIFY(writeFile(root + s("/steamapps/common/Ashfall Protocol/bin/ashfall.EXE"), writePe(true, {s("d3d12.dll")})));
    QVERIFY(writeFile(root + s("/steamapps/common/Ashfall Protocol/bin/amd_fidelityfx_dx12.dll"), "x"));

    // 1002: Windows only, Proton picked by Valve for this game, DirectX 11 behind a launcher.
    game(lib2, 1002, s("Neon Drift"), s("NeonDrift"));
    QVERIFY(writeFile(lib2 + s("/steamapps/common/NeonDrift/Launcher.exe"), writePe(true, {s("kernel32.dll")})));
    QVERIFY(writeFile(lib2 + s("/steamapps/common/NeonDrift/Game/NeonDrift.exe"),
                      writePe(true, {s("UnityPlayer.dll")}) + QByteArray(100000, '\0')));
    QVERIFY(writeFile(lib2 + s("/steamapps/common/NeonDrift/Game/UnityPlayer.dll"), writePe(true, {s("d3d11.dll")})));
    // Its cover in the newer layout: Steam's data names a file under a hash.
    // Another cover-named file in the folder must not win, even a newer one.
    const QString neonCover = root + s("/appcache/librarycache/1002/") + NeonDriftCover;
    QVERIFY(writeFile(neonCover, "x"));
    QFile neonCoverFile(neonCover);
    QVERIFY(neonCoverFile.open(QIODevice::ReadWrite));
    QVERIFY(neonCoverFile.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-3600), QFileDevice::FileModificationTime));
    QVERIFY(writeFile(root + s("/appcache/librarycache/1002/library_600x900.jpg"), "x"));

    // 1003: has a Linux version, runs natively with Vulkan.
    game(lib2, 1003, s("Stonewake"), s("Stonewake"));
    QVERIFY(writeFile(lib2 + s("/steamapps/common/Stonewake/stonewake"), writeElf(true, {s("libvulkan.so.1")}), true));

    // 1004: Windows only, uses the default tool for all other titles. Ships
    // Easy Anti-Cheat.
    game(root, 1004, s("Hollow Orbit"), s("Hollow Orbit"));
    QVERIFY(writeFile(root + s("/steamapps/common/Hollow Orbit/EasyAntiCheat/EasyAntiCheat_EOS_Setup.exe"), "x"));

    // 1005: has a Linux version, but the user forced Proton. Steam lists
    // Valve Anti-Cheat for it.
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
    QVERIFY(writeFile(root + s("/steamapps/appmanifest_1012.acf"), "\"AppState\" { \"appid\" "));
    game(root, 228980, s("Steamworks Common Redistributables"), s("Steamworks Shared"));
    game(lib2, 2805730, s("Proton 9.0"), s("Proton 9.0"));
    QVERIFY(writeFile(lib2 + s("/steamapps/common/Proton 9.0/toolmanifest.vdf"), "\"manifest\" { }"));
    // Launch scripts as far as FSR 4 goes: Proton 9.0 knows nothing of it,
    // GE-Proton upgrades when PROTON_FSR4_UPGRADE asks.
    QVERIFY(writeFile(lib2 + s("/steamapps/common/Proton 9.0/proton"), "#!/usr/bin/env python3\nprint('proton')\n", true));
    QVERIFY(writeFile(systemTools + s("/ge-proton/proton"),
                      "#!/usr/bin/env python3\n    g_session.check_environment(\"PROTON_FSR4_UPGRADE\", \"fsr4\")\n", true));

    // A custom Proton build installed by a system package.
    text(systemTools + s("/ge-proton/compatibilitytool.vdf"),
         file({section(s("compatibilitytools"),
                       {
                           section(s("compat_tools"),
                                   {section(s("GE-Proton10-26"),
                                            {
                                                value(s("install_path"), s(".")),
                                                value(s("display_name"), s("GE-Proton10-26")),
                                                value(s("from_oslist"), s("windows")),
                                                value(s("to_oslist"), s("linux")),
                                            })}),
                       })}));

    text(root + s("/config/config.vdf"),
         file({section(
             s("InstallConfigStore"),
             {section(
                 s("Software"),
                 {
                     section(
                         s("Valve"),
                         {section(
                             s("Steam"),
                             {section(s("CompatToolMapping"),
                                      {
                                          section(s("0"),
                                                  {value(s("name"), s("proton_experimental")), value(s("config"), {}),
                                                   value(s("priority"), s("75"))}),
                                          section(s("1001"), {value(s("name"), s("GE-Proton10-26")), value(s("priority"), s("250"))}),
                                          section(s("1005"), {value(s("name"), s("proton_experimental")), value(s("priority"), s("250"))}),
                                          section(s("1003"), {value(s("name"), {}), value(s("priority"), s("250"))}),
                                      })})}),
                 })})}));

    text(root + s("/config/loginusers.vdf"),
         file({section(
             s("users"),
             {
                 section(QString::number(OlderAccount),
                         {value(s("AccountName"), s("first")), value(s("MostRecent"), s("0")), value(s("Timestamp"), s("1800000000"))}),
                 section(QString::number(RecentAccount),
                         {value(s("AccountName"), s("second")), value(s("MostRecent"), s("1")), value(s("Timestamp"), s("1700000000"))}),
             })}));

    // Steam writes "apps"; check the reader does not depend on the case.
    text(root + s("/userdata/%1/config/localconfig.vdf").arg(RecentAccountFolder),
         file({section(
             s("UserLocalConfigStore"),
             {
                 section(
                     s("Software"),
                     {section(s("Valve"),
                              {section(s("Steam"),
                                       {section(s("Apps"),
                                                {
                                                    section(s("1001"),
                                                            {value(s("LaunchOptions"), s("gamemoderun %command% -dx12 \"quoted arg\""))}),
                                                    section(s("1003"), {value(s("LastPlayed"), s("1700000000"))}),
                                                })})})}),
             })}));

    Node vacGame = appInfo(s("Game"), s("windows,linux"));
    vacGame.children.front().children.push_back(section(s("category"), {value(s("category_2"), s("1")), value(s("category_8"), s("1"))}));
    Node neonDrift = appInfo(s("game"), s("windows"), {launchEntry(s("Launcher.exe"), {})});
    neonDrift.children.front().children.push_back(section(
        s("library_assets_full"),
        {section(s("library_capsule"),
                 {
                     section(s("image"),
                             {value(s("english"), NeonDriftCover),
                              value(s("japanese"), s("b2c3d4e5f60718293a4b5c6d7e8f9012345678a1/library_capsule_japanese.jpg"))}),
                     section(s("image2x"), {value(s("english"), s("a1b2c3d4e5f60718293a4b5c6d7e8f9012345678/library_capsule_2x.jpg"))}),
                 })}));
    const QList<QPair<quint32, Node>> apps{
        {1001, appInfo(s("Game"), s("windows"), {launchEntry(s("Bin\\Ashfall.exe"), s("windows"))})},
        {1002, neonDrift},
        {1003,
         appInfo(s("Game"), s("windows,linux"), {launchEntry(s("Stonewake.exe"), s("windows")), launchEntry(s("stonewake"), s("linux"))})},
        {1004, appInfo(s("Game"), s("windows"))},
        {1005, vacGame},
        {1008, appInfo(s("Music"), s("windows,linux"))},
        {891390,
         section({},
                 {section(s("extended"),
                          {
                              section(s("compat_tools"),
                                      {
                                          section(s("proton_9"),
                                                  {value(s("appid"), s("2805730")), value(s("from_oslist"), s("windows")),
                                                   value(s("to_oslist"), s("linux")), value(s("aliases"), s("proton-9,proton9"))}),
                                          section(s("proton_experimental"),
                                                  {value(s("appid"), s("1493710")), value(s("from_oslist"), s("windows")),
                                                   value(s("to_oslist"), s("linux")), value(s("display_name"), s("Proton Experimental"))}),
                                          section(s("steamlinuxruntime_sniper"),
                                                  {value(s("appid"), s("1628350")), value(s("from_oslist"), s("linux")),
                                                   value(s("to_oslist"), s("linux"))}),
                                      }),
                              section(s("app_mappings"),
                                      {
                                          section(s("1002"),
                                                  {value(s("appid"), s("1002")), value(s("tool"), s("proton-9")),
                                                   value(s("priority"), s("75"))}),
                                          section(s("1003"),
                                                  {value(s("appid"), s("1003")), value(s("tool"), s("steamlinuxruntime_sniper")),
                                                   value(s("priority"), s("75"))}),
                                      }),
                          })})},
    };
    QVERIFY(writeFile(root + s("/appcache/appinfo.vdf"), writeAppInfo(29, apps)));
}

} // namespace gfxtune::test
