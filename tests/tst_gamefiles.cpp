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
};

QTEST_GUILESS_MAIN(GameFilesTest)
#include "tst_gamefiles.moc"
