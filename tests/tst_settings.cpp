// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "settings/Availability.h"
#include "settings/SettingsStore.h"
#include "steam/Covers.h"
#include "support/Fixtures.h"

#include <QDateTime>
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
    s.fsr4 = Fsr4::Off;
    s.gamescope = true;
    s.upscaleFilter = UpscaleFilter::Nis;
    s.scalingMode = ScalingMode::Integer;
    s.renderScale = 67;
    s.sharpness = 3;
    s.filters.enabled = false;
    s.filters.sharpening = 45;
    s.filters.brightness = -10;
    s.filters.contrast = 12;
    s.filters.gamma = 130;
    s.filters.saturation = -100;
    s.filters.vibrance = 100;
    s.filters.temperature = -35;
    s.filters.antiAliasing = 1;
    s.filtersRiskAccepted = true;
    s.lodBias = -75;
    s.measurePerformance = false;
    s.gameMode = true;
    s.mangoHud = true;
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

private:
    // Makes a file look written `seconds` ago, as Steam's saves would be.
    static bool setAge(const QString &path, int seconds)
    {
        QFile file(path);
        return file.open(QIODevice::ReadWrite)
            && file.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-seconds), QFileDevice::FileModificationTime);
    }

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
        QVERIFY(test::saveGame(store, 1091, sample(), QStringLiteral("Ashfall Protocol")));
        QStringList warnings;
        QCOMPARE(store.load(1091, &warnings), sample());
        QVERIFY(warnings.isEmpty());
        QCOMPARE(store.loadProfile(1091).changed.size(), settingKinds().size());

        // Readable by people: plain keys and words.
        QFile file(store.pathFor(1091));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray text = file.readAll();
        QVERIFY(text.contains("name=Ashfall Protocol"));
        QVERIFY(text.contains("format=2"));
        QVERIFY(text.contains("vertical_sync=fast"));
        QVERIFY(text.contains("filter=nis"));
        // The section the Vulkan layer reads while the game runs.
        QVERIFY(text.contains("[filters]"));
        QVERIFY(text.contains("sharpening=45"));
        QVERIFY(text.contains("temperature=-35"));
        QVERIFY(text.contains("anti_aliasing=smaa"));
        QVERIFY(text.contains("lod_bias=-75"));
        QVERIFY(text.contains("[performance]\nmeasure=false"));
        QVERIFY(text.contains("gamemode=true"));
        QVERIFY(text.contains("mangohud=true"));
        // Nothing left behind by the atomic write.
        QCOMPARE(QDir(dir.filePath(QStringLiteral("gfxtune/games"))).entryList(QDir::Files), QStringList{QStringLiteral("1091.ini")});
    }

    void defaultSettingsLeaveNoFile()
    {
        QTemporaryDir dir;
        const SettingsStore store(dir.filePath(QStringLiteral("gfxtune")));
        QVERIFY(test::saveGame(store, 5, sample()));
        QVERIFY(QFile::exists(store.pathFor(5)));
        QVERIFY(test::saveGame(store, 5, GameSettings{}));
        QVERIFY(!QFile::exists(store.pathFor(5)));
        QCOMPARE(store.load(5), GameSettings{});
        // Not following the global settings is the game's own, and kept.
        GameProfile own;
        own.usesGlobal = false;
        QVERIFY(store.saveProfile(5, own, {}));
        QVERIFY(QFile::exists(store.pathFor(5)));
        QCOMPARE(store.loadProfile(5), own);
        QVERIFY(store.saveProfile(5, GameProfile{}, {}));
        QVERIFY(!QFile::exists(store.pathFor(5)));
    }

    void fallsBackOnDamagedValues()
    {
        QTemporaryDir dir;
        const SettingsStore store(dir.filePath(QStringLiteral("gfxtune")));
        QVERIFY(test::writeFile(store.pathFor(7), "[image]\nanisotropic_filtering=3\n"
                                                  "[frames]\nframe_cap_enabled=yes\nframe_cap=5\nvertical_sync=sometimes\n"
                                                  "[upscaling]\nfsr4_upgrade=true\n"
                                                  "[gamescope]\nenabled=TRUE\nrender_scale=66\nsharpness=21\nfilter=xbr\nscaling=fill\n"
                                                  "[filters]\nenabled=maybe\nsharpening=500\ncontrast=x\ngamma=150\nanti_aliasing=fxaa\n"
                                                  "[performance]\nmeasure=often\n"
                                                  "[launch]\ngamemode=1\nmangohud=true\n"
                                                  "[other]\nunknown_key=1\nlod_bias=-50\n"));
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
        // The good values survive; the old FSR 4 switch reads as On.
        QCOMPARE(loaded.fsr4, Fsr4::On);
        QVERIFY(loaded.gamescope);
        QCOMPARE(loaded.scalingMode, ScalingMode::Fill);
        QCOMPARE(loaded.filters.enabled, defaults.filters.enabled);
        QCOMPARE(loaded.filters.sharpening, defaults.filters.sharpening);
        QCOMPARE(loaded.filters.contrast, defaults.filters.contrast);
        QCOMPARE(loaded.filters.gamma, 150);
        QCOMPARE(loaded.filters.antiAliasing, 0);
        QCOMPARE(loaded.measurePerformance, true);
        QCOMPARE(loaded.gameMode, false);
        QVERIFY(loaded.mangoHud);
        // A key in the wrong section is not read.
        QCOMPARE(loaded.lodBias, 0);
        QCOMPARE(warnings.size(), 13);
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

    void reportsWriteFailures()
    {
        QTemporaryDir dir;
        // A file where the games folder should be.
        QVERIFY(test::writeFile(dir.filePath(QStringLiteral("gfxtune/games")), "not a folder"));
        const SettingsStore store(dir.filePath(QStringLiteral("gfxtune")));
        QString error;
        QVERIFY(!store.saveProfile(1, profileFrom(sample()), {}, &error));
        QVERIFY(!error.isEmpty());
        error.clear();
        // A folder where the global file should be.
        QVERIFY(QDir().mkpath(store.globalPath()));
        QVERIFY(!store.saveGlobal(sample(), &error));
        QVERIFY(!error.isEmpty());
    }

    void removesAllOfItsFilesAndNothingElse()
    {
        QTemporaryDir dir;
        QVERIFY(test::writeFile(dir.filePath(QStringLiteral("neighbour.txt")), "keep me"));
        const SettingsStore store(dir.filePath(QStringLiteral("gfxtune")));
        QVERIFY(test::saveGame(store, 1, sample()));
        QVERIFY(test::saveGame(store, 2, sample()));
        QVERIFY(store.saveGlobal(sample()));
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
        QTest::addColumn<bool>("vsync");
        // The cap with performance measuring on (the default: gfxtune's layer
        // caps every Vulkan game), and with it off.
        QTest::addColumn<bool>("frameCap");
        QTest::addColumn<bool>("frameCapWithoutLayer");
        QTest::addColumn<bool>("fsr4");
        QTest::addColumn<bool>("gamescope");

        const int dx11 = int(GraphicsApi::DirectX11);
        const int dx12 = int(GraphicsApi::DirectX12);
        const int vulkan = int(GraphicsApi::Vulkan);
        const int gl = int(GraphicsApi::OpenGL);
        const int proton = int(Runtime::Proton);
        const int native = int(Runtime::Native);
        QTest::newRow("Proton DX11") << proton << dx11 << false << false << true << true << true << true << true << false << true;
        QTest::newRow("Proton DX12 with FSR 3.1") << proton << dx12 << true << false << true << true << true << true << true << true << true;
        QTest::newRow("Proton DX12, gamescope on") << proton << dx12 << false << true << true << true << true << true << true << false << true;
        QTest::newRow("Proton, gamescope on but missing") << proton << dx12 << false << true << false << true << true << true << true << false << false;
        QTest::newRow("Proton Vulkan, no gamescope") << proton << vulkan << false << false << false << true << true << true << false << false << false;
        QTest::newRow("Proton, graphics unknown") << proton << 0 << false << false << true << true << true << true << true << false << true;
        QTest::newRow("Proton DX11 and 12") << proton << (dx11 | dx12) << true << false << true << true << true << true << true << true << true;
        QTest::newRow("native Vulkan") << native << vulkan << false << false << true << true << true << true << false << false << true;
        // Mesa's own switch reaches OpenGL vertical sync; nothing else does.
        QTest::newRow("native OpenGL") << native << gl << false << false << false << false << true << false << false << false << false;
        // Windows games often link OpenGL without using it: only a note.
        QTest::newRow("Proton, OpenGL guess") << proton << gl << false << false << true << true << true << true << true << false << true;
        QTest::newRow("native OpenGL, gamescope on") << native << gl << false << true << true << false << true << true << true << false << true;
        QTest::newRow("native, FSR DLL present") << native << vulkan << true << false << true << true << true << true << false << false << true;
        QTest::newRow("unknown runtime") << int(Runtime::Unknown) << 0 << false << false << true << true << true << false << false << false << true;
    }

    void decidesWhatAppliesToAGame()
    {
        QFETCH(int, runtime);
        QFETCH(int, apis);
        QFETCH(bool, fsr31);
        QFETCH(bool, gamescopeOn);
        QFETCH(bool, gamescopeInstalled);
        QFETCH(bool, aniso);
        QFETCH(bool, vsync);
        QFETCH(bool, frameCap);
        QFETCH(bool, frameCapWithoutLayer);
        QFETCH(bool, fsr4);
        QFETCH(bool, gamescope);

        GameSettings s;
        s.gamescope = gamescopeOn;
        SystemInfo system;
        system.gamescopeInstalled = gamescopeInstalled;
        const Game g = game(Runtime(runtime), GraphicsApis::fromInt(GraphicsApis::Int(apis)), fsr31);
        const Availability a = availabilityFor(g, s, system);
        QCOMPARE(a.anisotropicFiltering.available, aniso);
        QCOMPARE(a.verticalSync.available, vsync);
        QCOMPARE(a.frameCap.available, frameCap);
        QCOMPARE(a.fsr4.available, fsr4);
        QCOMPARE(a.gamescope.available, gamescope);
        // Every unavailable setting says why.
        for (const Option &option : {a.anisotropicFiltering, a.verticalSync, a.frameCap, a.fsr4, a.gamescope, a.lodBias,
                                     a.measurePerformance, a.gameMode, a.mangoHud, a.filters}) {
            QVERIFY(option.available || !option.note.isEmpty());
        }

        s.measurePerformance = false;
        QCOMPARE(availabilityFor(g, s, system).frameCap.available, frameCapWithoutLayer);
    }

    void capsProtonGamesInsideTheGame()
    {
        SystemInfo system;
        system.gamescopeInstalled = true;
        GameSettings withGamescope;
        withGamescope.gamescope = true;

        // DirectX under Proton: the in-game cap, gamescope or not.
        Availability a = availabilityFor(game(Runtime::Proton, GraphicsApi::DirectX11), withGamescope, system);
        QVERIFY(a.frameCap.available);
        QCOMPARE(a.frameCap.note, QString());

        // Vulkan used directly under Proton: only gfxtune's layer reaches it.
        a = availabilityFor(game(Runtime::Proton, GraphicsApi::Vulkan), withGamescope, system);
        QVERIFY(a.frameCap.available);
        withGamescope.measurePerformance = false;
        a = availabilityFor(game(Runtime::Proton, GraphicsApi::Vulkan), withGamescope, system);
        QVERIFY(!a.frameCap.available);
        QVERIFY(a.frameCap.note.contains(QLatin1String("measuring")));

        // Outside Proton without the layer only gamescope can, and only with
        // vertical sync on.
        a = availabilityFor(game(Runtime::Native, GraphicsApi::Vulkan), withGamescope, system);
        QVERIFY(a.frameCap.available);
        QVERIFY(a.frameCap.note.contains(QLatin1String("vertical sync")));
    }

    void followsWhatTheProtonDoesWithFsr4()
    {
        SystemInfo system;
        Game g = game(Runtime::Proton, GraphicsApi::DirectX12, true);

        g.tool.fsr4 = steam::Fsr4Support::BuiltIn;
        Availability a = availabilityFor(g, GameSettings{}, system);
        QVERIFY(a.fsr4.available);
        QVERIFY(a.fsr4.note.contains(QLatin1String("on by itself")));

        g.tool.fsr4 = steam::Fsr4Support::OnRequest;
        a = availabilityFor(g, GameSettings{}, system);
        QVERIFY(a.fsr4.available);
        QVERIFY(a.fsr4.note.contains(QLatin1String("unless FSR 4 is set to On")));

        // Steam's default Proton: the files cannot be read, so all choices stay.
        g.tool.fsr4 = steam::Fsr4Support::Unknown;
        a = availabilityFor(g, GameSettings{}, system);
        QVERIFY(a.fsr4.available);
        QVERIFY(a.fsr4.note.contains(QLatin1String("cannot tell")));

        g.tool.fsr4 = steam::Fsr4Support::None;
        a = availabilityFor(g, GameSettings{}, system);
        QVERIFY(!a.fsr4.available);
        QVERIFY(a.fsr4.note.contains(QLatin1String("cannot use FSR 4")));

        // The user's own override for AMD's library wins.
        g.tool.fsr4 = steam::Fsr4Support::BuiltIn;
        g.launchOptions = QStringLiteral("WINEDLLOVERRIDES=\"amdxcffx64=n\" %command%");
        QVERIFY(!availabilityFor(g, GameSettings{}, system).fsr4.available);
    }

    void readsOldAndNewFsr4Choices()
    {
        QTemporaryDir dir;
        const SettingsStore store(dir.filePath(QStringLiteral("gfxtune")));
        const auto loadFrom = [&store](const QByteArray &contents) {
            if (!test::writeFile(store.pathFor(9), "[frames]\nframe_cap_enabled=true\n" + contents)) {
                return Fsr4(-1);
            }
            return store.load(9).fsr4;
        };
        QCOMPARE(loadFrom("[upscaling]\nfsr4_upgrade=false\n"), Fsr4::ProtonDefault);
        QCOMPARE(loadFrom("[upscaling]\nfsr4_upgrade=true\n"), Fsr4::On);
        QCOMPARE(loadFrom("[upscaling]\nfsr4=off\n"), Fsr4::Off);
        QCOMPARE(loadFrom("[upscaling]\nfsr4=proton\n"), Fsr4::ProtonDefault);
        // The new key wins over the old one; a damaged one falls back.
        QCOMPARE(loadFrom("[upscaling]\nfsr4_upgrade=true\nfsr4=off\n"), Fsr4::Off);
        QCOMPARE(loadFrom("[upscaling]\nfsr4=maybe\n"), Fsr4::ProtonDefault);

        // Saving writes only the new key.
        GameSettings s;
        s.fsr4 = Fsr4::Off;
        QVERIFY(test::saveGame(store, 9, s));
        QFile file(store.pathFor(9));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray saved = file.readAll();
        QVERIFY(saved.contains("fsr4=off"));
        QVERIFY(!saved.contains("fsr4_upgrade"));
    }

    // gfxtune-run goes into a game when it has something to do there:
    // measuring (on by default), or any setting.
    void decidesWhenGfxtuneRunIsNeeded()
    {
        const GameFacts none;
        const GameFacts antiCheat{{QStringLiteral("Easy Anti-Cheat")}, false};
        const GameFacts openGl{{}, true};
        GameSettings s;
        QVERIFY(!s.affectsGame());
        QVERIFY(needsGfxtuneRun(s, none)); // measuring
        QVERIFY(!needsGfxtuneRun(s, antiCheat));
        QVERIFY(!needsGfxtuneRun(s, openGl));
        s.filtersRiskAccepted = true;
        QVERIFY(!s.affectsGame()); // the consent alone is no setting
        QVERIFY(needsGfxtuneRun(s, antiCheat));
        s.measurePerformance = false;
        QVERIFY(!s.affectsGame());
        QVERIFY(!needsGfxtuneRun(s, none));
        s.gameMode = true;
        QVERIFY(needsGfxtuneRun(s, openGl));
        QVERIFY(needsGfxtuneRun(s, antiCheat));

        // Filters count only where the layer can go in.
        GameSettings filtered;
        filtered.measurePerformance = false;
        filtered.filters.sharpening = 40;
        QVERIFY(needsGfxtuneRun(filtered, none));
        QVERIFY(!needsGfxtuneRun(filtered, antiCheat)); // locked
        QVERIFY(!needsGfxtuneRun(filtered, openGl));    // out of reach
        filtered.filtersRiskAccepted = true;
        QVERIFY(needsGfxtuneRun(filtered, antiCheat));
        // Settings applied from outside the game reach games with anti-cheat.
        GameSettings outside;
        outside.measurePerformance = false;
        outside.anisotropicFiltering = 16;
        QVERIFY(needsGfxtuneRun(outside, antiCheat));
        outside = GameSettings{};
        outside.measurePerformance = false;
        outside.frameCapEnabled = true;
        QVERIFY(needsGfxtuneRun(outside, antiCheat)); // through DXVK and VKD3D-Proton
    }

    void onlyOffersListedTextureDetail()
    {
        QTemporaryDir dir;
        const SettingsStore store(dir.filePath(QStringLiteral("gfxtune")));
        // One file each: QSettings rereads a file only when its size or
        // time changed, which a quick rewrite of the same length may not.
        quint32 appId = 100;
        for (int level : GameSettings::LodBiasLevels) {
            QVERIFY(test::writeFile(store.pathFor(++appId), "[image]\nlod_bias=" + QByteArray::number(level) + "\n"));
            QCOMPARE(store.load(appId).lodBias, level);
        }
        for (const QByteArray &bad : {QByteArray("-30"), QByteArray("-300"), QByteArray("1000"), QByteArray("-0.5")}) {
            QVERIFY(test::writeFile(store.pathFor(++appId), "[image]\nlod_bias=" + bad + "\n"));
            QCOMPARE(store.load(appId).lodBias, 0);
        }
        // Supersampling sizes are kept too.
        for (int scale : {125, 150, 200}) {
            QVERIFY(test::writeFile(store.pathFor(++appId), "[gamescope]\nenabled=true\nrender_scale=" + QByteArray::number(scale) + "\n"));
            QCOMPARE(store.load(appId).renderScale, scale);
            QVERIFY(store.load(appId).supersampling());
        }
        QVERIFY(!GameSettings{}.supersampling());
        GameSettings withoutGamescope;
        withoutGamescope.renderScale = 150; // the size gamescope would use
        QVERIFY(!withoutGamescope.supersampling());
    }

    // What the scan found about a game, kept in its settings file for
    // gfxtune-run.
    void keepsTheGamesFacts()
    {
        QTemporaryDir dir;
        const SettingsStore store(dir.filePath(QStringLiteral("gfxtune")));
        const GameFacts facts{{QStringLiteral("Easy Anti-Cheat"), QStringLiteral("BattlEye")}, true};
        QCOMPARE(store.loadFacts(4), GameFacts{});
        // Every game gets gfxtune-run, so a game without settings of its
        // own gets a file for them too.
        QVERIFY(store.saveFacts(4, facts, QStringLiteral("Hollow Orbit")));
        QCOMPARE(store.loadFacts(4), facts);
        QVERIFY(store.loadProfile(4).isEmpty());
        QCOMPARE(store.load(4), GameSettings{});

        GameSettings s;
        s.frameCapEnabled = true;
        QVERIFY(test::saveGame(store, 4, s, QStringLiteral("Hollow Orbit")));
        QCOMPARE(store.loadFacts(4), facts);
        // Saving the settings keeps them, and they are not settings.
        s.frameCap = 90;
        QVERIFY(test::saveGame(store, 4, s, QStringLiteral("Hollow Orbit")));
        QCOMPARE(store.loadFacts(4), facts);
        QCOMPARE(store.load(4), s);

        // Unchanged facts do not touch the file.
        QFileInfo before(store.pathFor(4));
        const QDateTime written = before.lastModified();
        QVERIFY(setAge(store.pathFor(4), 100));
        QVERIFY(store.saveFacts(4, facts));
        QVERIFY(QFileInfo(store.pathFor(4)).lastModified() < written);

        QVERIFY(store.saveFacts(4, GameFacts{}));
        QCOMPARE(store.loadFacts(4), GameFacts{});
        QFile file(store.pathFor(4));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray text = file.readAll();
        QVERIFY(!text.contains("anti_cheat"));
        QVERIFY(text.contains("name=Hollow Orbit")); // kept from before
        // With neither settings nor facts, the file goes.
        QVERIFY(test::saveGame(store, 4, GameSettings{}));
        QVERIFY(!QFile::exists(store.pathFor(4)));
        QVERIFY(store.saveFacts(4, facts));
        QVERIFY(store.saveFacts(4, GameFacts{}));
        QVERIFY(!QFile::exists(store.pathFor(4)));

        // Blank names in a hand-edited file are skipped.
        QVERIFY(test::writeFile(store.pathFor(5), "[game]\nanti_cheat=\" \", BattlEye\nopengl_only=TRUE\n"));
        QCOMPARE(store.loadFacts(5), (GameFacts{{QStringLiteral("BattlEye")}, true}));
    }

    void decidesWhereTextureDetailWorks()
    {
        SystemInfo system;
        GameSettings measuring; // the layer loads
        GameSettings plain;
        plain.measurePerformance = false;
        const auto lod = [&system](const Game &g, const GameSettings &s) { return availabilityFor(g, s, system).lodBias; };

        QVERIFY(lod(game(Runtime::Native, GraphicsApi::Vulkan), measuring).available);
        QVERIFY(lod(game(Runtime::Native, GraphicsApi::Vulkan), measuring).note.contains(QLatin1String("next time")));
        QVERIFY(!lod(game(Runtime::Native, GraphicsApi::Vulkan), plain).available);
        QVERIFY(lod(game(Runtime::Native, GraphicsApi::Vulkan), plain).note.contains(QLatin1String("Needs performance measuring")));
        // DXVK can set it for DirectX 9 to 11 without the layer.
        QVERIFY(lod(game(Runtime::Proton, GraphicsApi::DirectX11), plain).available);
        QVERIFY(lod(game(Runtime::Proton, GraphicsApi::DirectX11), plain).note.contains(QLatin1String("DirectX 9 to 11")));
        QVERIFY(!lod(game(Runtime::Native, GraphicsApi::OpenGL), measuring).available);

        Game own = game(Runtime::Proton, GraphicsApi::DirectX11);
        own.launchOptions = QStringLiteral("DXVK_CONFIG=\"d3d11.samplerLodBias = -1\" %command%");
        QVERIFY(!lod(own, measuring).available);

        // Locked by anti-cheat, the layer does not load.
        Game locked = game(Runtime::Native, GraphicsApi::Vulkan);
        locked.antiCheat = {QStringLiteral("Easy Anti-Cheat")};
        QVERIFY(!lod(locked, measuring).available);
        QVERIFY(lod(locked, measuring).note.contains(QLatin1String("accept the risk")));
        GameSettings capped;
        capped.frameCapEnabled = true;
        system.gamescopeInstalled = true;
        QVERIFY(availabilityFor(locked, capped, system).frameCap.note.contains(QLatin1String("accept the risk")));
        locked.runtime = Runtime::Proton;
        QVERIFY(availabilityFor(locked, capped, system).frameCap.note.contains(QLatin1String("accept the risk")));
    }

    void decidesWhereMeasuringWorks()
    {
        SystemInfo system;
        const auto measure = [&system](const Game &g) { return availabilityFor(g, GameSettings{}, system).measurePerformance; };
        QVERIFY(measure(game(Runtime::Native, GraphicsApi::Vulkan)).available);
        QVERIFY(measure(game(Runtime::Proton, GraphicsApi::DirectX12)).available);
        QVERIFY(measure(game(Runtime::Proton, GraphicsApi::OpenGL)).available); // may still draw through DXVK
        QVERIFY(!measure(game(Runtime::Native, GraphicsApi::OpenGL)).available);
        QVERIFY(measure(game(Runtime::Native, GraphicsApi::OpenGL)).note.contains(QLatin1String("OpenGL")));
        QVERIFY(!measure(game(Runtime::Unknown, {})).available);

        Game locked = game(Runtime::Proton, GraphicsApi::DirectX11);
        locked.antiCheat = {QStringLiteral("BattlEye")};
        const Availability a = availabilityFor(locked, GameSettings{}, system);
        QVERIFY(a.filtersLocked);
        QVERIFY(a.measurePerformance.available);
        QCOMPARE(a.measurePerformance.note, QStringLiteral("Locked until you accept the risk under In-game filters."));
        QVERIFY(a.filtersWarning.contains(QLatin1String("performance measuring")));
    }

    void offersGameModeAndMangoHudWhenInstalled()
    {
        SystemInfo none;
        Game g = game(Runtime::Proton, GraphicsApi::DirectX11);
        Availability a = availabilityFor(g, GameSettings{}, none);
        QVERIFY(!a.gameMode.available);
        QCOMPARE(a.gameMode.note, QStringLiteral("GameMode is not installed."));
        QVERIFY(!a.mangoHud.available);
        QCOMPARE(a.mangoHud.note, QStringLiteral("MangoHud is not installed."));

        SystemInfo system;
        system.gameModeInstalled = true;
        system.mangoHudInstalled = true;
        system.gamescopeInstalled = true;
        a = availabilityFor(g, GameSettings{}, system);
        QVERIFY(a.gameMode.available);
        QVERIFY(a.mangoHud.available);
        QVERIFY(a.mangoHud.note.contains(QLatin1String("Right Shift+F12")));

        // Started by the game's own launch options already.
        g.launchOptions = QStringLiteral("gamemoderun mangohud %command%");
        a = availabilityFor(g, GameSettings{}, system);
        QVERIFY(!a.gameMode.available);
        QVERIFY(!a.mangoHud.available);
        g.launchOptions = QStringLiteral("MANGOHUD=1 %command%");
        QVERIFY(!availabilityFor(g, GameSettings{}, system).mangoHud.available);

        // Inside gamescope, MangoHud needs gamescope's mangoapp.
        g.launchOptions.clear();
        GameSettings inGamescope;
        inGamescope.gamescope = true;
        a = availabilityFor(g, inGamescope, system);
        QVERIFY(!a.mangoHud.available);
        QVERIFY(a.mangoHud.note.contains(QLatin1String("mangoapp, which is not installed")));
        system.mangoAppInstalled = true;
        a = availabilityFor(g, inGamescope, system);
        QVERIFY(a.mangoHud.available);
        QVERIFY(a.mangoHud.note.contains(QLatin1String("mangoapp")));
        g.launchOptions = QStringLiteral("gamescope -f -- %command%");
        a = availabilityFor(g, GameSettings{}, system);
        QVERIFY(!a.mangoHud.available);
        QVERIFY(a.mangoHud.note.contains(QLatin1String("--mangoapp")));
    }

    void explainsSupersampling()
    {
        SystemInfo system;
        system.gamescopeInstalled = true;
        GameSettings s;
        s.gamescope = true;
        s.renderScale = 150;
        const Availability a = availabilityFor(game(Runtime::Proton, GraphicsApi::DirectX11), s, system);
        QVERIFY(a.gamescope.available);
        QVERIFY(a.gamescope.note.startsWith(QLatin1String("Supersampling")));
        s.renderScale = 67;
        QCOMPARE(availabilityFor(game(Runtime::Proton, GraphicsApi::DirectX11), s, system).gamescope.note, QString());
    }

    // Every setting is in the list of settings, once: copying all of them
    // copies everything but the consent, and no two share a field.
    void listsEverySetting()
    {
        GameSettings all = sample();
        all.filtersRiskAccepted = false;
        GameSettings copied;
        QSet<QString> ids;
        QSet<QString> keys;
        for (const SettingKind &kind : settingKinds()) {
            QVERIFY2(!ids.contains(kind.id), qPrintable(kind.id));
            ids.insert(kind.id);
            for (const QString &key : kind.keys) {
                QVERIFY2(!keys.contains(key), qPrintable(key));
                keys.insert(key);
            }
            GameSettings one;
            kind.copy(all, one);
            QVERIFY2(one != GameSettings{}, qPrintable(kind.id)); // copies something
            QVERIFY(!sameSetting(kind, one, GameSettings{}));
            for (const SettingKind &other : settingKinds()) {
                if (other.id != kind.id) {
                    QVERIFY2(sameSetting(other, one, GameSettings{}), qPrintable(kind.id + QLatin1String(" touches ") + other.id));
                }
            }
            kind.copy(all, copied);
        }
        QCOMPARE(copied, all);
        QCOMPARE(findSetting(QStringLiteral("frameCap"))->keys.size(), 2);
        QVERIFY(!findSetting(QStringLiteral("nothing")));
    }

    void savesTheGlobalSettings()
    {
        QTemporaryDir dir;
        const SettingsStore store(dir.filePath(QStringLiteral("gfxtune")));
        QCOMPARE(store.loadGlobal(), GameSettings{});
        GameSettings global = sample();
        QVERIFY(store.saveGlobal(global));
        global.filtersRiskAccepted = false; // only ever a game's own
        QCOMPARE(store.loadGlobal(), global);
        QFile file(store.globalPath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray text = file.readAll();
        file.close();
        QVERIFY(text.contains("[filters]"));
        QVERIFY(text.contains("sharpening=45"));
        QVERIFY(!text.contains("risk_accepted"));
        QVERIFY(store.saveGlobal(GameSettings{}));
        QVERIFY(!QFile::exists(store.globalPath()));
        QVERIFY(store.saveGlobal(GameSettings{})); // nothing to remove is fine
        GameSettings consentOnly;
        consentOnly.filtersRiskAccepted = true;
        QVERIFY(store.saveGlobal(consentOnly));
        QVERIFY(!QFile::exists(store.globalPath()));

        // Damaged values fall back to the defaults.
        QStringList warnings;
        QVERIFY(test::writeFile(store.globalPath(), "[image]\nanisotropic_filtering=5\n[frames]\nframe_cap_enabled=true\nframe_cap=141\n"));
        const GameSettings loaded = store.loadGlobal(&warnings);
        QCOMPARE(loaded.anisotropicFiltering, 0);
        QVERIFY(loaded.frameCapEnabled);
        QCOMPARE(loaded.frameCap, 141);
        QCOMPARE(warnings.size(), 1);
    }

    // A game gets the global settings, with what it changed; a game that
    // stopped using them gets the defaults with its changes.
    void appliesAGamesChangesOverTheGlobalSettings()
    {
        QTemporaryDir dir;
        const SettingsStore store(dir.filePath(QStringLiteral("gfxtune")));
        GameSettings global;
        global.anisotropicFiltering = 16;
        global.frameCapEnabled = true;
        global.frameCap = 141;
        global.filters.saturation = 30;
        QVERIFY(store.saveGlobal(global));

        GameProfile profile;
        profile.changed = {QStringLiteral("frameCap"), QStringLiteral("filterContrast")};
        profile.own.frameCapEnabled = true;
        profile.own.frameCap = 90;
        profile.own.filters.contrast = -10;
        profile.own.anisotropicFiltering = 2; // not changed: ignored
        QVERIFY(store.saveProfile(7, profile, QStringLiteral("Stonewake")));
        QCOMPARE(store.loadProfile(7), profile);
        GameSettings s = store.load(7);
        QCOMPARE(s.anisotropicFiltering, 16);
        QCOMPARE(s.frameCap, 90);
        QCOMPARE(s.filters.saturation, 30);
        QCOMPARE(s.filters.contrast, -10);

        // The file holds only the changes, for the layer to read over the global file.
        QFile file(store.pathFor(7));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray text = file.readAll();
        file.close();
        QVERIFY(text.contains("frame_cap=90"));
        QVERIFY(text.contains("frame_cap_enabled=true"));
        QVERIFY(text.contains("contrast=-10"));
        QVERIFY(!text.contains("anisotropic_filtering"));
        QVERIFY(!text.contains("saturation"));
        QVERIFY(!text.contains("uses_global"));

        // Later global changes reach what the game did not change.
        global.anisotropicFiltering = 8;
        global.frameCap = 60;
        QVERIFY(store.saveGlobal(global));
        s = store.load(7);
        QCOMPARE(s.anisotropicFiltering, 8);
        QCOMPARE(s.frameCap, 90);

        // Out of the global settings: left alone, apart from its changes.
        profile.usesGlobal = false;
        QVERIFY(store.saveProfile(7, profile, QStringLiteral("Stonewake")));
        QCOMPARE(store.loadProfile(7), profile);
        s = store.load(7);
        QCOMPARE(s.anisotropicFiltering, 0);
        QCOMPARE(s.filters.saturation, 0);
        QVERIFY(!s.measurePerformance);
        QCOMPARE(s.frameCap, 90);
        QCOMPARE(s.filters.contrast, -10);
        QFile again(store.pathFor(7));
        QVERIFY(again.open(QIODevice::ReadOnly));
        QVERIFY(again.readAll().contains("uses_global=false"));

        // A damaged change falls back to the global setting.
        QVERIFY(test::writeFile(store.pathFor(8), "[game]\nformat=2\n[image]\nanisotropic_filtering=3\nlod_bias=-50\n"));
        QStringList warnings;
        const GameProfile damaged = store.loadProfile(8, &warnings);
        QCOMPARE(damaged.changed, QSet<QString>{QStringLiteral("lodBias")});
        QCOMPARE(warnings.size(), 1);
        QCOMPARE(store.load(8).anisotropicFiltering, 8);
        QCOMPARE(store.load(8).lodBias, -50);
    }

    // Files from before global settings held every setting: the ones at
    // gfxtune's default were not chosen for the game and follow the global
    // settings now; the rest stay the game's own.
    void readsFilesFromBeforeGlobalSettings()
    {
        QTemporaryDir dir;
        const SettingsStore store(dir.filePath(QStringLiteral("gfxtune")));
        QVERIFY(test::writeFile(store.pathFor(1001), "[game]\nname=Ashfall Protocol\nsteam_app_id=1001\n"
                                                     "[image]\nanisotropic_filtering=0\nlod_bias=0\n"
                                                     "[frames]\nframe_cap_enabled=true\nframe_cap=60\nvertical_sync=game\n"
                                                     "[upscaling]\nfsr4=proton\n"
                                                     "[gamescope]\nenabled=false\nfilter=fsr\nscaling=auto\nrender_scale=100\nsharpness=10\n"
                                                     "[filters]\nenabled=true\nsharpening=40\nbrightness=0\ncontrast=0\ngamma=100\nsaturation=0\n"
                                                     "vibrance=0\ntemperature=0\nanti_aliasing=off\nrisk_accepted=true\n"
                                                     "[performance]\nmeasure=true\n[launch]\ngamemode=false\nmangohud=false\n"));
        const GameProfile profile = store.loadProfile(1001);
        QCOMPARE(profile.changed, (QSet<QString>{QStringLiteral("frameCap"), QStringLiteral("filterSharpening")}));
        QVERIFY(profile.usesGlobal);
        QVERIFY(profile.own.filtersRiskAccepted);

        GameSettings global;
        global.anisotropicFiltering = 16;
        QVERIFY(store.saveGlobal(global));
        const GameSettings s = store.load(1001);
        QCOMPARE(s.anisotropicFiltering, 16);
        QVERIFY(s.frameCapEnabled);
        QCOMPARE(s.filters.sharpening, 40);

        // Saved again, it holds only its changes.
        QVERIFY(store.saveProfile(1001, profile, QStringLiteral("Ashfall Protocol")));
        QFile file(store.pathFor(1001));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray text = file.readAll();
        QVERIFY(text.contains("format=2"));
        QVERIFY(!text.contains("anisotropic_filtering"));
        QVERIFY(text.contains("sharpening=40"));
        QCOMPARE(store.loadProfile(1001), profile);
    }

    void saysWhatTheGlobalSettingsReach()
    {
        SystemInfo none;
        GameSettings s;
        Availability a = availabilityForAll(s, none);
        QVERIFY(a.anisotropicFiltering.available);
        QVERIFY(a.filters.available);
        QVERIFY(!a.filtersLocked);
        QVERIFY(a.filters.note.contains(QLatin1String("anti-cheat")));
        QVERIFY(a.measurePerformance.available);
        QVERIFY(!a.gamescope.available);
        QVERIFY(!a.gameMode.available);
        QVERIFY(!a.mangoHud.available);

        SystemInfo all;
        all.gamescopeInstalled = true;
        all.gameModeInstalled = true;
        all.mangoHudInstalled = true;
        a = availabilityForAll(s, all);
        QVERIFY(a.gamescope.available);
        QVERIFY(a.gameMode.available);
        QVERIFY(a.mangoHud.available);
        s.gamescope = true;
        s.renderScale = 150;
        a = availabilityForAll(s, all);
        QVERIFY(a.gamescope.note.startsWith(QLatin1String("Supersampling")));
        QVERIFY(!a.mangoHud.available); // inside gamescope without mangoapp
        all.mangoAppInstalled = true;
        QVERIFY(availabilityForAll(s, all).mangoHud.available);
    }

    void decidesWhereFiltersWork()
    {
        SystemInfo system;
        const auto filtersFor = [&system](Runtime runtime, GraphicsApis apis) { return availabilityFor(game(runtime, apis), GameSettings{}, system).filters; };
        QVERIFY(!filtersFor(Runtime::Native, GraphicsApi::OpenGL).available);
        QVERIFY(filtersFor(Runtime::Native, GraphicsApi::OpenGL).note.contains(QLatin1String("OpenGL")));
        QVERIFY(!filtersFor(Runtime::Unknown, {}).available);
        QVERIFY(filtersFor(Runtime::Native, GraphicsApi::Vulkan).available);
        QVERIFY(filtersFor(Runtime::Native, GraphicsApi::Vulkan).note.contains(QLatin1String("HDR")));
        QVERIFY(filtersFor(Runtime::Proton, GraphicsApi::DirectX11).available);
        // Not sure what it draws with: offered, with a word of caution.
        QVERIFY(filtersFor(Runtime::Native, {}).available);
        QVERIFY(filtersFor(Runtime::Native, {}).note.contains(QLatin1String("Only shows")));
        QVERIFY(filtersFor(Runtime::Proton, GraphicsApi::OpenGL).available);
        QVERIFY(filtersFor(Runtime::Proton, GraphicsApi::OpenGL).note.contains(QLatin1String("Only shows")));
    }

    void locksFiltersUntilTheRiskIsAccepted()
    {
        SystemInfo system;
        Game g = game(Runtime::Proton, GraphicsApi::DirectX11);
        g.name = QStringLiteral("Hollow Orbit");
        Availability a = availabilityFor(g, GameSettings{}, system);
        QVERIFY(!a.filtersLocked);

        g.antiCheat = {QStringLiteral("Easy Anti-Cheat"), QStringLiteral("BattlEye")};
        a = availabilityFor(g, GameSettings{}, system);
        QVERIFY(a.filters.available);
        QVERIFY(a.filtersLocked);
        QVERIFY(a.filtersWarning.startsWith(QLatin1String("Hollow Orbit ships Easy Anti-Cheat and BattlEye.")));
        QVERIFY(a.filtersWarning.contains(QLatin1String("your responsibility")));

        GameSettings accepted;
        accepted.filtersRiskAccepted = true;
        QVERIFY(!availabilityFor(g, accepted, system).filtersLocked);

        // Nothing to lock where the filters cannot work anyway.
        g = game(Runtime::Native, GraphicsApi::OpenGL);
        g.antiCheat = {QStringLiteral("Valve Anti-Cheat")};
        QVERIFY(!availabilityFor(g, GameSettings{}, system).filtersLocked);
    }

    void onlyNotesAnOpenGlGuessUnderProton()
    {
        SystemInfo system;
        system.gamescopeInstalled = true;
        const Availability a = availabilityFor(game(Runtime::Proton, GraphicsApi::OpenGL), GameSettings{}, system);
        QVERIFY(a.anisotropicFiltering.available);
        QVERIFY(a.anisotropicFiltering.note.contains(QLatin1String("OpenGL")));
        QVERIFY(a.verticalSync.available);
    }

    void respectsTheGamesOwnLaunchOptions()
    {
        SystemInfo system;
        system.gamescopeInstalled = true;
        Game g = game(Runtime::Proton, GraphicsApi::DirectX12, true);

        g.launchOptions = QStringLiteral("gamescope -w 1280 -h 720 -f -- %command%");
        Availability a = availabilityFor(g, GameSettings{}, system);
        QVERIFY(!a.gamescope.available);
        QVERIFY(a.gamescope.note.contains(QLatin1String("already start gamescope")));
        GameSettings withGamescope;
        withGamescope.gamescope = true;
        // gfxtune's gamescope would not run, so it cannot be what caps the game.
        QCOMPARE(availabilityFor(g, withGamescope, system).frameCap.note, QString());

        g.launchOptions = QStringLiteral("RADV_TEX_ANISO=4 MESA_VK_WSI_PRESENT_MODE=fifo PROTON_FSR4_UPGRADE=1 %command%");
        a = availabilityFor(g, GameSettings{}, system);
        QVERIFY(!a.anisotropicFiltering.available);
        QVERIFY(!a.verticalSync.available);
        QVERIFY(!a.fsr4.available);
        QVERIFY(a.fsr4.note.contains(QLatin1String("PROTON_FSR4_UPGRADE")));
        QVERIFY(a.gamescope.available);

        g.launchOptions = QStringLiteral("VKD3D_FRAME_RATE=60 %command%");
        QVERIFY(!availabilityFor(g, GameSettings{}, system).frameCap.available);

        g.apis = GraphicsApi::DirectX11;
        g.launchOptions = QStringLiteral("DXVK_CONFIG=\"dxgi.syncInterval = 0\" %command%");
        GameSettings withoutLayer;
        withoutLayer.measurePerformance = false;
        a = availabilityFor(g, withoutLayer, system);
        QVERIFY(a.frameCap.available);
        QVERIFY(a.frameCap.note.contains(QLatin1String("DXVK_CONFIG")));

        // Arguments after %command% belong to the game, not to Steam.
        g.launchOptions = QStringLiteral("%command% RADV_TEX_ANISO=4 gamescope");
        a = availabilityFor(g, GameSettings{}, system);
        QVERIFY(a.anisotropicFiltering.available);
        QVERIFY(a.gamescope.available);
    }

    void findsCoversInEveryLayout()
    {
        QTemporaryDir dir;
        const QString root = dir.path();
        const QString cache = root + QStringLiteral("/appcache/librarycache/");
        QCOMPARE(steam::findCover(root, 42, 10), QString());

        QVERIFY(test::writeFile(cache + QStringLiteral("10_library_600x900.jpg"), "x"));
        QCOMPARE(steam::findCover(root, 42, 10), cache + QStringLiteral("10_library_600x900.jpg"));

        // Files in the game's own folder win over the flat layout, in either
        // generation's name and one level deeper under a hash.
        const QString older = cache + QStringLiteral("10/0123456789abcdef0123456789abcdef01234567/library_600x900.jpg");
        QVERIFY(test::writeFile(older, "x"));
        QVERIFY(setAge(older, 300));
        QCOMPARE(steam::findCover(root, 42, 10), older);

        // Of several, the one Steam saved last.
        const QString newer = cache + QStringLiteral("10/fedcba9876543210fedcba9876543210fedcba98/library_capsule.jpg");
        QVERIFY(test::writeFile(newer, "x"));
        QVERIFY(setAge(newer, 200));
        QCOMPARE(steam::findCover(root, 42, 10), newer);
        const QString direct = cache + QStringLiteral("10/library_600x900.jpg");
        QVERIFY(test::writeFile(direct, "x"));
        QVERIFY(setAge(direct, 100));
        QCOMPARE(steam::findCover(root, 42, 10), direct);

        // Other pictures and folders that are not hashes are ignored.
        QVERIFY(test::writeFile(cache + QStringLiteral("11/not-a-hash/library_capsule.jpg"), "x"));
        QVERIFY(test::writeFile(cache + QStringLiteral("11/library_hero.jpg"), "x"));
        QVERIFY(test::writeFile(cache + QStringLiteral("11/0123456789abcdef0123456789abcdef01234567/logo.png"), "x"));
        QCOMPARE(steam::findCover(root, 42, 11), QString());

        // A cover the user picked wins, but only for their account.
        const QString custom = root + QStringLiteral("/userdata/42/config/grid/10p.png");
        QVERIFY(test::writeFile(custom, "x"));
        QCOMPARE(steam::findCover(root, 42, 10), custom);
        QCOMPARE(steam::findCover(root, 0, 10), direct);
        QCOMPARE(steam::findCover(root, 43, 10), direct);
    }

    void prefersTheCoverSteamsDataNames()
    {
        QTemporaryDir dir;
        const QString root = dir.path();
        const QString folder = root + QStringLiteral("/appcache/librarycache/20/");
        const QString english = QStringLiteral("1111111111111111111111111111111111111111/library_capsule.jpg");
        const QString japanese = QStringLiteral("2222222222222222222222222222222222222222/library_capsule_japanese.jpg");
        const QStringList listed{english, japanese};

        // Listed but not saved yet: the folder is searched as before.
        QVERIFY(test::writeFile(folder + QStringLiteral("library_600x900.jpg"), "x"));
        QCOMPARE(steam::findCover(root, 0, 20, listed), folder + QStringLiteral("library_600x900.jpg"));

        // A listed file wins over other cover files, even newer ones.
        QVERIFY(test::writeFile(folder + english, "x"));
        QVERIFY(setAge(folder + english, 500));
        QCOMPARE(steam::findCover(root, 0, 20, listed), folder + english);

        // Steam saves only the language it shows; after a language change
        // both exist and the one saved last is the one Steam shows.
        QVERIFY(test::writeFile(folder + japanese, "x"));
        QVERIFY(setAge(folder + japanese, 100));
        QCOMPARE(steam::findCover(root, 0, 20, listed), folder + japanese);
        // With equal times the earlier listed name wins.
        QVERIFY(setAge(folder + english, 100));
        QCOMPARE(steam::findCover(root, 0, 20, listed), folder + english);

        // Names that leave the game's folder are never followed, even when
        // the folder exists and the path would resolve.
        QVERIFY(test::writeFile(root + QStringLiteral("/appcache/librarycache/outside.jpg"), "x"));
        QVERIFY(QDir().mkpath(root + QStringLiteral("/appcache/librarycache/21")));
        QCOMPARE(steam::findCover(root, 0, 21, {QStringLiteral("../outside.jpg")}), QString());
    }

    void readsListedCoverNames()
    {
        using test::section;
        using test::value;
        QCOMPARE(steam::listedCovers(nullptr), QStringList());
        const vdf::Node empty = section({}, {section(QStringLiteral("common"), {})});
        QCOMPARE(steam::listedCovers(&empty), QStringList());

        const QString hash = QStringLiteral("0123456789abcdef0123456789abcdef01234567");
        const vdf::Node info = section({}, {section(QStringLiteral("common"), {section(QStringLiteral("library_assets_full"), {
            section(QStringLiteral("library_capsule"), {
                section(QStringLiteral("image"), {
                    value(QStringLiteral("english"), hash + QStringLiteral("/library_capsule.jpg")),
                    value(QStringLiteral("schinese"), QStringLiteral("library_600x900_schinese.jpg")),
                    value(QStringLiteral("japanese"), QStringLiteral("../../../etc/passwd.jpg")),
                    value(QStringLiteral("koreana"), QStringLiteral("/tmp/absolute.jpg")),
                    value(QStringLiteral("french"), QStringLiteral("not-a-hash/library_capsule.jpg")),
                    value(QStringLiteral("german"), hash.toUpper() + QStringLiteral("/library_capsule.jpg")),
                    value(QStringLiteral("dutch"), QStringLiteral("library_capsule.exe")),
                    value(QStringLiteral("polish"), QString()),
                    section(QStringLiteral("nested"), {}),
                }),
                section(QStringLiteral("image2x"), {
                    value(QStringLiteral("english"), hash + QStringLiteral("/library_capsule_2x.jpg")),
                    value(QStringLiteral("schinese"), QStringLiteral("library_600x900_schinese.jpg")),
                }),
            }),
            section(QStringLiteral("library_hero"), {section(QStringLiteral("image"), {value(QStringLiteral("english"), QStringLiteral("library_hero.jpg"))})}),
        })})});
        QCOMPARE(steam::listedCovers(&info), (QStringList{hash + QStringLiteral("/library_capsule.jpg"),
                                                          QStringLiteral("library_600x900_schinese.jpg"),
                                                          hash + QStringLiteral("/library_capsule_2x.jpg")}));
    }
};

QTEST_GUILESS_MAIN(SettingsTest)
#include "tst_settings.moc"
