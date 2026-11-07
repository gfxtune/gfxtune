// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "binaries/ExecutableImports.h"
#include "steam/GameFiles.h"
#include "support/Fixtures.h"

#include <QTest>
#include <QtEndian>

using namespace gfxtune;
using binaries::ExecutableFormat;
using binaries::Imports;
using steam::GraphicsApi;

class ImportsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void readsWindowsImports_data()
    {
        QTest::addColumn<bool>("is64");
        QTest::newRow("64-bit") << true;
        QTest::newRow("32-bit") << false;
    }

    void readsWindowsImports()
    {
        QFETCH(bool, is64);
        const QByteArray program = test::writePe(is64, {QStringLiteral("KERNEL32.dll"), QStringLiteral("D3D11.dll")},
                                                 {QStringLiteral("d3d12.dll"), QStringLiteral("D3D11.DLL")});
        Imports imports;
        QString error;
        QVERIFY2(binaries::parseImports(program, imports, &error), qPrintable(error));
        QCOMPARE(imports.format, ExecutableFormat::Windows);
        // Lower case, delay-loaded included, duplicates dropped.
        QCOMPARE(imports.libraries,
                 (QStringList{QStringLiteral("kernel32.dll"), QStringLiteral("d3d11.dll"), QStringLiteral("d3d12.dll")}));
    }

    void readsOldStyleDelayImports()
    {
        const QByteArray program = test::writePe(false, {}, {QStringLiteral("d3d9.dll")}, true);
        Imports imports;
        QVERIFY(binaries::parseImports(program, imports));
        QCOMPARE(imports.libraries, QStringList{QStringLiteral("d3d9.dll")});
    }

    void readsLinuxNeededLibraries_data()
    {
        QTest::addColumn<bool>("is64");
        QTest::newRow("64-bit") << true;
        QTest::newRow("32-bit") << false;
    }

    void readsLinuxNeededLibraries()
    {
        QFETCH(bool, is64);
        const QByteArray program = test::writeElf(is64, {QStringLiteral("libvulkan.so.1"), QStringLiteral("libc.so.6")});
        Imports imports;
        QString error;
        QVERIFY2(binaries::parseImports(program, imports, &error), qPrintable(error));
        QCOMPARE(imports.format, ExecutableFormat::Linux);
        QCOMPARE(imports.libraries, (QStringList{QStringLiteral("libvulkan.so.1"), QStringLiteral("libc.so.6")}));
    }

    void acceptsProgramsWithoutImports()
    {
        Imports imports;
        QVERIFY(binaries::parseImports(test::writePe(true, {}), imports));
        QVERIFY(imports.libraries.isEmpty());
        QVERIFY(binaries::parseImports(test::writeElf(true, {}), imports));
        QVERIFY(imports.libraries.isEmpty());

        // A statically linked program has no dynamic section at all.
        QByteArray elf = test::writeElf(true, {});
        qToLittleEndian<quint16>(1, elf.data() + 0x38); // keep only the load segment
        QVERIFY(binaries::parseImports(elf, imports));
        QVERIFY(imports.libraries.isEmpty());
    }

    void rejectsOtherFiles_data()
    {
        QTest::addColumn<QByteArray>("data");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("text") << QByteArray("#!/bin/sh\necho hi\n");
        QTest::newRow("MZ without PE") << QByteArray("MZ") + QByteArray(100, '\0');
        QTest::newRow("PE header cut off") << test::writePe(true, {QStringLiteral("a.dll")}).left(0x90);
        QTest::newRow("ELF cut off") << QByteArray(
            "\x7F"
            "ELF\x02\x01",
            6);
        QByteArray bigEndian = test::writeElf(true, {QStringLiteral("libGL.so.1")});
        bigEndian[5] = 2;
        QTest::newRow("big-endian ELF") << bigEndian;
        QByteArray badClass = test::writeElf(true, {});
        badClass[4] = 9;
        QTest::newRow("unknown ELF class") << badClass;
        QByteArray badMagic = test::writePe(true, {});
        qToLittleEndian<quint16>(0x1234, badMagic.data() + 0x84 + 20);
        QTest::newRow("unknown PE type") << badMagic;
    }

    void rejectsOtherFiles()
    {
        QFETCH(QByteArray, data);
        Imports imports;
        QString error;
        QVERIFY(!binaries::parseImports(data, imports, &error));
        QVERIFY(!error.isEmpty());
    }

    void ignoresImportTablesOutsideTheFile()
    {
        QByteArray program = test::writePe(true, {QStringLiteral("d3d11.dll")});
        // Point the import directory at an address no section covers.
        qToLittleEndian<quint32>(0x900000, program.data() + 0x84 + 20 + 112 + 8);
        Imports imports;
        QVERIFY(binaries::parseImports(program, imports));
        QVERIFY(imports.libraries.isEmpty());
    }

    void mapsLibrariesToGraphicsApis_data()
    {
        QTest::addColumn<QStringList>("libraries");
        QTest::addColumn<int>("expected");
        QTest::newRow("DirectX 12") << QStringList{QStringLiteral("D3D12.dll")} << int(GraphicsApi::DirectX12);
        QTest::newRow("DirectX 11 via dxgi") << QStringList{QStringLiteral("dxgi.dll"), QStringLiteral("d3d11.dll")}
                                             << int(GraphicsApi::DirectX11);
        QTest::newRow("DirectX 10") << QStringList{QStringLiteral("d3d10_1.dll")} << int(GraphicsApi::DirectX10);
        QTest::newRow("DirectX 9 and 8") << QStringList{QStringLiteral("d3d9.dll"), QStringLiteral("d3d8.dll")}
                                         << int(GraphicsApi::DirectX9 | GraphicsApi::DirectX8);
        QTest::newRow("Vulkan on Windows") << QStringList{QStringLiteral("vulkan-1.dll")} << int(GraphicsApi::Vulkan);
        QTest::newRow("OpenGL on Windows") << QStringList{QStringLiteral("OPENGL32.dll")} << int(GraphicsApi::OpenGL);
        QTest::newRow("Vulkan on Linux") << QStringList{QStringLiteral("libvulkan.so.1")} << int(GraphicsApi::Vulkan);
        QTest::newRow("OpenGL on Linux") << QStringList{QStringLiteral("libGL.so.1"), QStringLiteral("libEGL.so.1")}
                                         << int(GraphicsApi::OpenGL);
        QTest::newRow("dxgi alone says nothing") << QStringList{QStringLiteral("dxgi.dll")} << 0;
        QTest::newRow("unrelated") << QStringList{QStringLiteral("kernel32.dll"), QStringLiteral("libc.so.6")} << 0;
    }

    void mapsLibrariesToGraphicsApis()
    {
        QFETCH(QStringList, libraries);
        QFETCH(int, expected);
        QCOMPARE(int(steam::apisFromLibraries(libraries).toInt()), expected);
    }

    void survivesDamagedPrograms()
    {
        const QList<QByteArray> samples{
            test::writePe(true, {QStringLiteral("d3d11.dll")}, {QStringLiteral("d3d12.dll")}),
            test::writePe(false, {QStringLiteral("d3d9.dll")}, {QStringLiteral("x.dll")}, true),
            test::writeElf(true, {QStringLiteral("libvulkan.so.1")}),
            test::writeElf(false, {QStringLiteral("libGL.so.1")}),
        };
        std::mt19937 random(11);
        for (int i = 0; i < 40000; ++i) {
            Imports imports;
            binaries::parseImports(test::mutate(samples[i % samples.size()], random), imports);
        }
    }
};

QTEST_GUILESS_MAIN(ImportsTest)
#include "tst_imports.moc"
