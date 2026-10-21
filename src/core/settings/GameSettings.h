// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include <QList>
#include <QString>

namespace gfxtune::settings
{

enum class VerticalSync {
    GameDefault,
    Off,      // present immediately, may tear
    On,       // wait for every refresh
    Fast,     // newest frame at each refresh, no tearing (mailbox)
    Adaptive, // wait, but tear when a frame is late (relaxed)
};

// gamescope's upscaling filters (-F) and scaling modes (-S).
enum class UpscaleFilter { Fsr, Nis, Sgsr, Linear, Nearest, Pixel };
enum class ScalingMode { Auto, Integer, Fit, Fill, Stretch };

// Everything gfxtune can set for one game. A default-constructed value means
// "leave the game alone".
struct GameSettings {
    // 0 means the game decides; otherwise 2, 4, 8 or 16.
    int anisotropicFiltering = 0;

    bool frameCapEnabled = false;
    int frameCap = 60;

    VerticalSync verticalSync = VerticalSync::GameDefault;

    bool fsr4Upgrade = false;

    bool gamescope = false;
    UpscaleFilter upscaleFilter = UpscaleFilter::Fsr;
    ScalingMode scalingMode = ScalingMode::Auto;
    // Percentage of the display resolution the game renders at.
    int renderScale = 100;
    // 0 (off) to 20 (strongest). gamescope counts the other way round.
    int sharpness = 10;

    static constexpr int MinFrameCap = 10;
    static constexpr int MaxFrameCap = 1000;
    static constexpr int MaxSharpness = 20;
    static const QList<int> AnisotropicLevels;
    static const QList<int> RenderScales;

    bool isDefault() const { return *this == GameSettings{}; }
    bool operator==(const GameSettings &) const = default;
};

QString toKey(VerticalSync value);
QString toKey(UpscaleFilter value);
QString toKey(ScalingMode value);
bool fromKey(const QString &text, VerticalSync &out);
bool fromKey(const QString &text, UpscaleFilter &out);
bool fromKey(const QString &text, ScalingMode &out);

} // namespace gfxtune::settings
