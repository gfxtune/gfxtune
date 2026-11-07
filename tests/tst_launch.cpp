// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "launch/LastLaunch.h"
#include "launch/LaunchLine.h"
#include "launch/LaunchLines.h"
#include "launch/LaunchPlan.h"
#include "steam/SteamProcess.h"
#include "support/Fixtures.h"
#include "vdf/TextVdf.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <unistd.h>

using namespace gfxtune;
using namespace gfxtune::launch;
using settings::GameSettings;
using settings::ScalingMode;
using settings::UpscaleFilter;
using settings::VerticalSync;

namespace
{

QString envValue(const LaunchPlan &plan, const QString &name)
{
    for (const auto &[key, value] : plan.environment) {
        if (key == name) {
            return value;
        }
    }
    return {};
}

bool setsEnv(const LaunchPlan &plan, const QString &name)
{
    for (const auto &[key, value] : plan.environment) {
        if (key == name) {
            return true;
        }
    }
    return false;
}

LaunchContext context(bool gamescopeInstalled = true, std::optional<Screen> screen = Screen{2560, 1440, 165})
{
    LaunchContext c;
    c.environment.insert(QStringLiteral("HOME"), QStringLiteral("/home/user"));
    c.gamescopeInstalled = gamescopeInstalled;
    c.screen = screen;
    return c;
}

} // namespace

class LaunchTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void addsTheHelperBeforeTheCommand_data()
    {
        QTest::addColumn<QString>("before");
        QTest::addColumn<QString>("after");
        QTest::newRow("empty") << QString() << QStringLiteral("gfxtune-run %command%");
        QTest::newRow("spaces only") << QStringLiteral("   ") << QStringLiteral("gfxtune-run %command%");
        QTest::newRow("bare command") << QStringLiteral("%command%") << QStringLiteral("gfxtune-run %command%");
        QTest::newRow("wrapper kept first") << QStringLiteral("gamemoderun %command%")
                                            << QStringLiteral("gamemoderun gfxtune-run %command%");
        QTest::newRow("variables and arguments") << QStringLiteral("PROTON_LOG=1 mangohud %command% -dx12 --skip-intro")
                                                 << QStringLiteral("PROTON_LOG=1 mangohud gfxtune-run %command% -dx12 --skip-intro");
        QTest::newRow("plain arguments keep their meaning")
            << QStringLiteral(" -novid -high ") << QStringLiteral("gfxtune-run %command% -novid -high");
        QTest::newRow("already there") << QStringLiteral("gamemoderun gfxtune-run %command%")
                                       << QStringLiteral("gamemoderun gfxtune-run %command%");
        QTest::newRow("already there by path") << QStringLiteral("/usr/bin/gfxtune-run %command%")
                                               << QStringLiteral("/usr/bin/gfxtune-run %command%");
        QTest::newRow("user's own gamescope") << QStringLiteral("gamescope -f -- %command%")
                                              << QStringLiteral("gamescope -f -- gfxtune-run %command%");
    }

    void addsTheHelperBeforeTheCommand()
    {
        QFETCH(QString, before);
        QFETCH(QString, after);
        QCOMPARE(withHelper(before), after);
        QVERIFY(hasHelper(withHelper(before)));
    }

    void removesOnlyTheHelper_data()
    {
        QTest::addColumn<QString>("before");
        QTest::addColumn<QString>("after");
        QTest::newRow("only the helper") << QStringLiteral("gfxtune-run %command%") << QString();
        QTest::newRow("with a wrapper") << QStringLiteral("gamemoderun gfxtune-run %command%") << QStringLiteral("gamemoderun %command%");
        QTest::newRow("full path") << QStringLiteral("/usr/bin/gfxtune-run %command% -dx12") << QStringLiteral("%command% -dx12");
        QTest::newRow("extra spaces") << QStringLiteral("A=1 gfxtune-run   %command%") << QStringLiteral("A=1 %command%");
        QTest::newRow("not ours") << QStringLiteral("my-gfxtune-run %command%") << QStringLiteral("my-gfxtune-run %command%");
        QTest::newRow("absent") << QStringLiteral("gamemoderun %command%") << QStringLiteral("gamemoderun %command%");
    }

    void removesOnlyTheHelper()
    {
        QFETCH(QString, before);
        QFETCH(QString, after);
        QCOMPARE(withoutHelper(before), after);
        QVERIFY(!hasHelper(withoutHelper(before)));
    }

    void addingThenRemovingGivesTheLineBack()
    {
        for (const QString &line : {QString(), QStringLiteral("gamemoderun %command%"), QStringLiteral("A=1 B=\"x y\" %command% -arg")}) {
            QCOMPARE(withoutHelper(withHelper(line)), line);
        }
    }

    void findsWhatTheUserAlreadySet()
    {
        const OwnSettings own = ownSettings(QStringLiteral(
            "PROTON_FSR4_UPGRADE=1 RADV_TEX_ANISO=8 DXVK_CONFIG=\"dxgi.maxFrameRate = 60\" /usr/bin/gamescope -f -- %command% NOT_THIS=1"));
        QCOMPARE(own.environment,
                 (QSet<QString>{QStringLiteral("PROTON_FSR4_UPGRADE"), QStringLiteral("RADV_TEX_ANISO"), QStringLiteral("DXVK_CONFIG")}));
        QVERIFY(own.gamescope);

        const OwnSettings none = ownSettings(QStringLiteral("gamemoderun %command% -gamescope"));
        QVERIFY(none.environment.isEmpty());
        QVERIFY(!none.gamescope);
        QVERIFY(ownSettings({}).environment.isEmpty());
        // Damaged quoting must not crash or loop.
        ownSettings(QStringLiteral("A=\"unterminated %command%"));
        ownSettings(QStringLiteral("B=x\\"));
    }

    void plansNothingForDefaults()
    {
        const LaunchPlan plan = planLaunch(GameSettings{}, context());
        QVERIFY(plan.environment.isEmpty());
        QVERIFY(plan.wrapper.isEmpty());
        QVERIFY(plan.applied.isEmpty());
    }

    void plansDriverAndProtonSettings()
    {
        GameSettings s;
        s.anisotropicFiltering = 16;
        s.verticalSync = VerticalSync::Fast;
        s.frameCapEnabled = true;
        s.frameCap = 141;
        s.fsr4 = settings::Fsr4::On;
        LaunchContext c = context();
        c.proton = true;
        const LaunchPlan plan = planLaunch(s, c);
        QCOMPARE(envValue(plan, QStringLiteral("RADV_TEX_ANISO")), QStringLiteral("16"));
        QCOMPARE(envValue(plan, QStringLiteral("MESA_VK_WSI_PRESENT_MODE")), QStringLiteral("mailbox"));
        QCOMPARE(envValue(plan, QStringLiteral("VKD3D_FRAME_RATE")), QStringLiteral("141"));
        QCOMPARE(envValue(plan, QStringLiteral("PROTON_FSR4_UPGRADE")), QStringLiteral("1"));
        QCOMPARE(
            envValue(plan, QStringLiteral("DXVK_CONFIG")),
            QStringLiteral("d3d11.samplerAnisotropy = 16; d3d9.samplerAnisotropy = 16; dxgi.maxFrameRate = 141; d3d9.maxFrameRate = 141"));
        QVERIFY(plan.wrapper.isEmpty());
        QCOMPARE(plan.applied.size(), 4);
    }

    void switchesTheFilterLayerOn()
    {
        GameSettings s;
        s.filters.sharpening = 40;
        LaunchContext c = context();
        c.filterSettingsPath = QStringLiteral("/home/user/.config/gfxtune/games/7.ini");
        c.filterReportPath = QStringLiteral("/home/user/.config/gfxtune/last-launch/7.filters");
        LaunchPlan plan = planLaunch(s, c);
        QCOMPARE(envValue(plan, QStringLiteral("GFXTUNE_FILTERS")), QStringLiteral("1"));
        QCOMPARE(envValue(plan, QStringLiteral("GFXTUNE_FILTERS_SETTINGS")), c.filterSettingsPath);
        QCOMPARE(envValue(plan, QStringLiteral("GFXTUNE_FILTERS_REPORT")), c.filterReportPath);
        QVERIFY(plan.applied.contains(QStringLiteral("In-game filters")));

        // Only when a filter is on as the game starts, or for measuring.
        s.filters.enabled = false;
        s.measurePerformance = false;
        QVERIFY(!setsEnv(planLaunch(s, c), QStringLiteral("GFXTUNE_FILTERS")));
        s = GameSettings{};
        s.measurePerformance = false;
        s.filters.gamma = 100; // neutral
        QVERIFY(!setsEnv(planLaunch(s, c), QStringLiteral("GFXTUNE_FILTERS")));

        // Without a settings file to follow there is nothing to load.
        s.filters.contrast = 10;
        QVERIFY(!setsEnv(planLaunch(s, context()), QStringLiteral("GFXTUNE_FILTERS")));

        // The user's own choice in the launch options wins.
        c.environment.insert(QStringLiteral("GFXTUNE_FILTERS"), QStringLiteral("0"));
        plan = planLaunch(s, c);
        QVERIFY(!setsEnv(plan, QStringLiteral("GFXTUNE_FILTERS")));
        QVERIFY(!setsEnv(plan, QStringLiteral("GFXTUNE_FILTERS_SETTINGS")));
        QVERIFY(plan.keptFromSteam.contains(QStringLiteral("GFXTUNE_FILTERS")));
    }

    void turnsFsr4OffOnEveryProton()
    {
        GameSettings s;
        QVERIFY(!setsEnv(planLaunch(s, context()), QStringLiteral("PROTON_FSR4_UPGRADE")));

        s.fsr4 = settings::Fsr4::Off;
        LaunchPlan plan = planLaunch(s, context());
        QCOMPARE(envValue(plan, QStringLiteral("PROTON_FSR4_UPGRADE")), QStringLiteral("0"));
        QCOMPARE(envValue(plan, QStringLiteral("WINEDLLOVERRIDES")), QStringLiteral("amdxcffx64=d"));
        QVERIFY(plan.applied.contains(QStringLiteral("FSR 4 off (the game keeps FSR 3.1)")));

        // The user's own overrides stay, with this one added.
        LaunchContext c = context();
        c.environment.insert(QStringLiteral("WINEDLLOVERRIDES"), QStringLiteral("dxgi=n,b;winmm=n"));
        plan = planLaunch(s, c);
        QCOMPARE(envValue(plan, QStringLiteral("WINEDLLOVERRIDES")), QStringLiteral("dxgi=n,b;winmm=n;amdxcffx64=d"));
        QVERIFY(plan.keptFromSteam.isEmpty());

        // An override of their own for the same library wins.
        c.environment.insert(QStringLiteral("WINEDLLOVERRIDES"), QStringLiteral("dxgi=n;AMDXCFFX64=n"));
        plan = planLaunch(s, c);
        QVERIFY(!setsEnv(plan, QStringLiteral("WINEDLLOVERRIDES")));
        QVERIFY(plan.keptFromSteam.contains(QStringLiteral("WINEDLLOVERRIDES")));
        QVERIFY(!plan.applied.contains(QStringLiteral("FSR 4 off (the game keeps FSR 3.1)")));
    }

    void mapsEveryVerticalSyncMode()
    {
        const QList<std::pair<VerticalSync, QString>> modes{
            {VerticalSync::Off, QStringLiteral("immediate")},
            {VerticalSync::On, QStringLiteral("fifo")},
            {VerticalSync::Fast, QStringLiteral("mailbox")},
            {VerticalSync::Adaptive, QStringLiteral("relaxed")},
        };
        for (const auto &[mode, name] : modes) {
            GameSettings s;
            s.verticalSync = mode;
            QCOMPARE(envValue(planLaunch(s, context()), QStringLiteral("MESA_VK_WSI_PRESENT_MODE")), name);
        }
    }

    void leavesTheUsersOwnVariablesAlone()
    {
        GameSettings s;
        s.anisotropicFiltering = 4;
        s.frameCapEnabled = true;
        s.fsr4 = settings::Fsr4::On;
        LaunchContext c = context();
        c.proton = true;
        c.environment.insert(QStringLiteral("PROTON_FSR4_UPGRADE"), QStringLiteral("4.0.2"));
        c.environment.insert(QStringLiteral("DXVK_CONFIG"), QStringLiteral("dxgi.syncInterval = 0"));
        const LaunchPlan plan = planLaunch(s, c);
        QVERIFY(!setsEnv(plan, QStringLiteral("PROTON_FSR4_UPGRADE")));
        QVERIFY(!setsEnv(plan, QStringLiteral("DXVK_CONFIG")));
        QVERIFY(setsEnv(plan, QStringLiteral("RADV_TEX_ANISO")));
        QVERIFY(setsEnv(plan, QStringLiteral("VKD3D_FRAME_RATE")));
        QCOMPARE(plan.keptFromSteam, (QStringList{QStringLiteral("PROTON_FSR4_UPGRADE"), QStringLiteral("DXVK_CONFIG")}));
        QVERIFY(!plan.applied.contains(QStringLiteral("FSR 4 on")));
        QVERIFY(plan.applied.contains(QStringLiteral("Frame rate cap 60"))); // through VKD3D-Proton still
    }

    void reportsACapThatNothingApplies()
    {
        GameSettings s;
        s.frameCapEnabled = true;
        LaunchContext c = context();
        c.proton = true;
        c.environment.insert(QStringLiteral("DXVK_CONFIG"), QStringLiteral("x = 1"));
        c.environment.insert(QStringLiteral("VKD3D_FRAME_RATE"), QStringLiteral("30"));
        const LaunchPlan plan = planLaunch(s, c);
        QVERIFY(plan.applied.isEmpty());
        QCOMPARE(plan.keptFromSteam.size(), 2);
    }

    void wrapsTheGameInGamescope()
    {
        GameSettings s;
        s.gamescope = true;
        s.renderScale = 67;
        s.upscaleFilter = UpscaleFilter::Fsr;
        s.sharpness = 15;
        s.scalingMode = ScalingMode::Fit;
        s.frameCapEnabled = true;
        s.frameCap = 120;
        const LaunchPlan plan = planLaunch(s, context());
        QCOMPARE(plan.wrapper,
                 (QStringList{QStringLiteral("gamescope"), QStringLiteral("-W"), QStringLiteral("2560"), QStringLiteral("-H"),
                              QStringLiteral("1440"), QStringLiteral("-w"), QStringLiteral("1714"), QStringLiteral("-h"),
                              QStringLiteral("964"), QStringLiteral("-r"), QStringLiteral("120"), QStringLiteral("-F"),
                              QStringLiteral("fsr"), QStringLiteral("-S"), QStringLiteral("fit"), QStringLiteral("--sharpness"),
                              QStringLiteral("5"), QStringLiteral("-f"), QStringLiteral("--")}));
        // Outside Proton, gamescope's screen is the cap; the in-game limiters stay off.
        QVERIFY(!setsEnv(plan, QStringLiteral("VKD3D_FRAME_RATE")));
        QVERIFY(!setsEnv(plan, QStringLiteral("DXVK_CONFIG")));
    }

    void capsProtonGamesInsideTheGameEvenInGamescope()
    {
        // gamescope's -r does not hold back a game with vertical sync off, so
        // under Proton the exact in-game limiters cap it and gamescope follows
        // the monitor.
        GameSettings s;
        s.gamescope = true;
        s.frameCapEnabled = true;
        s.frameCap = 60;
        LaunchContext c = context();
        c.proton = true;
        const LaunchPlan plan = planLaunch(s, c);
        QVERIFY(!plan.wrapper.isEmpty());
        QVERIFY(!plan.wrapper.contains(QStringLiteral("-r")));
        QVERIFY(setsEnv(plan, QStringLiteral("VKD3D_FRAME_RATE")));
        QVERIFY(setsEnv(plan, QStringLiteral("DXVK_CONFIG")));
        QVERIFY(plan.applied.contains(QStringLiteral("Frame rate cap 60")));

        // Without a cap, gamescope is never given a refresh rate either.
        s.frameCapEnabled = false;
        QVERIFY(!planLaunch(s, c).wrapper.contains(QStringLiteral("-r")));
    }

    // The layer loads for measuring and filters when gfxtune-run gives it a
    // settings file to follow.
    static LaunchContext layerContext()
    {
        LaunchContext c = context();
        c.filterSettingsPath = QStringLiteral("/home/user/.config/gfxtune/games/7.ini");
        c.performanceDir = QStringLiteral("/home/user/.config/gfxtune/performance/7");
        return c;
    }

    void measuresPerformanceInTheLayer()
    {
        GameSettings s; // measuring is on by default
        LaunchPlan plan = planLaunch(s, layerContext());
        QCOMPARE(envValue(plan, QStringLiteral("GFXTUNE_FILTERS")), QStringLiteral("1"));
        QCOMPARE(envValue(plan, QStringLiteral("GFXTUNE_PERF_DIR")), QStringLiteral("/home/user/.config/gfxtune/performance/7"));
        QCOMPARE(plan.applied, QStringList{QStringLiteral("Performance measuring")});

        // Filters alone: no measurements written.
        s.measurePerformance = false;
        s.filters.saturation = 20;
        plan = planLaunch(s, layerContext());
        QVERIFY(setsEnv(plan, QStringLiteral("GFXTUNE_FILTERS")));
        QVERIFY(!setsEnv(plan, QStringLiteral("GFXTUNE_PERF_DIR")));
        QCOMPARE(plan.applied, QStringList{QStringLiteral("In-game filters")});

        // Games that draw only with OpenGL are out of the layer's reach.
        s = GameSettings{};
        LaunchContext c = layerContext();
        c.facts.openGlOnly = true;
        plan = planLaunch(s, c);
        QVERIFY(!setsEnv(plan, QStringLiteral("GFXTUNE_FILTERS")));
        QVERIFY(plan.notApplied.isEmpty());
    }

    void locksTheLayerInGamesWithAntiCheat()
    {
        GameSettings s;
        s.filters.contrast = 10;
        s.frameCapEnabled = true;
        s.frameCap = 90;
        LaunchContext c = layerContext();
        c.proton = true;
        c.facts.antiCheat = {QStringLiteral("Easy Anti-Cheat")};
        LaunchPlan plan = planLaunch(s, c);
        QVERIFY(!setsEnv(plan, QStringLiteral("GFXTUNE_FILTERS")));
        QVERIFY(!setsEnv(plan, QStringLiteral("GFXTUNE_PERF_DIR")));
        QCOMPARE(plan.notApplied,
                 QStringList{QStringLiteral("In-game filters and measuring: locked until the anti-cheat risk is accepted")});
        // The cap falls back to DXVK and VKD3D-Proton.
        QVERIFY(setsEnv(plan, QStringLiteral("VKD3D_FRAME_RATE")));
        QVERIFY(plan.applied.contains(QStringLiteral("Frame rate cap 90")));

        s.filtersRiskAccepted = true;
        plan = planLaunch(s, c);
        QVERIFY(setsEnv(plan, QStringLiteral("GFXTUNE_FILTERS")));
        QVERIFY(plan.notApplied.isEmpty());
        QVERIFY(!setsEnv(plan, QStringLiteral("VKD3D_FRAME_RATE")));

        // Nothing for the layer to do: nothing locked to report.
        s = GameSettings{};
        s.measurePerformance = false;
        QVERIFY(planLaunch(s, c).notApplied.isEmpty());
    }

    // A native game without the layer or gamescope: DXVK and VKD3D-Proton are
    // not there to hold a cap, so none is claimed.
    void claimsNoCapNothingHolds()
    {
        GameSettings s;
        s.measurePerformance = false;
        s.frameCapEnabled = true;
        s.frameCap = 60;
        const LaunchPlan plan = planLaunch(s, context());
        QVERIFY(!setsEnv(plan, QStringLiteral("VKD3D_FRAME_RATE")));
        QVERIFY(!setsEnv(plan, QStringLiteral("DXVK_CONFIG")));
        QVERIFY(plan.applied.isEmpty());
        QCOMPARE(plan.notApplied,
                 QStringList{QStringLiteral("Frame rate cap: needs gfxtune's layer or gamescope, and neither runs in this game")});

        // gamescope switched on but not installed: the same.
        s.gamescope = true;
        QVERIFY(
            planLaunch(s, context(false))
                .notApplied.contains(QStringLiteral("Frame rate cap: needs gfxtune's layer or gamescope, and neither runs in this game")));
    }

    // One limiter at a time: the layer's whenever it loads.
    void capsInTheLayerWhenItLoads()
    {
        GameSettings s;
        s.frameCapEnabled = true;
        s.frameCap = 60;
        s.gamescope = true;
        for (bool proton : {false, true}) {
            LaunchContext c = layerContext();
            c.proton = proton;
            const LaunchPlan plan = planLaunch(s, c);
            QVERIFY(setsEnv(plan, QStringLiteral("GFXTUNE_FILTERS")));
            QVERIFY(!setsEnv(plan, QStringLiteral("VKD3D_FRAME_RATE")));
            QVERIFY(!setsEnv(plan, QStringLiteral("DXVK_CONFIG")));
            QVERIFY(!plan.wrapper.contains(QStringLiteral("-r")));
            QCOMPARE(plan.applied.count(QStringLiteral("Frame rate cap 60")), 1);
        }
    }

    void setsTextureDetail()
    {
        GameSettings s;
        s.lodBias = -50;
        // Through the layer, for every API it reaches, under Proton too.
        LaunchPlan plan = planLaunch(s, layerContext());
        QVERIFY(plan.applied.contains(QStringLiteral("Texture detail -0.50")));
        QVERIFY(!setsEnv(plan, QStringLiteral("DXVK_CONFIG")));
        LaunchContext protonLayer = layerContext();
        protonLayer.proton = true;
        plan = planLaunch(s, protonLayer);
        QVERIFY(!setsEnv(plan, QStringLiteral("DXVK_CONFIG")));
        QCOMPARE(plan.applied, (QStringList{QStringLiteral("Performance measuring"), QStringLiteral("Texture detail -0.50")}));

        // Without it, DXVK sets it for DirectX 9 to 11.
        s.measurePerformance = false;
        LaunchContext c = layerContext();
        c.proton = true;
        plan = planLaunch(s, c);
        QVERIFY(!setsEnv(plan, QStringLiteral("GFXTUNE_FILTERS")));
        QCOMPARE(envValue(plan, QStringLiteral("DXVK_CONFIG")),
                 QStringLiteral("d3d11.samplerLodBias = -0.50; d3d9.samplerLodBias = -0.50"));
        QCOMPARE(plan.applied, QStringList{QStringLiteral("Texture detail -0.50 (DirectX 9 to 11)")});

        // Softer textures: a positive bias.
        s.lodBias = 100;
        QVERIFY(planLaunch(s, c).applied.contains(QStringLiteral("Texture detail 1.00 (DirectX 9 to 11)")));

        // The user's own DXVK_CONFIG wins, and nothing is claimed.
        c.environment.insert(QStringLiteral("DXVK_CONFIG"), QStringLiteral("dxgi.syncInterval = 0"));
        plan = planLaunch(s, c);
        QVERIFY(!setsEnv(plan, QStringLiteral("DXVK_CONFIG")));
        QVERIFY(plan.applied.isEmpty());

        // A native game without the layer has nothing that sets it.
        plan = planLaunch(s, context());
        QCOMPARE(plan.notApplied, QStringList{QStringLiteral("Texture detail: needs gfxtune's layer, which is off for this game")});
    }

    // Mesa's vblank_mode reaches OpenGL games, which know only off and on.
    void syncsOpenGlGamesToo()
    {
        const QList<std::pair<VerticalSync, QString>> modes{
            {VerticalSync::Off, QStringLiteral("0")}, {VerticalSync::On, QStringLiteral("3")}, {VerticalSync::Fast, QString()},
            {VerticalSync::Adaptive, QString()},      {VerticalSync::GameDefault, QString()},
        };
        for (const auto &[mode, value] : modes) {
            GameSettings s;
            s.verticalSync = mode;
            const LaunchPlan plan = planLaunch(s, context());
            QCOMPARE(setsEnv(plan, QStringLiteral("vblank_mode")), !value.isEmpty());
            QCOMPARE(envValue(plan, QStringLiteral("vblank_mode")), value);
        }
        GameSettings s;
        s.verticalSync = VerticalSync::Off;
        LaunchContext c = context();
        c.environment.insert(QStringLiteral("vblank_mode"), QStringLiteral("1"));
        const LaunchPlan plan = planLaunch(s, c);
        QVERIFY(!setsEnv(plan, QStringLiteral("vblank_mode")));
        QVERIFY(setsEnv(plan, QStringLiteral("MESA_VK_WSI_PRESENT_MODE")));
    }

    void supersamplesThroughGamescope()
    {
        GameSettings s;
        s.gamescope = true;
        s.renderScale = 150;
        s.upscaleFilter = UpscaleFilter::Fsr; // not used when scaling down
        s.sharpness = 15;
        const LaunchPlan plan = planLaunch(s, context());
        QCOMPARE(plan.wrapper,
                 (QStringList{QStringLiteral("gamescope"), QStringLiteral("-W"), QStringLiteral("2560"), QStringLiteral("-H"),
                              QStringLiteral("1440"), QStringLiteral("-w"), QStringLiteral("3840"), QStringLiteral("-h"),
                              QStringLiteral("2160"), QStringLiteral("-F"), QStringLiteral("linear"), QStringLiteral("-S"),
                              QStringLiteral("auto"), QStringLiteral("-f"), QStringLiteral("--")}));
        QCOMPARE(plan.applied, QStringList{QStringLiteral("gamescope: drawn at 3840×2160, scaled down to 2560×1440")});

        // Twice the size of a 4K screen stays an even, valid size.
        s.renderScale = 200;
        const LaunchPlan big = planLaunch(s, context(true, Screen{3840, 2160, 120}));
        QCOMPARE(big.wrapper.mid(5, 4),
                 (QStringList{QStringLiteral("-w"), QStringLiteral("7680"), QStringLiteral("-h"), QStringLiteral("4320")}));
    }

    void startsGameModeAndMangoHud()
    {
        GameSettings s;
        s.gameMode = true;
        s.mangoHud = true;
        LaunchContext c = context();
        c.gameModeInstalled = true;
        c.mangoHudInstalled = true;
        LaunchPlan plan = planLaunch(s, c);
        QCOMPARE(plan.wrapper, (QStringList{QStringLiteral("gamemoderun"), QStringLiteral("mangohud")}));
        QCOMPARE(plan.applied, (QStringList{QStringLiteral("GameMode"), QStringLiteral("MangoHud")}));

        // Started already by the launch options: left alone.
        c.environment.insert(QStringLiteral("LD_PRELOAD"), QStringLiteral("/usr/$LIB/libgamemodeauto.so.0"));
        c.environment.insert(QStringLiteral("MANGOHUD"), QStringLiteral("1"));
        plan = planLaunch(s, c);
        QVERIFY(plan.wrapper.isEmpty());
        QCOMPARE(plan.keptFromSteam, (QStringList{QStringLiteral("gamemoderun"), QStringLiteral("MangoHud")}));

        // Not installed.
        c = context();
        plan = planLaunch(s, c);
        QVERIFY(plan.wrapper.isEmpty());
        QCOMPARE(plan.notApplied, (QStringList{QStringLiteral("GameMode: not installed"), QStringLiteral("MangoHud: not installed")}));
    }

    // MangoHud's layer is not supported inside gamescope; gamescope shows it
    // through mangoapp instead.
    void showsMangoHudThroughGamescope()
    {
        GameSettings s;
        s.gameMode = true;
        s.mangoHud = true;
        s.gamescope = true;
        LaunchContext c = context();
        c.gameModeInstalled = true;
        c.mangoHudInstalled = true;
        c.mangoAppInstalled = true;
        LaunchPlan plan = planLaunch(s, c);
        QCOMPARE(plan.wrapper.first(), QStringLiteral("gamemoderun"));
        QCOMPARE(plan.wrapper.at(1), QStringLiteral("gamescope"));
        QVERIFY(!plan.wrapper.contains(QStringLiteral("mangohud")));
        QCOMPARE(plan.wrapper.mid(plan.wrapper.size() - 3),
                 (QStringList{QStringLiteral("--mangoapp"), QStringLiteral("-f"), QStringLiteral("--")}));
        QVERIFY(plan.applied.contains(QStringLiteral("MangoHud (gamescope's mangoapp)")));

        c.mangoAppInstalled = false;
        plan = planLaunch(s, c);
        QVERIFY(!plan.wrapper.contains(QStringLiteral("--mangoapp")));
        QVERIFY(!plan.wrapper.contains(QStringLiteral("mangohud")));
        QCOMPARE(plan.notApplied, QStringList{QStringLiteral("MangoHud: inside gamescope it needs mangoapp, which is not installed")});

        // gamescope cannot run: MangoHud goes around the game as usual.
        c.gamescopeInstalled = false;
        plan = planLaunch(s, c);
        QCOMPARE(plan.wrapper, (QStringList{QStringLiteral("gamemoderun"), QStringLiteral("mangohud")}));
    }

    void recognisesProtonCommands_data()
    {
        QTest::addColumn<QStringList>("command");
        QTest::addColumn<bool>("proton");
        const QString game = QStringLiteral("/games/Raft/Raft.exe");
        QTest::newRow("Proton") << QStringList{QStringLiteral("/steam/steamapps/common/Proton - Experimental/proton"),
                                               QStringLiteral("waitforexitandrun"), game}
                                << true;
        QTest::newRow("behind the runtime") << QStringList{
            QStringLiteral("/steam/steamapps/common/SteamLinuxRuntime_sniper/_v2-entry-point"),
            QStringLiteral("--verb=waitforexitandrun"),
            QStringLiteral("--"),
            QStringLiteral("/usr/share/steam/compatibilitytools.d/proton-cachyos/proton"),
            QStringLiteral("waitforexitandrun"),
            game} << true;
        QTest::newRow("native game") << QStringList{QStringLiteral("/games/Farm Days/FarmDays")} << false;
        QTest::newRow("native, runtime only") << QStringList{QStringLiteral("/steam/SteamLinuxRuntime_sniper/_v2-entry-point"),
                                                             QStringLiteral("--verb=waitforexitandrun"), QStringLiteral("--"),
                                                             QStringLiteral("/games/arena/arena.sh")}
                                              << false;
        QTest::newRow("a program named proton, no verb")
            << QStringList{QStringLiteral("/games/proton"), QStringLiteral("-fullscreen")} << false;
        QTest::newRow("proton only in a folder name") << QStringList{QStringLiteral("/games/proton/run"), QStringLiteral("run")} << false;
        QTest::newRow("verb before proton") << QStringList{QStringLiteral("waitforexitandrun"), QStringLiteral("/x/proton")} << false;
        QTest::newRow("nothing") << QStringList{} << false;
    }

    void recognisesProtonCommands()
    {
        QFETCH(QStringList, command);
        QFETCH(bool, proton);
        QCOMPARE(runsThroughProton(command), proton);
    }

    void leavesSharpnessOutForOtherFilters()
    {
        GameSettings s;
        s.gamescope = true;
        s.upscaleFilter = UpscaleFilter::Nearest;
        const LaunchPlan plan = planLaunch(s, context());
        QVERIFY(!plan.wrapper.contains(QStringLiteral("--sharpness")));
        QVERIFY(plan.wrapper.contains(QStringLiteral("nearest")));
        QVERIFY(!plan.wrapper.contains(QStringLiteral("-r")));
        // Native resolution: drawn at the screen size.
        QCOMPARE(plan.wrapper.at(plan.wrapper.indexOf(QStringLiteral("-w")) + 1), QStringLiteral("2560"));
    }

    void skipsGamescopeWhenItCannotRun_data()
    {
        QTest::addColumn<bool>("installed");
        QTest::addColumn<bool>("screenKnown");
        QTest::addColumn<bool>("insideGamescope");
        QTest::newRow("not installed") << false << true << false;
        QTest::newRow("screen unknown") << true << false << false;
        QTest::newRow("already inside gamescope") << true << true << true;
    }

    void skipsGamescopeWhenItCannotRun()
    {
        QFETCH(bool, installed);
        QFETCH(bool, screenKnown);
        QFETCH(bool, insideGamescope);
        GameSettings s;
        s.gamescope = true;
        s.frameCapEnabled = true;
        for (bool proton : {false, true}) {
            LaunchContext c = context(installed, screenKnown ? std::optional<Screen>(Screen{1920, 1080, 60}) : std::nullopt);
            c.proton = proton;
            if (insideGamescope) {
                c.environment.insert(QStringLiteral("GAMESCOPE_WAYLAND_DISPLAY"), QStringLiteral("gamescope-0"));
            }
            const LaunchPlan plan = planLaunch(s, c);
            QVERIFY(plan.wrapper.isEmpty());
            QVERIFY(!plan.notApplied.isEmpty() || plan.keptFromSteam.contains(QStringLiteral("gamescope")));
            // Under Proton the cap falls back to the in-game limiters; a
            // native game has none.
            QCOMPARE(setsEnv(plan, QStringLiteral("VKD3D_FRAME_RATE")), proton);
            QCOMPARE(plan.applied.contains(QStringLiteral("Frame rate cap 60")), proton);
        }
    }

    void keepsRenderSizesSane()
    {
        GameSettings s;
        s.gamescope = true;
        s.renderScale = 33;
        const LaunchPlan tiny = planLaunch(s, context(true, Screen{800, 480, 60}));
        QCOMPARE(tiny.wrapper.at(tiny.wrapper.indexOf(QStringLiteral("-w")) + 1), QStringLiteral("320"));
        QCOMPARE(tiny.wrapper.at(tiny.wrapper.indexOf(QStringLiteral("-h")) + 1), QStringLiteral("200"));
        s.renderScale = 59;
        const LaunchPlan odd = planLaunch(s, context(true, Screen{1920, 1080, 60}));
        QCOMPARE(odd.wrapper.at(odd.wrapper.indexOf(QStringLiteral("-w")) + 1).toInt() % 2, 0);
        QCOMPARE(odd.wrapper.at(odd.wrapper.indexOf(QStringLiteral("-h")) + 1).toInt() % 2, 0);
    }

    void recordsTheLastLaunch()
    {
        QTemporaryDir dir;
        QVERIFY(!readLastLaunch(dir.path(), 7));
        LaunchPlan plan;
        plan.applied = {QStringLiteral("Anisotropic filtering 16×"), QStringLiteral("FSR 4 on")};
        plan.keptFromSteam = {QStringLiteral("DXVK_CONFIG")};
        plan.notApplied = {QStringLiteral("gamescope: not installed")};
        const QDateTime when = QDateTime(QDate(2026, 10, 21), QTime(18, 3), QTimeZone::UTC);
        QVERIFY(writeLastLaunch(dir.path(), 7, plan, when));
        const auto read = readLastLaunch(dir.path(), 7);
        QVERIFY(read);
        QCOMPARE(read->time, when);
        QCOMPARE(read->applied, plan.applied);
        QCOMPARE(read->keptFromSteam, plan.keptFromSteam);
        QCOMPARE(read->notApplied, plan.notApplied);

        QVERIFY(test::writeFile(lastLaunchPath(dir.path(), 8), "[launch]\ntime=yesterday\n"));
        QVERIFY(!readLastLaunch(dir.path(), 8));
    }

    void detectsTheUsersSteam()
    {
        QTemporaryDir proc;
        const uint me = uint(getuid());
        auto process = [&](const char *pid, const char *comm, uint uid) {
            test::writeFile(proc.filePath(QString::fromLatin1(pid) + QStringLiteral("/comm")), QByteArray(comm) + '\n');
            test::writeFile(proc.filePath(QString::fromLatin1(pid) + QStringLiteral("/status")),
                            "Name:\t" + QByteArray(comm) + "\nUid:\t" + QByteArray::number(uid) + "\t" + QByteArray::number(uid)
                                + "\t0\t0\n");
        };
        process("10", "bash", me);
        process("11", "steamwebhelper", me);
        QVERIFY(!steam::isSteamRunning(proc.path(), me));
        process("12", "steam", me + 1); // someone else's Steam
        QVERIFY(!steam::isSteamRunning(proc.path(), me));
        process("13", "steam", me);
        QVERIFY(steam::isSteamRunning(proc.path(), me));
        QVERIFY(!steam::isSteamRunning(QStringLiteral("/does/not/exist"), me));
    }

    void plansLineChanges()
    {
        const QHash<quint32, QString> current{
            {1, QStringLiteral("gamemoderun %command%")},
            {2, QStringLiteral("gfxtune-run %command%")},
            {3, QStringLiteral("gamemoderun gfxtune-run %command% -x")},
            {4, QStringLiteral("-novid")},
        };
        const QList<LineChange> changes = planLineChanges(current, {1, 3, 9});
        QCOMPARE(changes.size(), 3);
        QCOMPARE(changes[0].appId, 1u);
        QCOMPARE(changes[0].after, QStringLiteral("gamemoderun gfxtune-run %command%"));
        QCOMPARE(changes[1].appId, 2u);
        QCOMPARE(changes[1].after, QString());
        QCOMPARE(changes[2].appId, 9u);
        QCOMPARE(changes[2].after, QStringLiteral("gfxtune-run %command%"));
        QVERIFY(planLineChanges(current, {3}).size() == 1); // only 2 loses it
    }

    void syncsSteamsFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("localconfig.vdf"));
        const QByteArray original =
            "\"UserLocalConfigStore\"\n{\n\t\"friends\"\n\t{\n\t\t\"12345\"\n\t\t{\n\t\t\t\"name\"\t\t\"someone\"\n\t\t}\n\t}\n"
            "\t\"Software\"\n\t{\n\t\t\"Valve\"\n\t\t{\n\t\t\t\"Steam\"\n\t\t\t{\n\t\t\t\t\"apps\"\n\t\t\t\t{\n"
            "\t\t\t\t\t\"1001\"\n\t\t\t\t\t{\n\t\t\t\t\t\t\"LastPlayed\"\t\t\"1700000000\"\n"
            "\t\t\t\t\t\t\"LaunchOptions\"\t\t\"gamemoderun %command%\"\n\t\t\t\t\t}\n"
            "\t\t\t\t\t\"1002\"\n\t\t\t\t\t{\n\t\t\t\t\t\t\"LastPlayed\"\t\t\"1700000001\"\n\t\t\t\t\t}\n"
            "\t\t\t\t}\n\t\t\t}\n\t\t}\n\t}\n}\n";
        QVERIFY(test::writeFile(path, original));
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);

        QList<LineChange> done;
        QString error;
        QVERIFY2(syncLaunchLines(path, {1001, 1002, 1003}, &done, &error), qPrintable(error));
        QCOMPARE(done.size(), 3);
        QHash<quint32, QString> now;
        QVERIFY(readLaunchOptions(path, now));
        QCOMPARE(now.value(1001), QStringLiteral("gamemoderun gfxtune-run %command%"));
        QCOMPARE(now.value(1002), QStringLiteral("gfxtune-run %command%"));
        QCOMPARE(now.value(1003), QStringLiteral("gfxtune-run %command%"));
        QCOMPARE(QFile::permissions(path) & test::FileModeBits, QFileDevice::ReadOwner | QFileDevice::WriteOwner);

        // Everything else in the file is untouched, friends included.
        vdf::Node tree;
        QVERIFY(vdf::readTextFile(path, tree));
        QCOMPARE(tree.find({u"UserLocalConfigStore", u"friends", u"12345", u"name"})->value, QStringLiteral("someone"));

        // Taking it out again gives the original file back, byte for byte.
        QVERIFY2(syncLaunchLines(path, {}, &done, &error), qPrintable(error));
        QCOMPARE(done.size(), 3);
        QFile back(path);
        QVERIFY(back.open(QIODevice::ReadOnly));
        QByteArray restored = back.readAll();
        // 1003 did not exist before; its emptied section stays, empty.
        restored.replace("\t\t\t\t\t\"1003\"\n\t\t\t\t\t{\n\t\t\t\t\t}\n", "");
        QCOMPARE(restored, original);
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files), QStringList{QStringLiteral("localconfig.vdf")});
    }

    void refusesToSyncADamagedFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("localconfig.vdf"));
        const QByteArray damaged = "\"UserLocalConfigStore\" { \"Software\" { ";
        QVERIFY(test::writeFile(path, damaged));
        QString error;
        QVERIFY(!syncLaunchLines(path, {1}, nullptr, &error));
        QVERIFY(!error.isEmpty());
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), damaged);
    }

    void handlesANewAccountWithoutAFile()
    {
        QTemporaryDir dir;
        QString error;
        const QString path = dir.filePath(QStringLiteral("missing.vdf"));
        QVERIFY(syncLaunchLines(path, {}, nullptr, &error)); // nothing to do
        QVERIFY(!syncLaunchLines(path, {1}, nullptr, &error)); // cannot create Steam's file
        QVERIFY(!QFile::exists(path));
    }
};

QTEST_GUILESS_MAIN(LaunchTest)
#include "tst_launch.moc"
