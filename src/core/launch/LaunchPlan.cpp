// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "LaunchPlan.h"

#include <QRegularExpression>
#include <QtMath>

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
        return QStringLiteral("linear scaling");
    case UpscaleFilter::Nearest:
        return QStringLiteral("nearest-neighbor scaling");
    case UpscaleFilter::Pixel:
        return QStringLiteral("pixel-art scaling");
    }
    return {};
}

int evenAtLeast(double value, int minimum)
{
    const int rounded = qMax(minimum, qRound(value));
    return rounded - (rounded % 2);
}

class Planner
{
public:
    Planner(const LaunchContext &context, LaunchPlan &plan)
        : m_context(context)
        , m_plan(plan)
    {
    }

    // Sets a variable unless the user's launch options already did.
    bool set(const QString &name, const QString &value)
    {
        if (m_context.environment.contains(name)) {
            if (!m_plan.keptFromSteam.contains(name)) {
                m_plan.keptFromSteam.append(name);
            }
            return false;
        }
        m_plan.environment.append({name, value});
        return true;
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
            if (!m_plan.keptFromSteam.contains(name)) {
                m_plan.keptFromSteam.append(name);
            }
            return false;
        }
        m_plan.environment.append({name, *own + QLatin1Char(';') + entry});
        return true;
    }

    const LaunchContext &m_context;
    LaunchPlan &m_plan;
};

} // namespace

bool needsScreen(const GameSettings &settings)
{
    return settings.gamescope;
}

bool runsThroughProton(const QStringList &command)
{
    static const QStringList verbs{
        QStringLiteral("waitforexitandrun"), QStringLiteral("run"), QStringLiteral("waitforexit"),
        QStringLiteral("runinprefix"), QStringLiteral("getcompatpath"), QStringLiteral("getnativepath"),
    };
    for (qsizetype i = 0; i + 1 < command.size(); ++i) {
        const QString &argument = command.at(i);
        const qsizetype slash = argument.lastIndexOf(QLatin1Char('/'));
        if (QStringView(argument).sliced(slash + 1) == u"proton" && verbs.contains(command.at(i + 1))) {
            return true;
        }
    }
    return false;
}

LaunchPlan planLaunch(const GameSettings &s, const LaunchContext &context)
{
    LaunchPlan plan;
    Planner p(context, plan);
    QStringList dxvk; // options for DXVK_CONFIG

    bool gamescope = false;
    if (s.gamescope) {
        if (context.environment.contains(QStringLiteral("GAMESCOPE_WAYLAND_DISPLAY"))) {
            plan.keptFromSteam.append(QStringLiteral("gamescope"));
        } else if (!context.gamescopeInstalled) {
            plan.notApplied.append(QStringLiteral("gamescope: not installed"));
        } else if (!context.screen || context.screen->width <= 0 || context.screen->height <= 0) {
            plan.notApplied.append(QStringLiteral("gamescope: the screen size could not be read"));
        } else {
            gamescope = true;
        }
    }

    // gfxtune's Vulkan layer loads only with GFXTUNE_FILTERS set, for the
    // filters or for measuring. It follows the settings file while the game
    // runs, and also takes over the frame cap and texture detail, the same
    // way for Vulkan, DXVK and VKD3D-Proton games.
    bool layer = false;
    if (settings::layerWanted(s, context.facts) && !context.filterSettingsPath.isEmpty()
        && p.set(QStringLiteral("GFXTUNE_FILTERS"), QStringLiteral("1"))) {
        layer = true;
        plan.environment.append({QStringLiteral("GFXTUNE_FILTERS_SETTINGS"), context.filterSettingsPath});
        if (!context.filterReportPath.isEmpty()) {
            plan.environment.append({QStringLiteral("GFXTUNE_FILTERS_REPORT"), context.filterReportPath});
        }
        if (s.filters.active()) {
            plan.applied.append(QStringLiteral("In-game filters"));
        }
        if (s.measurePerformance && !context.performanceDir.isEmpty()) {
            plan.environment.append({QStringLiteral("GFXTUNE_PERF_DIR"), context.performanceDir});
            plan.applied.append(QStringLiteral("Performance measuring"));
        }
    } else if (!settings::layerAllowed(s, context.facts) && (s.filters.active() || s.measurePerformance)) {
        plan.notApplied.append(QStringLiteral("In-game filters and measuring: locked until the anti-cheat risk is accepted"));
    }

    if (s.anisotropicFiltering > 0) {
        const QString level = QString::number(s.anisotropicFiltering);
        const bool driver = p.set(QStringLiteral("RADV_TEX_ANISO"), level);
        dxvk << QStringLiteral("d3d11.samplerAnisotropy = ") + level << QStringLiteral("d3d9.samplerAnisotropy = ") + level;
        if (driver) {
            plan.applied.append(QStringLiteral("Anisotropic filtering %1×").arg(level));
        }
    }

    bool lodByDxvk = false;
    if (s.lodBias != 0) {
        const QString bias = QString::number(double(s.lodBias) / 100.0, 'f', 2);
        if (layer) {
            plan.applied.append(QStringLiteral("Texture detail %1").arg(bias));
        } else if (context.proton) {
            dxvk << QStringLiteral("d3d11.samplerLodBias = ") + bias << QStringLiteral("d3d9.samplerLodBias = ") + bias;
            lodByDxvk = true;
        } else {
            plan.notApplied.append(QStringLiteral("Texture detail: needs gfxtune's layer, which is off for this game"));
        }
    }

    if (s.verticalSync != VerticalSync::GameDefault) {
        const bool vulkan = p.set(QStringLiteral("MESA_VK_WSI_PRESENT_MODE"), presentMode(s.verticalSync));
        // OpenGL through Mesa knows only off and on.
        if (s.verticalSync == VerticalSync::Off || s.verticalSync == VerticalSync::On) {
            p.set(QStringLiteral("vblank_mode"), s.verticalSync == VerticalSync::Off ? QStringLiteral("0") : QStringLiteral("3"));
        }
        if (vulkan) {
            plan.applied.append(QStringLiteral("Vertical sync: %1").arg(verticalSyncWords(s.verticalSync)));
        }
    }

    // One limiter, never two at once: two on the same rate drift against
    // each other and stutter. The layer's paces every Vulkan game exactly;
    // without it, DXVK and VKD3D-Proton cap Proton games inside the game.
    // gamescope's -r only sets the refresh rate of its own screen, which
    // holds a game back only while it waits for vertical sync: it is the
    // cap only for native games the layer does not reach.
    const bool capByLayer = s.frameCapEnabled && layer;
    const bool capWithGamescope = s.frameCapEnabled && !layer && gamescope && !context.proton;
    const bool capInGame = s.frameCapEnabled && !capByLayer && !capWithGamescope;
    bool cappedByVkd3d = false;
    if (capByLayer) {
        plan.applied.append(QStringLiteral("Frame rate cap %1").arg(s.frameCap));
    } else if (capInGame) {
        const QString fps = QString::number(s.frameCap);
        dxvk << QStringLiteral("dxgi.maxFrameRate = ") + fps << QStringLiteral("d3d9.maxFrameRate = ") + fps;
        cappedByVkd3d = p.set(QStringLiteral("VKD3D_FRAME_RATE"), fps);
    }

    switch (s.fsr4) {
    case Fsr4::On:
        if (p.set(QStringLiteral("PROTON_FSR4_UPGRADE"), QStringLiteral("1"))) {
            plan.applied.append(QStringLiteral("FSR 4 on"));
        }
        break;
    case Fsr4::Off: {
        // proton-cachyos and GE-Proton read the variable; Valve's Proton has
        // no switch but copies AMD's FSR 4 library into the prefix, so Wine
        // is also told not to load it. The game then keeps its FSR 3.1.
        const bool variable = p.set(QStringLiteral("PROTON_FSR4_UPGRADE"), QStringLiteral("0"));
        const bool library = p.overrideDll(QStringLiteral("amdxcffx64"), QStringLiteral("d"));
        if (variable && library) {
            plan.applied.append(QStringLiteral("FSR 4 off (the game keeps FSR 3.1)"));
        }
        break;
    }
    case Fsr4::ProtonDefault:
        break;
    }

    const bool dxvkSet = !dxvk.isEmpty() && p.set(QStringLiteral("DXVK_CONFIG"), dxvk.join(QStringLiteral("; ")));
    if (capInGame && (dxvkSet || cappedByVkd3d)) {
        plan.applied.append(QStringLiteral("Frame rate cap %1").arg(s.frameCap));
    }
    if (lodByDxvk && dxvkSet) {
        plan.applied.append(QStringLiteral("Texture detail %1 (DirectX 9 to 11)").arg(QString::number(double(s.lodBias) / 100.0, 'f', 2)));
    }

    // The programs the game starts through, outermost first: GameMode for
    // everything, then gamescope, then MangoHud right around the game so its
    // overlay lands in the game's own picture. Inside gamescope MangoHud's
    // layer is not supported; gamescope starts its mangoapp instead.
    if (s.gameMode) {
        if (context.environment.value(QStringLiteral("LD_PRELOAD")).contains(QLatin1String("gamemodeauto"))) {
            plan.keptFromSteam.append(QStringLiteral("gamemoderun"));
        } else if (!context.gameModeInstalled) {
            plan.notApplied.append(QStringLiteral("GameMode: not installed"));
        } else {
            plan.wrapper << QStringLiteral("gamemoderun");
            plan.applied.append(QStringLiteral("GameMode"));
        }
    }

    enum class Hud { None, Wrapper, Gamescope } hud = Hud::None;
    if (s.mangoHud) {
        if (context.environment.contains(QStringLiteral("MANGOHUD"))) {
            plan.keptFromSteam.append(QStringLiteral("MangoHud"));
        } else if (gamescope && !context.mangoAppInstalled) {
            plan.notApplied.append(QStringLiteral("MangoHud: inside gamescope it needs mangoapp, which is not installed"));
        } else if (!gamescope && !context.mangoHudInstalled) {
            plan.notApplied.append(QStringLiteral("MangoHud: not installed"));
        } else {
            hud = gamescope ? Hud::Gamescope : Hud::Wrapper;
        }
    }

    if (gamescope) {
        const Screen screen = *context.screen;
        const double scale = s.renderScale / 100.0;
        const int width = evenAtLeast(screen.width * scale, 320);
        const int height = evenAtLeast(screen.height * scale, 200);
        // Drawn bigger than the screen: scaled down, where upscalers and
        // their sharpening do not apply.
        const bool supersampling = s.renderScale > 100;
        plan.wrapper << QStringLiteral("gamescope")
                     << QStringLiteral("-W") << QString::number(screen.width)
                     << QStringLiteral("-H") << QString::number(screen.height)
                     << QStringLiteral("-w") << QString::number(width)
                     << QStringLiteral("-h") << QString::number(height);
        if (capWithGamescope) {
            plan.wrapper << QStringLiteral("-r") << QString::number(s.frameCap);
            plan.applied.append(QStringLiteral("Frame rate cap %1 (gamescope, while vertical sync is on)").arg(s.frameCap));
        }
        plan.wrapper << QStringLiteral("-F") << (supersampling ? QStringLiteral("linear") : settings::toKey(s.upscaleFilter));
        plan.wrapper << QStringLiteral("-S") << settings::toKey(s.scalingMode);
        if (!supersampling && s.upscaleFilter == UpscaleFilter::Fsr) {
            // gamescope counts 0 as the strongest and 20 as the weakest.
            plan.wrapper << QStringLiteral("--sharpness") << QString::number(GameSettings::MaxSharpness - s.sharpness);
        }
        if (hud == Hud::Gamescope) {
            plan.wrapper << QStringLiteral("--mangoapp");
        }
        plan.wrapper << QStringLiteral("-f") << QStringLiteral("--");
        if (supersampling) {
            plan.applied.append(QStringLiteral("gamescope: drawn at %1×%2, scaled down to %3×%4").arg(width).arg(height).arg(screen.width).arg(screen.height));
        } else {
            plan.applied.append(QStringLiteral("gamescope: drawn at %1×%2, shown at %3×%4 with %5")
                                    .arg(width)
                                    .arg(height)
                                    .arg(screen.width)
                                    .arg(screen.height)
                                    .arg(filterWords(s.upscaleFilter)));
        }
    }

    if (hud == Hud::Gamescope) {
        plan.applied.append(QStringLiteral("MangoHud (gamescope's mangoapp)"));
    } else if (hud == Hud::Wrapper) {
        plan.wrapper << QStringLiteral("mangohud");
        plan.applied.append(QStringLiteral("MangoHud"));
    }
    return plan;
}

} // namespace gfxtune::launch
