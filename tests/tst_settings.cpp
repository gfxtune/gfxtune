// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "settings/Availability.h"
#include "settings/SettingsStore.h"
#include "steam/Covers.h"
#include "support/Fixtures.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace gfxtune;
using namespace gfxtune::settings;
using steam::Game;
using steam::GraphicsApi;
using steam::GraphicsApis;
using steam::Runtime;

namespace
{

GameSettings sample()
{
    GameSettings s;
    s.anisotropicFiltering = 16;
    s.frameCapEnabled = true;
    s.frameCap = 141;
    s.verticalSync = VerticalSync::Fast;
    s.fsr4Upgrade = true;
    s.gamescope = true;
    s.upscaleFilter = UpscaleFilter::Nis;
    s.scalingMode = ScalingMode::Integer;
    s.renderScale = 67;
    s.sharpness = 3;
    return s;
}

Game game(Runtime runtime, GraphicsApis apis, bool fsr31 = false)
{
    Game g;
    g.appId = 1;
    g.runtime = runtime;
    g.apis = apis;
    g.fsr31Dx12 = fsr31;
    return g;
}

} // namespace

class SettingsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void namesRoundTrip()
    {
        for (int i = 0; i <= int(VerticalSync::Adaptive); ++i) {
            VerticalSync v{};
            QVERIFY(fromKey(toKey(VerticalSync(i)), v));
            QCOMPARE(int(v), i);
        }
        for (int i = 0; i <= int(UpscaleFilter::Pixel); ++i) {
            UpscaleFilter v{};
            QVERIFY(fromKey(toKey(UpscaleFilter(i)), v));
            QCOMPARE(int(v), i);
        }
        for (int i = 0; i <= int(ScalingMode::Stretch); ++i) {
            ScalingMode v{};
            QVERIFY(fromKey(toKey(ScalingMode(i)), v));
            QCOMPARE(int(v), i);
        }
        VerticalSync v = VerticalSync::On;
        QVERIFY(fromKey(QStringLiteral("  FAST "), v));
        QCOMPARE(v, VerticalSync::Fast);
        QVERIFY(!fromKey(QStringLiteral("sometimes"), v));
        QCOMPARE(v, VerticalSync::Fast);
    }

    void savesAndLoadsEverySetting()
    {
        QTemporaryDir dir;
        const SettingsStore store(dir.filePath(QStringLiteral("gfxtune")));
        QString error;
        QVERIFY2(store.save(1091, sample(), QStringLiteral("Ashfall Protocol"), &error), qPrintable(error));
        QStringList warnings;
        QCOMPARE(store.load(1091, &warnings), sample());
        QVERIFY(warnings.isEmpty());
        QCOMPARE(store.customizedGames(), QSet<quint32>{1091});

        // Readable by people: plain keys and words.
        QFile file(store.pathFor(1091));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray text = file.readAll();
        QVERIFY(text.contains("name=Ashfall Protocol"));
        QVERIFY(text.contains("vertical_sync=fast"));
        QVERIFY(text.contains("filter=nis"));
        // Nothing left behind by the atomic write.
        QCOMPARE(QDir(dir.filePath(QStringLiteral("gfxtune/games"))).entryList(QDir::Files), QStringList{QStringLiteral("1091.ini")});
    }

    void defaultSettingsLeaveNoFile()
    {
        QTemporaryDir dir;
        const SettingsStore store(dir.filePath(QStringLiteral("gfxtune")));
        QVERIFY(store.save(5, sample(), {}));
        QVERIFY(QFile::exists(store.pathFor(5)));
        QVERIFY(store.save(5, GameSettings{}, {}));
        QVERIFY(!QFile::exists(store.pathFor(5)));
        QCOMPARE(store.load(5), GameSettings{});
        QVERIFY(store.customizedGames().isEmpty());
    }

    void fallsBackOnDamagedValues()
    {
        QTemporaryDir dir;
        const SettingsStore store(dir.filePath(QStringLiteral("gfxtune")));
        QVERIFY(test::writeFile(store.pathFor(7), "[image]\nanisotropic_filtering=3\n"
                                                  "[frames]\nframe_cap_enabled=yes\nframe_cap=5\nvertical_sync=sometimes\n"
                                                  "[upscaling]\nfsr4_upgrade=true\n"
                                                  "[gamescope]\nenabled=TRUE\nrender_scale=66\nsharpness=21\nfilter=xbr\nscaling=fill\n"
                                                  "[other]\nunknown_key=1\n"));
        QStringList warnings;
        const GameSettings loaded = store.load(7, &warnings);
        const GameSettings defaults;
        QCOMPARE(loaded.anisotropicFiltering, defaults.anisotropicFiltering);
        QCOMPARE(loaded.frameCapEnabled, defaults.frameCapEnabled);
        QCOMPARE(loaded.frameCap, defaults.frameCap);
        QCOMPARE(loaded.verticalSync, defaults.verticalSync);
        QCOMPARE(loaded.renderScale, defaults.renderScale);
        QCOMPARE(loaded.sharpness, defaults.sharpness);
        QCOMPARE(loaded.upscaleFilter, defaults.upscaleFilter);
        // The good values survive.
        QVERIFY(loaded.fsr4Upgrade);
        QVERIFY(loaded.gamescope);
        QCOMPARE(loaded.scalingMode, ScalingMode::Fill);
        QCOMPARE(warnings.size(), 7);
    }

    void survivesGarbageFiles()
    {
        QTemporaryDir dir;
        const SettingsStore store(dir.filePath(QStringLiteral("gfxtune")));
        std::mt19937 random(3);
        for (int i = 0; i < 300; ++i) {
            QByteArray junk(int(random() % 400), '\0');
            for (char &c : junk) {
                c = char(random() % 256);
            }
            QVERIFY(test::writeFile(store.pathFor(9), junk));
            store.load(9);
        }
    }

    void ignoresStrayFilesWhenListing()
    {
        QTemporaryDir dir;
        const SettingsStore store(dir.filePath(QStringLiteral("gfxtune")));
        QVERIFY(store.save(12, sample(), {}));
        QVERIFY(test::writeFile(dir.filePath(QStringLiteral("gfxtune/games/notes.txt")), "x"));
        QVERIFY(test::writeFile(dir.filePath(QStringLiteral("gfxtune/games/abc.ini")), "x"));
        QVERIFY(test::writeFile(dir.filePath(QStringLiteral("gfxtune/games/0.ini")), "x"));
        QCOMPARE(store.customizedGames(), QSet<quint32>{12});
    }

    void reportsWriteFailures()
    {
        QTemporaryDir dir;
        // A file where the games folder should be.
        QVERIFY(test::writeFile(dir.filePath(QStringLiteral("gfxtune/games")), "not a folder"));
        const SettingsStore store(dir.filePath(QStringLiteral("gfxtune")));
        QString error;
        QVERIFY(!store.save(1, sample(), {}, &error));
        QVERIFY(!error.isEmpty());
    }

    void removesAllOfItsFilesAndNothingElse()
    {
        QTemporaryDir dir;
        QVERIFY(test::writeFile(dir.filePath(QStringLiteral("neighbour.txt")), "keep me"));
        const SettingsStore store(dir.filePath(QStringLiteral("gfxtune")));
        QVERIFY(store.save(1, sample(), {}));
        QVERIFY(store.save(2, sample(), {}));
        QVERIFY(store.removeAll());
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("gfxtune"))));
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("neighbour.txt"))));
        QVERIFY(store.removeAll()); // nothing left to remove is fine
    }

    void refusesToRemoveOtherFolders()
    {
        QTemporaryDir dir;
        QVERIFY(test::writeFile(dir.filePath(QStringLiteral("precious/file")), "x"));
        const SettingsStore store(dir.filePath(QStringLiteral("precious")));
        QString error;
        QVERIFY(!store.removeAll(&error));
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("precious/file"))));
    }

    void decidesWhatAppliesToAGame_data()
    {
        QTest::addColumn<int>("runtime");
        QTest::addColumn<int>("apis");
        QTest::addColumn<bool>("fsr31");
        QTest::addColumn<bool>("gamescopeOn");
        QTest::addColumn<bool>("gamescopeInstalled");
        QTest::addColumn<bool>("aniso");
        QTest::addColumn<bool>("frameCap");
        QTest::addColumn<bool>("fsr4");
        QTest::addColumn<bool>("gamescope");

        const int dx11 = int(GraphicsApi::DirectX11);
        const int dx12 = int(GraphicsApi::DirectX12);
        const int vulkan = int(GraphicsApi::Vulkan);
        const int gl = int(GraphicsApi::OpenGL);
        const int proton = int(Runtime::Proton);
        const int native = int(Runtime::Native);
        QTest::newRow("Proton DX11") << proton << dx11 << false << false << true << true << true << false << true;
        QTest::newRow("Proton DX12 with FSR 3.1") << proton << dx12 << true << false << true << true << false << true << true;
        QTest::newRow("Proton DX12, gamescope on") << proton << dx12 << false << true << true << true << true << false << true;
        QTest::newRow("Proton, gamescope on but missing") << proton << dx12 << false << true << false << true << false << false << false;
        QTest::newRow("Proton, graphics unknown") << proton << 0 << false << false << true << true << true << false << true;
        QTest::newRow("Proton DX11 and 12") << proton << (dx11 | dx12) << true << false << true << true << true << true << true;
        QTest::newRow("native Vulkan") << native << vulkan << false << false << true << true << false << false << true;
        QTest::newRow("native OpenGL") << native << gl << false << false << false << false << false << false << false;
        QTest::newRow("native OpenGL, gamescope on") << native << gl << false << true << true << false << true << false << true;
        QTest::newRow("native, FSR DLL present") << native << vulkan << true << false << true << true << false << false << true;
        QTest::newRow("unknown runtime") << int(Runtime::Unknown) << 0 << false << false << true << true << false << false << true;
    }

    void decidesWhatAppliesToAGame()
    {
        QFETCH(int, runtime);
        QFETCH(int, apis);
        QFETCH(bool, fsr31);
        QFETCH(bool, gamescopeOn);
        QFETCH(bool, gamescopeInstalled);
        QFETCH(bool, aniso);
        QFETCH(bool, frameCap);
        QFETCH(bool, fsr4);
        QFETCH(bool, gamescope);

        GameSettings s;
        s.gamescope = gamescopeOn;
        SystemInfo system;
        system.gamescopeInstalled = gamescopeInstalled;
        const Availability a = availabilityFor(game(Runtime(runtime), GraphicsApis::fromInt(GraphicsApis::Int(apis)), fsr31), s, system);
        QCOMPARE(a.anisotropicFiltering.available, aniso);
        QCOMPARE(a.verticalSync.available, aniso);
        QCOMPARE(a.frameCap.available, frameCap);
        QCOMPARE(a.fsr4Upgrade.available, fsr4);
        QCOMPARE(a.gamescope.available, gamescope);
        // Every unavailable setting says why.
        for (const Option &option : {a.anisotropicFiltering, a.verticalSync, a.frameCap, a.fsr4Upgrade, a.gamescope}) {
            QVERIFY(option.available || !option.note.isEmpty());
        }
    }

    void findsCoversInEveryLayout()
    {
        QTemporaryDir dir;
        const QString root = dir.path();
        const QString cache = root + QStringLiteral("/appcache/librarycache/");
        QCOMPARE(steam::findCover(root, 42, 10), QString());

        QVERIFY(test::writeFile(cache + QStringLiteral("10_library_600x900.jpg"), "x"));
        QCOMPARE(steam::findCover(root, 42, 10), cache + QStringLiteral("10_library_600x900.jpg"));

        const QString nested = cache + QStringLiteral("10/0123456789abcdef0123456789abcdef01234567/library_600x900.jpg");
        QVERIFY(test::writeFile(nested, "x"));
        QCOMPARE(steam::findCover(root, 42, 10), nested);

        // A second hashed copy makes the nested choice ambiguous.
        QVERIFY(test::writeFile(cache + QStringLiteral("10/fedcba9876543210fedcba9876543210fedcba98/library_600x900.jpg"), "x"));
        QCOMPARE(steam::findCover(root, 42, 10), cache + QStringLiteral("10_library_600x900.jpg"));
        // Folders that are not hashes are ignored.
        QVERIFY(test::writeFile(cache + QStringLiteral("11/not-a-hash/library_600x900.jpg"), "x"));
        QCOMPARE(steam::findCover(root, 42, 11), QString());

        QVERIFY(test::writeFile(cache + QStringLiteral("10/library_600x900.jpg"), "x"));
        QCOMPARE(steam::findCover(root, 42, 10), cache + QStringLiteral("10/library_600x900.jpg"));

        // A cover the user picked wins, but only for their account.
        const QString custom = root + QStringLiteral("/userdata/42/config/grid/10p.png");
        QVERIFY(test::writeFile(custom, "x"));
        QCOMPARE(steam::findCover(root, 42, 10), custom);
        QCOMPARE(steam::findCover(root, 0, 10), cache + QStringLiteral("10/library_600x900.jpg"));
        QCOMPARE(steam::findCover(root, 43, 10), cache + QStringLiteral("10/library_600x900.jpg"));
    }
};

QTEST_GUILESS_MAIN(SettingsTest)
#include "tst_settings.moc"
