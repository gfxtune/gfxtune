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
    s.filtersRiskAccepted = true;
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
        // The section the Vulkan layer reads while the game runs.
        QVERIFY(text.contains("[filters]"));
        QVERIFY(text.contains("sharpening=45"));
        QVERIFY(text.contains("temperature=-35"));
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
                                                  "[filters]\nenabled=maybe\nsharpening=500\ncontrast=x\ngamma=150\n"
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
        // The good values survive; the old FSR 4 switch reads as On.
        QCOMPARE(loaded.fsr4, Fsr4::On);
        QVERIFY(loaded.gamescope);
        QCOMPARE(loaded.scalingMode, ScalingMode::Fill);
        QCOMPARE(loaded.filters.enabled, defaults.filters.enabled);
        QCOMPARE(loaded.filters.sharpening, defaults.filters.sharpening);
        QCOMPARE(loaded.filters.contrast, defaults.filters.contrast);
        QCOMPARE(loaded.filters.gamma, 150);
        QCOMPARE(warnings.size(), 10);
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
        QTest::newRow("Proton DX12 with FSR 3.1") << proton << dx12 << true << false << true << true << true << true << true;
        QTest::newRow("Proton DX12, gamescope on") << proton << dx12 << false << true << true << true << true << false << true;
        QTest::newRow("Proton, gamescope on but missing") << proton << dx12 << false << true << false << true << true << false << false;
        QTest::newRow("Proton Vulkan, no gamescope") << proton << vulkan << false << false << false << true << false << false << false;
        QTest::newRow("Proton, graphics unknown") << proton << 0 << false << false << true << true << true << false << true;
        QTest::newRow("Proton DX11 and 12") << proton << (dx11 | dx12) << true << false << true << true << true << true << true;
        QTest::newRow("native Vulkan") << native << vulkan << false << false << true << true << false << false << true;
        QTest::newRow("native OpenGL") << native << gl << false << false << false << false << false << false << false;
        // Windows games often link OpenGL without using it: only a note.
        QTest::newRow("Proton, OpenGL guess") << proton << gl << false << false << true << true << true << false << true;
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
        QCOMPARE(a.fsr4.available, fsr4);
        QCOMPARE(a.gamescope.available, gamescope);
        // Every unavailable setting says why.
        for (const Option &option : {a.anisotropicFiltering, a.verticalSync, a.frameCap, a.fsr4, a.gamescope}) {
            QVERIFY(option.available || !option.note.isEmpty());
        }
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

        // Vulkan used directly under Proton: nothing reaches it.
        a = availabilityFor(game(Runtime::Proton, GraphicsApi::Vulkan), withGamescope, system);
        QVERIFY(!a.frameCap.available);
        QVERIFY(a.frameCap.note.contains(QLatin1String("Vulkan")));

        // Outside Proton only gamescope can, and only with vertical sync on.
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
        QVERIFY(store.save(9, s, {}));
        QFile file(store.pathFor(9));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray saved = file.readAll();
        QVERIFY(saved.contains("fsr4=off"));
        QVERIFY(!saved.contains("fsr4_upgrade"));
    }

    void acceptingTheWarningAloneChangesNothing()
    {
        QTemporaryDir dir;
        const SettingsStore store(dir.filePath(QStringLiteral("gfxtune")));
        GameSettings s;
        s.filtersRiskAccepted = true;
        QVERIFY(!s.isDefault()); // kept in a file...
        QVERIFY(!s.affectsGame()); // ...but no reason for a launch line
        QVERIFY(store.save(3, s, {}));
        QVERIFY(store.load(3).filtersRiskAccepted);
        QVERIFY(store.customizedGames().isEmpty());
        s.filters.sharpening = 20;
        QVERIFY(s.affectsGame());
        QVERIFY(store.save(3, s, {}));
        QCOMPARE(store.customizedGames(), QSet<quint32>{3});
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
        a = availabilityFor(g, GameSettings{}, system);
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
