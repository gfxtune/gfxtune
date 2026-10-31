// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include "filters/Filters.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace gfxtune::settings
{

enum class VerticalSync {
    GameDefault,
    Off,      // present immediately, may tear
    On,       // wait for every refresh
    Fast,     // newest frame at each refresh, no tearing (mailbox)
    Adaptive, // wait, but tear when a frame is late (relaxed)
};

// FSR 4 for games that ship FSR 3.1. Some Protons turn it on by themselves,
// others only when asked, so the default is whatever the game's Proton does.
enum class Fsr4 { ProtonDefault, On, Off };

// gamescope's upscaling filters (-F) and scaling modes (-S).
enum class UpscaleFilter { Fsr, Nis, Sgsr, Linear, Nearest, Pixel };
enum class ScalingMode { Auto, Integer, Fit, Fill, Stretch };

// Everything gfxtune can set for one game. A default-constructed value means
// "leave the game alone".
struct GameSettings {
    // 0 means the game decides; otherwise 2, 4, 8 or 16.
    int anisotropicFiltering = 0;
    // Texture detail: LOD bias in hundredths, added to the game's own.
    // Negative is sharper (and shimmers more), positive softer.
    int lodBias = 0;

    bool frameCapEnabled = false;
    int frameCap = 60;

    VerticalSync verticalSync = VerticalSync::GameDefault;

    Fsr4 fsr4 = Fsr4::ProtonDefault;

    bool gamescope = false;
    UpscaleFilter upscaleFilter = UpscaleFilter::Fsr;
    ScalingMode scalingMode = ScalingMode::Auto;
    // Percentage of the display resolution the game renders at; above 100
    // the game renders bigger and gamescope scales it down (supersampling).
    int renderScale = 100;
    // 0 (off) to 20 (strongest). gamescope counts the other way round.
    int sharpness = 10;

    // Drawn into the game's picture by gfxtune's Vulkan layer.
    filters::Values filters;
    // The user accepted the risk of the filters in a game that ships
    // anti-cheat; until then they stay locked for it.
    bool filtersRiskAccepted = false;

    // Frame rate, frame times and the card's readings, recorded by the
    // layer while the game runs.
    bool measurePerformance = true;

    // Start the game through these programs when installed.
    bool gameMode = false;
    bool mangoHud = false;

    static constexpr int MinFrameCap = 10;
    static constexpr int MaxFrameCap = 1000;
    static constexpr int MaxSharpness = 20;
    static const QList<int> AnisotropicLevels;
    static const QList<int> LodBiasLevels;
    static const QList<int> RenderScales;

    bool isDefault() const { return *this == GameSettings{}; }
    bool supersampling() const { return gamescope && renderScale > 100; }
    // Whether anything changes how the game starts or looks, measuring and
    // the accepted anti-cheat warning aside (see needsGfxtuneRun()).
    bool affectsGame() const
    {
        GameSettings rest = *this;
        rest.filtersRiskAccepted = false;
        rest.measurePerformance = true;
        return !rest.isDefault();
    }
    bool operator==(const GameSettings &) const = default;
};

// What gfxtune last found out about a game, kept in its settings file so
// gfxtune-run, which never scans Steam, knows it at launch.
struct GameFacts {
    QStringList antiCheat;
    bool openGlOnly = false; // a native game whose files only point to OpenGL

    bool operator==(const GameFacts &) const = default;
};

// gfxtune's Vulkan layer goes into a game for the filters or for measuring,
// unless the game ships anti-cheat and the user has not accepted the risk.
// It cannot reach a native game that draws with OpenGL.
inline bool layerAllowed(const GameSettings &s, const GameFacts &facts)
{
    return facts.antiCheat.isEmpty() || s.filtersRiskAccepted;
}

inline bool layerWanted(const GameSettings &s, const GameFacts &facts)
{
    return (s.filters.active() || s.measurePerformance) && layerAllowed(s, facts) && !facts.openGlOnly;
}

// Whether gfxtune-run has anything to do when the game starts: a setting
// applied from outside the game, or the layer (filters, measuring) where it
// can go in. Filters locked by anti-cheat, or out of reach in an OpenGL
// game, do nothing.
inline bool needsGfxtuneRun(const GameSettings &s, const GameFacts &facts)
{
    GameSettings outside = s;
    outside.filters = filters::Values{}; // the layer's alone
    return outside.affectsGame() || layerWanted(s, facts);
}

QString toKey(VerticalSync value);
QString toKey(Fsr4 value);
QString toKey(UpscaleFilter value);
QString toKey(ScalingMode value);
bool fromKey(const QString &text, VerticalSync &out);
bool fromKey(const QString &text, Fsr4 &out);
bool fromKey(const QString &text, UpscaleFilter &out);
bool fromKey(const QString &text, ScalingMode &out);

} // namespace gfxtune::settings
