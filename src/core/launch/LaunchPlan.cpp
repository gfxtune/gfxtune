// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "LaunchPlan.h"

#include <KLocalizedString>
#include <QRegularExpression>
#include <QtMath>

#include <utility>

namespace gfxtune::launch
{

using settings::Fsr4;
using settings::GameSettings;
using settings::UpscaleFilter;
using settings::VerticalSync;

namespace
{

// Vulkan present modes as Mesa's MESA_VK_WSI_PRESENT_MODE names them.
QString presentMode(VerticalSync mode)
{
    switch (mode) {
    case VerticalSync::Off:
        return QStringLiteral("immediate");
    case VerticalSync::On:
        return QStringLiteral("fifo");
    case VerticalSync::Fast:
        return QStringLiteral("mailbox");
    case VerticalSync::Adaptive:
        return QStringLiteral("relaxed");
    case VerticalSync::GameDefault:
        break;
    }
    return {};
}

QString verticalSyncWords(VerticalSync mode)
{
    switch (mode) {
    case VerticalSync::Off:
        return QStringLiteral("off");
    case VerticalSync::On:
        return QStringLiteral("on");
    case VerticalSync::Fast:
        return QStringLiteral("fast");
    case VerticalSync::Adaptive:
        return QStringLiteral("adaptive");
    case VerticalSync::GameDefault:
        break;
    }
    return {};
}

QString filterWords(UpscaleFilter filter)
{
    switch (filter) {
    case UpscaleFilter::Fsr:
        return QStringLiteral("FSR 1");
    case UpscaleFilter::Nis:
        return QStringLiteral("NIS");
    case UpscaleFilter::Sgsr:
        return QStringLiteral("Snapdragon GSR");
    case UpscaleFilter::Linear:
        return i18n("linear scaling");
    case UpscaleFilter::Nearest:
        return i18n("nearest-neighbor scaling");
    case UpscaleFilter::Pixel:
        return i18n("pixel-art scaling");
    }
    return {};
}

int evenAtLeast(double value, int minimum)
{
    const int rounded = qMax(minimum, qRound(value));
    return rounded - (rounded % 2);
}

// Builds a launch plan one setting at a time. The steps run in a fixed
// order, which is also the order of the variables and of the report lines.
class Planner
{
public:
    Planner(const GameSettings &settings, const LaunchContext &context)
        : m_s(settings)
        , m_context(context)
    {
    }

    LaunchPlan plan()
    {
        chooseGamescope();
        addLayer();
        addAnisotropicFiltering();
        addTextureDetail();
        addVerticalSync();
        addFrameCap();
        addFsr4();
        addDxvkConfig();
        addPrograms();
        return std::move(m_plan);
    }

private:
    enum class Hud { None, Wrapper, Gamescope };

    // Sets a variable unless the user's launch options already did.
    bool set(const QString &name, const QString &value)
    {
        if (m_context.environment.contains(name)) {
            keep(name);
            return false;
        }
        m_plan.environment.append({name, value});
        return true;
    }

    void keep(const QString &what)
    {
        if (!m_plan.keptFromSteam.contains(what)) {
            m_plan.keptFromSteam.append(what);
        }
    }

    // Adds a Wine DLL override to any the user set, unless they already
    // chose one for the same library.
    bool overrideDll(const QString &dll, const QString &mode)
    {
        const QString name = QStringLiteral("WINEDLLOVERRIDES");
        const QString entry = dll + QLatin1Char('=') + mode;
        const auto own = m_context.environment.constFind(name);
        if (own == m_context.environment.constEnd() || own->trimmed().isEmpty()) {
            m_plan.environment.append({name, entry});
            return true;
        }
        static const QRegularExpression separators(QStringLiteral("[;,=]"));
        if (own->split(separators).contains(dll, Qt::CaseInsensitive)) {
            keep(name);
            return false;
        }
        m_plan.environment.append({name, *own + QLatin1Char(';') + entry});
        return true;
    }

    QString bias() const { return QString::number(double(m_s.lodBias) / 100.0, 'f', 2); }

    void chooseGamescope()
    {
        if (!m_s.gamescope) {
            return;
        }
        if (m_context.environment.contains(QStringLiteral("GAMESCOPE_WAYLAND_DISPLAY"))) {
            keep(QStringLiteral("gamescope"));
        } else if (!m_context.gamescopeInstalled) {
            m_plan.notApplied.append(i18n("gamescope: not installed"));
        } else if (!m_context.screen || m_context.screen->width <= 0 || m_context.screen->height <= 0) {
            m_plan.notApplied.append(i18n("gamescope: the screen size could not be read"));
        } else {
            m_gamescope = true;
            m_screen = *m_context.screen;
        }
    }

    // gfxtune's Vulkan layer loads only with GFXTUNE_FILTERS set, for the
    // filters or for measuring. It follows the settings file while the game
    // runs, and also takes over the frame cap and texture detail, the same
    // way for Vulkan, DXVK and VKD3D-Proton games.
    void addLayer()
    {
        if (settings::layerWanted(m_s, m_context.facts) && !m_context.filterSettingsPath.isEmpty()
            && set(QStringLiteral("GFXTUNE_FILTERS"), QStringLiteral("1"))) {
            m_layer = true;
            m_plan.layer = true;
            m_plan.environment.append({QStringLiteral("GFXTUNE_FILTERS_SETTINGS"), m_context.filterSettingsPath});
            if (!m_context.globalSettingsPath.isEmpty()) {
                m_plan.environment.append({QStringLiteral("GFXTUNE_GLOBAL_SETTINGS"), m_context.globalSettingsPath});
            }
            if (!m_context.filterReportPath.isEmpty()) {
                m_plan.environment.append({QStringLiteral("GFXTUNE_FILTERS_REPORT"), m_context.filterReportPath});
            }
            if (m_s.filters.active()) {
                m_plan.filters = true;
                m_plan.applied.append(i18n("In-game filters"));
            }
            if (m_s.measurePerformance && !m_context.performanceDir.isEmpty()) {
                m_plan.environment.append({QStringLiteral("GFXTUNE_PERF_DIR"), m_context.performanceDir});
                m_plan.applied.append(i18n("Performance measuring"));
            }
        } else if (!settings::layerAllowed(m_s, m_context.facts) && (m_s.filters.active() || m_s.measurePerformance)) {
            m_plan.notApplied.append(i18n("In-game filters and measuring: locked until the anti-cheat risk is accepted"));
        }
    }

    void addAnisotropicFiltering()
    {
        if (m_s.anisotropicFiltering <= 0) {
            return;
        }
        const QString level = QString::number(m_s.anisotropicFiltering);
        const bool driver = set(QStringLiteral("RADV_TEX_ANISO"), level);
        m_dxvk << QStringLiteral("d3d11.samplerAnisotropy = ") + level << QStringLiteral("d3d9.samplerAnisotropy = ") + level;
        if (driver) {
            m_plan.applied.append(i18n("Anisotropic filtering %1×", level));
        }
    }

    void addTextureDetail()
    {
        if (m_s.lodBias == 0) {
            return;
        }
        if (m_layer) {
            m_plan.applied.append(i18n("Texture detail %1", bias()));
        } else if (m_context.proton) {
            m_dxvk << QStringLiteral("d3d11.samplerLodBias = ") + bias() << QStringLiteral("d3d9.samplerLodBias = ") + bias();
            m_lodByDxvk = true;
        } else {
            m_plan.notApplied.append(i18n("Texture detail: needs gfxtune's layer, which is off for this game"));
        }
    }

    void addVerticalSync()
    {
        const VerticalSync mode = m_s.verticalSync;
        if (mode == VerticalSync::GameDefault) {
            return;
        }
        const bool vulkan = set(QStringLiteral("MESA_VK_WSI_PRESENT_MODE"), presentMode(mode));
        // OpenGL through Mesa knows only off and on.
        if (mode == VerticalSync::Off || mode == VerticalSync::On) {
            set(QStringLiteral("vblank_mode"), mode == VerticalSync::Off ? QStringLiteral("0") : QStringLiteral("3"));
        }
        if (vulkan) {
            m_plan.applied.append(i18n("Vertical sync: %1", verticalSyncWords(mode)));
        }
    }

    // One limiter, never two at once: two on the same rate drift against
    // each other and stutter. The layer's paces every Vulkan game exactly;
    // without it, DXVK and VKD3D-Proton cap Proton games inside the game.
    // gamescope's -r only sets the refresh rate of its own screen, which
    // holds a game back only while it waits for vertical sync: it is the
    // cap only for native games the layer does not reach. A native game
    // with neither has nothing to cap it.
    void addFrameCap()
    {
        if (!m_s.frameCapEnabled) {
            return;
        }
        if (m_layer) {
            m_plan.applied.append(i18n("Frame rate cap %1", m_s.frameCap));
        } else if (m_context.proton) {
            const QString fps = QString::number(m_s.frameCap);
            m_dxvk << QStringLiteral("dxgi.maxFrameRate = ") + fps << QStringLiteral("d3d9.maxFrameRate = ") + fps;
            m_capInGame = true;
            m_cappedByVkd3d = set(QStringLiteral("VKD3D_FRAME_RATE"), fps);
        } else if (m_gamescope) {
            m_capWithGamescope = true; // added with gamescope's other options
        } else {
            m_plan.notApplied.append(i18n("Frame rate cap: needs gfxtune's layer or gamescope, and neither runs in this game"));
        }
    }

    void addFsr4()
    {
        switch (m_s.fsr4) {
        case Fsr4::On:
            if (set(QStringLiteral("PROTON_FSR4_UPGRADE"), QStringLiteral("1"))) {
                m_plan.applied.append(i18n("FSR 4 on"));
            }
            break;
        case Fsr4::Off: {
            // proton-cachyos and GE-Proton read the variable; Valve's Proton
            // has no switch but copies AMD's FSR 4 library into the prefix,
            // so Wine is also told not to load it. The game then keeps its
            // FSR 3.1.
            const bool variable = set(QStringLiteral("PROTON_FSR4_UPGRADE"), QStringLiteral("0"));
            const bool library = overrideDll(QStringLiteral("amdxcffx64"), QStringLiteral("d"));
            if (variable && library) {
                m_plan.applied.append(i18n("FSR 4 off (the game keeps FSR 3.1)"));
            }
            break;
        }
        case Fsr4::ProtonDefault:
            break;
        }
    }

    // DXVK takes all of its options in one variable, which the settings
    // above filled in.
    void addDxvkConfig()
    {
        const bool dxvkSet = !m_dxvk.isEmpty() && set(QStringLiteral("DXVK_CONFIG"), m_dxvk.join(QStringLiteral("; ")));
        if (m_capInGame && (dxvkSet || m_cappedByVkd3d)) {
            m_plan.applied.append(i18n("Frame rate cap %1", m_s.frameCap));
        }
        if (m_lodByDxvk && dxvkSet) {
            m_plan.applied.append(i18n("Texture detail %1 (DirectX 9 to 11)", bias()));
        }
    }

    // The programs the game starts through, outermost first: GameMode for
    // everything, then gamescope, then MangoHud right around the game so its
    // overlay lands in the game's own picture. Inside gamescope MangoHud's
    // layer is not supported; gamescope starts its mangoapp instead.
    void addPrograms()
    {
        addGameMode();
        const Hud hud = chooseHud();
        if (m_gamescope) {
            addGamescope(hud == Hud::Gamescope);
        }
        if (hud == Hud::Gamescope) {
            m_plan.applied.append(i18n("MangoHud (gamescope's mangoapp)"));
        } else if (hud == Hud::Wrapper) {
            m_plan.wrapper << QStringLiteral("mangohud");
            m_plan.applied.append(QStringLiteral("MangoHud"));
        }
    }

    void addGameMode()
    {
        if (!m_s.gameMode) {
            return;
        }
        if (m_context.environment.value(QStringLiteral("LD_PRELOAD")).contains(QLatin1String("gamemodeauto"))) {
            keep(QStringLiteral("gamemoderun"));
        } else if (!m_context.gameModeInstalled) {
            m_plan.notApplied.append(i18n("GameMode: not installed"));
        } else {
            m_plan.wrapper << QStringLiteral("gamemoderun");
            m_plan.applied.append(QStringLiteral("GameMode"));
        }
    }

    Hud chooseHud()
    {
        if (!m_s.mangoHud) {
            return Hud::None;
        }
        if (m_context.environment.contains(QStringLiteral("MANGOHUD"))) {
            keep(QStringLiteral("MangoHud"));
        } else if (m_gamescope && !m_context.mangoAppInstalled) {
            m_plan.notApplied.append(i18n("MangoHud: inside gamescope it needs mangoapp, which is not installed"));
        } else if (!m_gamescope && !m_context.mangoHudInstalled) {
            m_plan.notApplied.append(i18n("MangoHud: not installed"));
        } else {
            return m_gamescope ? Hud::Gamescope : Hud::Wrapper;
        }
        return Hud::None;
    }

    void addGamescope(bool mangoApp)
    {
        const Screen &screen = m_screen;
        const double scale = m_s.renderScale / 100.0;
        const int width = evenAtLeast(screen.width * scale, 320);
        const int height = evenAtLeast(screen.height * scale, 200);
        // Drawn bigger than the screen: scaled down, where upscalers and
        // their sharpening do not apply.
        const bool supersampling = m_s.renderScale > 100;
        QStringList &w = m_plan.wrapper;
        // The screen's size, then the size the game draws at.
        w << QStringLiteral("gamescope");
        w << QStringLiteral("-W") << QString::number(screen.width) << QStringLiteral("-H") << QString::number(screen.height);
        w << QStringLiteral("-w") << QString::number(width) << QStringLiteral("-h") << QString::number(height);
        if (m_capWithGamescope) {
            w << QStringLiteral("-r") << QString::number(m_s.frameCap);
            m_plan.applied.append(i18n("Frame rate cap %1 (gamescope, while vertical sync is on)", m_s.frameCap));
        }
        w << QStringLiteral("-F") << (supersampling ? QStringLiteral("linear") : settings::toKey(m_s.upscaleFilter));
        w << QStringLiteral("-S") << settings::toKey(m_s.scalingMode);
        if (!supersampling && m_s.upscaleFilter == UpscaleFilter::Fsr) {
            // gamescope counts 0 as the strongest and 20 as the weakest.
            w << QStringLiteral("--sharpness") << QString::number(GameSettings::MaxSharpness - m_s.sharpness);
        }
        if (mangoApp) {
            w << QStringLiteral("--mangoapp");
        }
        w << QStringLiteral("-f") << QStringLiteral("--");
        // Sizes as plain digits: no thousands separators in 2560×1440.
        const auto n = [](int value) { return QString::number(value); };
        if (supersampling) {
            m_plan.applied.append(
                i18n("gamescope: drawn at %1×%2, scaled down to %3×%4", n(width), n(height), n(screen.width), n(screen.height)));
        } else {
            m_plan.applied.append(i18n("gamescope: drawn at %1×%2, shown at %3×%4 with %5", n(width), n(height), n(screen.width),
                                       n(screen.height), filterWords(m_s.upscaleFilter)));
        }
    }

    const GameSettings &m_s;
    const LaunchContext &m_context;
    LaunchPlan m_plan;
    QStringList m_dxvk; // options for DXVK_CONFIG, set once at the end
    bool m_gamescope = false;
    Screen m_screen; // the one gamescope shows the game on
    bool m_layer = false;
    bool m_capInGame = false; // by DXVK and VKD3D-Proton
    bool m_cappedByVkd3d = false;
    bool m_capWithGamescope = false;
    bool m_lodByDxvk = false;
};

} // namespace

bool needsScreen(const GameSettings &settings)
{
    return settings.gamescope;
}

bool runsThroughProton(const QStringList &command)
{
    static const QStringList verbs{QStringLiteral("waitforexitandrun"), QStringLiteral("run"),           QStringLiteral("waitforexit"),
                                   QStringLiteral("runinprefix"),       QStringLiteral("getcompatpath"), QStringLiteral("getnativepath")};
    for (qsizetype i = 0; i + 1 < command.size(); ++i) {
        const QString &argument = command.at(i);
        const qsizetype slash = argument.lastIndexOf(QLatin1Char('/'));
        if (QStringView(argument).sliced(slash + 1) == u"proton" && verbs.contains(command.at(i + 1))) {
            return true;
        }
    }
    return false;
}

LaunchPlan planLaunch(const GameSettings &settings, const LaunchContext &context)
{
    return Planner(settings, context).plan();
}

} // namespace gfxtune::launch
