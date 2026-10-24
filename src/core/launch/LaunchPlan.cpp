// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "LaunchPlan.h"

#include <QtMath>

namespace gfxtune::launch
{

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

    // gamescope first: for games outside Proton it is what caps the frame rate.
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

    if (s.anisotropicFiltering > 0) {
        const QString level = QString::number(s.anisotropicFiltering);
        const bool driver = p.set(QStringLiteral("RADV_TEX_ANISO"), level);
        dxvk << QStringLiteral("d3d11.samplerAnisotropy = ") + level << QStringLiteral("d3d9.samplerAnisotropy = ") + level;
        if (driver) {
            plan.applied.append(QStringLiteral("Anisotropic filtering %1×").arg(level));
        }
    }

    if (s.verticalSync != VerticalSync::GameDefault) {
        if (p.set(QStringLiteral("MESA_VK_WSI_PRESENT_MODE"), presentMode(s.verticalSync))) {
            plan.applied.append(QStringLiteral("Vertical sync: %1").arg(verticalSyncWords(s.verticalSync)));
        }
    }

    // DXVK caps DirectX 9 to 11 and VKD3D-Proton DirectX 12, exactly and inside
    // the game, so under Proton they cap it whether gamescope runs or not.
    // gamescope's -r only sets the refresh rate of its own screen, which holds
    // a game back only while the game waits for vertical sync: it is the cap
    // only for games outside Proton.
    const bool capWithGamescope = s.frameCapEnabled && gamescope && !context.proton;
    const bool capInGame = s.frameCapEnabled && !capWithGamescope;
    bool cappedByVkd3d = false;
    if (capInGame) {
        const QString fps = QString::number(s.frameCap);
        dxvk << QStringLiteral("dxgi.maxFrameRate = ") + fps << QStringLiteral("d3d9.maxFrameRate = ") + fps;
        cappedByVkd3d = p.set(QStringLiteral("VKD3D_FRAME_RATE"), fps);
    }

    if (s.fsr4Upgrade && p.set(QStringLiteral("PROTON_FSR4_UPGRADE"), QStringLiteral("1"))) {
        plan.applied.append(QStringLiteral("FSR 4 upgrade"));
    }

    const bool dxvkSet = !dxvk.isEmpty() && p.set(QStringLiteral("DXVK_CONFIG"), dxvk.join(QStringLiteral("; ")));
    if (capInGame && (dxvkSet || cappedByVkd3d)) {
        plan.applied.append(QStringLiteral("Frame rate cap %1").arg(s.frameCap));
    }

    if (gamescope) {
        const Screen screen = *context.screen;
        const double scale = s.renderScale / 100.0;
        const int width = evenAtLeast(screen.width * scale, 320);
        const int height = evenAtLeast(screen.height * scale, 200);
        plan.wrapper = {
            QStringLiteral("gamescope"),
            QStringLiteral("-W"), QString::number(screen.width),
            QStringLiteral("-H"), QString::number(screen.height),
            QStringLiteral("-w"), QString::number(width),
            QStringLiteral("-h"), QString::number(height),
        };
        if (capWithGamescope) {
            plan.wrapper << QStringLiteral("-r") << QString::number(s.frameCap);
            plan.applied.append(QStringLiteral("Frame rate cap %1 (gamescope, while vertical sync is on)").arg(s.frameCap));
        }
        plan.wrapper << QStringLiteral("-F") << settings::toKey(s.upscaleFilter);
        plan.wrapper << QStringLiteral("-S") << settings::toKey(s.scalingMode);
        if (s.upscaleFilter == UpscaleFilter::Fsr) {
            // gamescope counts 0 as the strongest and 20 as the weakest.
            plan.wrapper << QStringLiteral("--sharpness") << QString::number(GameSettings::MaxSharpness - s.sharpness);
        }
        plan.wrapper << QStringLiteral("-f") << QStringLiteral("--");
        plan.applied.append(QStringLiteral("gamescope: drawn at %1×%2, shown at %3×%4 with %5")
                                .arg(width)
                                .arg(height)
                                .arg(screen.width)
                                .arg(screen.height)
                                .arg(filterWords(s.upscaleFilter)));
    }
    return plan;
}

} // namespace gfxtune::launch
