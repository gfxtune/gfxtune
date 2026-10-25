// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "steam/GameFiles.h"
#include "support/Fixtures.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace gfxtune;
using steam::GraphicsApi;
using steam::GraphicsApis;

class GameFilesTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    QString gameFolder(const QString &name)
    {
        return m_dir.filePath(name);
    }

private Q_SLOTS:
    void followsLaunchEntryIgnoringCaseAndSlashes()
    {
        const QString root = gameFolder(QStringLiteral("case"));
        QVERIFY(test::writeFile(root + QStringLiteral("/Bin/Win64/Game.EXE"), test::writePe(true, {QStringLiteral("d3d12.dll")})));
        const auto report = steam::inspectGameFiles(root, {QStringLiteral("bin\\win64\\game.exe")}, true);
        QCOMPARE(report.apis, GraphicsApis(GraphicsApi::DirectX12));
    }

    void readsLibrariesShippedNextToTheProgram()
    {
        // Engines such as Unity: the program loads the engine library, which
        // loads Direct3D.
        const QString root = gameFolder(QStringLiteral("engine"));
        QVERIFY(test::writeFile(root + QStringLiteral("/Game.exe"), test::writePe(true, {QStringLiteral("UnityPlayer.dll"), QStringLiteral("kernel32.dll")})));
        QVERIFY(test::writeFile(root + QStringLiteral("/UnityPlayer.dll"), test::writePe(true, {QStringLiteral("d3d11.dll")}, {QStringLiteral("d3d12.dll")})));
        const auto report = steam::inspectGameFiles(root, {QStringLiteral("Game.exe")}, true);
        QCOMPARE(report.apis, GraphicsApi::DirectX11 | GraphicsApi::DirectX12);
    }

    void fallsBackToTheLargestProgram()
    {
        const QString root = gameFolder(QStringLiteral("launcher"));
        QVERIFY(test::writeFile(root + QStringLiteral("/Launcher.exe"), test::writePe(true, {QStringLiteral("kernel32.dll")})));
        QByteArray big = test::writePe(true, {QStringLiteral("d3d11.dll")});
        big += QByteArray(200000, '\0');
        QVERIFY(test::writeFile(root + QStringLiteral("/Binaries/Win64/Game-Shipping.exe"), big));
        QVERIFY(test::writeFile(root + QStringLiteral("/Tools/CrashReporter.exe"), test::writePe(true, {QStringLiteral("opengl32.dll")})));
        const auto report = steam::inspectGameFiles(root, {QStringLiteral("Launcher.exe")}, true);
        QCOMPARE(report.apis, GraphicsApis(GraphicsApi::DirectX11));
    }

    void readsUnityGamesAsDirectX11()
    {
        // Unity's player links OpenGL and loads Direct3D 11 only at run time.
        const QString root = gameFolder(QStringLiteral("unity"));
        QVERIFY(test::writeFile(root + QStringLiteral("/Raftlike.exe"), test::writePe(true, {QStringLiteral("UnityPlayer.dll")})));
        QVERIFY(test::writeFile(root + QStringLiteral("/UnityPlayer.dll"), test::writePe(true, {QStringLiteral("OPENGL32.dll"), QStringLiteral("kernel32.dll")})));
        auto report = steam::inspectGameFiles(root, {QStringLiteral("Raftlike.exe")}, true);
        QCOMPARE(report.apis, GraphicsApis(GraphicsApi::DirectX11));

        // A Unity game that links Vulkan keeps what it links.
        QVERIFY(test::writeFile(root + QStringLiteral("/UnityPlayer.dll"), test::writePe(true, {QStringLiteral("vulkan-1.dll"), QStringLiteral("OPENGL32.dll")})));
        report = steam::inspectGameFiles(root, {QStringLiteral("Raftlike.exe")}, true);
        QVERIFY(report.apis & GraphicsApi::Vulkan);
    }

    void dropsOpenGlWhenAWindowsGameLinksDirectX()
    {
        const QString root = gameFolder(QStringLiteral("both"));
        QVERIFY(test::writeFile(root + QStringLiteral("/Game.exe"), test::writePe(true, {QStringLiteral("d3d11.dll"), QStringLiteral("opengl32.dll")})));
        QCOMPARE(steam::inspectGameFiles(root, {QStringLiteral("Game.exe")}, true).apis, GraphicsApis(GraphicsApi::DirectX11));

        // Without DirectX or Unity, OpenGL is what it is.
        const QString gl = gameFolder(QStringLiteral("opengl"));
        QVERIFY(test::writeFile(gl + QStringLiteral("/Game.exe"), test::writePe(true, {QStringLiteral("opengl32.dll")})));
        QCOMPARE(steam::inspectGameFiles(gl, {QStringLiteral("Game.exe")}, true).apis, GraphicsApis(GraphicsApi::OpenGL));

        // Native games are left as found: Linux libraries are a good guide.
        const QString native = gameFolder(QStringLiteral("native-both"));
        QVERIFY(test::writeFile(native + QStringLiteral("/game"), test::writeElf(true, {QStringLiteral("libvulkan.so.1"), QStringLiteral("libGL.so.1")}), true));
        QCOMPARE(steam::inspectGameFiles(native, {QStringLiteral("game")}, false).apis, GraphicsApi::Vulkan | GraphicsApi::OpenGL);
    }

    void detectsFsr31Libraries()
    {
        const QString root = gameFolder(QStringLiteral("fsr"));
        QVERIFY(test::writeFile(root + QStringLiteral("/Game.exe"), test::writePe(true, {QStringLiteral("d3d12.dll")})));
        QVERIFY(test::writeFile(root + QStringLiteral("/Plugins/AMD_FidelityFX_DX12.dll"), QByteArray("x")));
        auto report = steam::inspectGameFiles(root, {}, true);
        QVERIFY(report.fsr31Dx12);
        QVERIFY(!report.fsr31Vulkan);
        QVERIFY(test::writeFile(root + QStringLiteral("/amd_fidelityfx_vk.dll"), QByteArray("x")));
        report = steam::inspectGameFiles(root, {}, true);
        QVERIFY(report.fsr31Vulkan);
    }

    void readsNativeGames()
    {
        const QString root = gameFolder(QStringLiteral("native"));
        QVERIFY(test::writeFile(root + QStringLiteral("/game.x86_64"), test::writeElf(true, {QStringLiteral("UnityPlayer.so"), QStringLiteral("libc.so.6")}), true));
        QVERIFY(test::writeFile(root + QStringLiteral("/UnityPlayer.so"), test::writeElf(true, {QStringLiteral("libGL.so.1")})));
        // Not executable, so not a program even though it is an ELF file.
        QVERIFY(test::writeFile(root + QStringLiteral("/data/tool"), test::writeElf(true, {QStringLiteral("libvulkan.so.1")})));
        const auto report = steam::inspectGameFiles(root, {QStringLiteral("game.x86_64")}, false);
        QCOMPARE(report.apis, GraphicsApis(GraphicsApi::OpenGL));
    }

    void ignoresShellScriptsAsPrograms()
    {
        const QString root = gameFolder(QStringLiteral("script"));
        QVERIFY(test::writeFile(root + QStringLiteral("/start.sh"), QByteArray("#!/bin/sh\n./game\n"), true));
        const auto report = steam::inspectGameFiles(root, {QStringLiteral("start.sh")}, false);
        QCOMPARE(report.apis, GraphicsApis());
    }

    void stopsAtDepthLimitAndSkipsLinkedFolders()
    {
        const QString root = gameFolder(QStringLiteral("deep"));
        QVERIFY(test::writeFile(root + QStringLiteral("/a/b/c/d/e/amd_fidelityfx_vk.dll"), QByteArray("x")));
        QVERIFY(test::writeFile(root + QStringLiteral("/a/b/c/d/e/f/amd_fidelityfx_dx12.dll"), QByteArray("x")));
        QVERIFY(QFile::link(root, root + QStringLiteral("/a/loop")));
        const auto report = steam::inspectGameFiles(root, {}, true);
        QVERIFY(report.fsr31Vulkan);
        QVERIFY(!report.fsr31Dx12);
    }

    void handlesMissingAndEmptyFolders()
    {
        const auto missing = steam::inspectGameFiles(gameFolder(QStringLiteral("nope")), {QStringLiteral("x.exe")}, true);
        QCOMPARE(missing.apis, GraphicsApis());
        QVERIFY(QDir().mkpath(gameFolder(QStringLiteral("empty"))));
        const auto empty = steam::inspectGameFiles(gameFolder(QStringLiteral("empty")), {QStringLiteral("x.exe")}, true);
        QCOMPARE(empty.apis, GraphicsApis());
        QVERIFY(!empty.fsr31Dx12);
    }

    void survivesBrokenPrograms()
    {
        const QString root = gameFolder(QStringLiteral("broken"));
        QVERIFY(test::writeFile(root + QStringLiteral("/Game.exe"), QByteArray("MZ") + QByteArray(10, '\xFF')));
        QVERIFY(test::writeFile(root + QStringLiteral("/Other.exe"), QByteArray()));
        const auto report = steam::inspectGameFiles(root, {QStringLiteral("Game.exe")}, true);
        QCOMPARE(report.apis, GraphicsApis());
    }

    void recognisesAntiCheat()
    {
        const auto file = [](const char *name) { return steam::antiCheatOfFile(QString::fromUtf8(name)); };
        const auto folder = [](const char *name) { return steam::antiCheatOfFolder(QString::fromUtf8(name)); };
        QCOMPARE(file("easyanticheat_x64.dll"), QStringLiteral("Easy Anti-Cheat"));
        QCOMPARE(file("start_protected_game.exe"), QStringLiteral("Easy Anti-Cheat"));
        QCOMPARE(file("beservice_x64.exe"), QStringLiteral("BattlEye"));
        QCOMPARE(file("beclient_x64.so"), QStringLiteral("BattlEye"));
        QCOMPARE(file("ace-base64.sys"), QStringLiteral("Anti-Cheat Expert"));
        QCOMPARE(file("sguard64.exe"), QStringLiteral("Anti-Cheat Expert"));
        QCOMPARE(file("mhypbase.dll"), QStringLiteral("miHoYo Protect"));
        QCOMPARE(file("hoyokprotect.sys"), QStringLiteral("miHoYo Protect"));
        QCOMPARE(file("gamemon64.des"), QStringLiteral("nProtect GameGuard"));
        QCOMPARE(file("x3.xem"), QStringLiteral("XIGNCODE3"));
        QCOMPARE(file("eaanticheat.gameservice.exe"), QStringLiteral("EA Anti-Cheat"));
        QVERIFY(file("unknownguard.sys").contains(QLatin1String("kernel driver")));
        // Look-alikes that are not anti-cheat.
        QVERIFY(file("ace-of-spades.png").isEmpty());
        QVERIFY(file("sguard_logo.png").isEmpty());
        QVERIFY(file("unityplayer.dll").isEmpty());
        QVERIFY(file("system.dll").isEmpty());

        QCOMPARE(folder("easyanticheat_eos"), QStringLiteral("Easy Anti-Cheat"));
        QCOMPARE(folder("battleye"), QStringLiteral("BattlEye"));
        QCOMPARE(folder("gameguard"), QStringLiteral("nProtect GameGuard"));
        QCOMPARE(folder("anticheatexpert"), QStringLiteral("anti-cheat"));
        QCOMPARE(folder("anti_cheat"), QStringLiteral("anti-cheat"));
        QVERIFY(folder("cheats").isEmpty());
        QVERIFY(folder("battle").isEmpty());

        // In a game folder, each found once, whatever the letter case.
        const QString root = gameFolder(QStringLiteral("protected"));
        QVERIFY(test::writeFile(root + QStringLiteral("/Game.exe"), test::writePe(true, {QStringLiteral("d3d11.dll")})));
        QVERIFY(test::writeFile(root + QStringLiteral("/EasyAntiCheat/EasyAntiCheat_x64.dll"), "x"));
        QVERIFY(test::writeFile(root + QStringLiteral("/BattlEye/BEService_x64.exe"), "x"));
        QVERIFY(test::writeFile(root + QStringLiteral("/Engine/Binaries/Driver.sys"), "x"));
        const QStringList found = steam::inspectGameFiles(root, {QStringLiteral("Game.exe")}, true).antiCheat;
        QCOMPARE(found.size(), 3);
        QVERIFY(found.contains(QStringLiteral("Easy Anti-Cheat")));
        QVERIFY(found.contains(QStringLiteral("BattlEye")));
        QVERIFY(found.contains(QStringLiteral("a kernel driver, most likely anti-cheat")));
        QVERIFY(steam::inspectGameFiles(gameFolder(QStringLiteral("unity")), {}, true).antiCheat.isEmpty());
    }
};

QTEST_GUILESS_MAIN(GameFilesTest)
#include "tst_gamefiles.moc"
