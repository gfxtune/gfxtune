// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "GameSettings.h"

#include <array>
#include <utility>

namespace gfxtune::settings
{

const QList<int> GameSettings::AnisotropicLevels{0, 2, 4, 8, 16};
// Native, then the usual upscaler presets: ultra quality, quality, balanced,
// performance, ultra performance, then supersampling.
const QList<int> GameSettings::RenderScales{100, 77, 67, 59, 50, 33, 125, 150, 200};
// Game decides, sharper in quarter steps, then softer.
const QList<int> GameSettings::LodBiasLevels{0, -25, -50, -75, -100, -150, -200, 50, 100};

namespace
{

// The names written to settings files: lower case, stable, readable.
constexpr std::array<std::pair<VerticalSync, const char *>, 5> VerticalSyncNames{{
    {VerticalSync::GameDefault, "game"},
    {VerticalSync::Off, "off"},
    {VerticalSync::On, "on"},
    {VerticalSync::Fast, "fast"},
    {VerticalSync::Adaptive, "adaptive"},
}};
constexpr std::array<std::pair<Fsr4, const char *>, 3> Fsr4Names{{
    {Fsr4::ProtonDefault, "proton"},
    {Fsr4::On, "on"},
    {Fsr4::Off, "off"},
}};
constexpr std::array<std::pair<UpscaleFilter, const char *>, 6> UpscaleFilterNames{{
    {UpscaleFilter::Fsr, "fsr"},
    {UpscaleFilter::Nis, "nis"},
    {UpscaleFilter::Sgsr, "sgsr"},
    {UpscaleFilter::Linear, "linear"},
    {UpscaleFilter::Nearest, "nearest"},
    {UpscaleFilter::Pixel, "pixel"},
}};
constexpr std::array<std::pair<ScalingMode, const char *>, 5> ScalingModeNames{{
    {ScalingMode::Auto, "auto"},
    {ScalingMode::Integer, "integer"},
    {ScalingMode::Fit, "fit"},
    {ScalingMode::Fill, "fill"},
    {ScalingMode::Stretch, "stretch"},
}};

template<typename Enum, std::size_t N>
QString nameOf(const std::array<std::pair<Enum, const char *>, N> &names, Enum value)
{
    for (const auto &[e, name] : names) {
        if (e == value) {
            return QString::fromLatin1(name);
        }
    }
    return {};
}

template<typename Enum, std::size_t N>
bool valueOf(const std::array<std::pair<Enum, const char *>, N> &names, const QString &text, Enum &out)
{
    const QString key = text.trimmed().toLower();
    for (const auto &[e, name] : names) {
        if (key == QLatin1String(name)) {
            out = e;
            return true;
        }
    }
    return false;
}

} // namespace

QString toKey(VerticalSync value)
{
    return nameOf(VerticalSyncNames, value);
}
QString toKey(Fsr4 value)
{
    return nameOf(Fsr4Names, value);
}
QString toKey(UpscaleFilter value)
{
    return nameOf(UpscaleFilterNames, value);
}
QString toKey(ScalingMode value)
{
    return nameOf(ScalingModeNames, value);
}
bool fromKey(const QString &text, VerticalSync &out)
{
    return valueOf(VerticalSyncNames, text, out);
}
bool fromKey(const QString &text, Fsr4 &out)
{
    return valueOf(Fsr4Names, text, out);
}
bool fromKey(const QString &text, UpscaleFilter &out)
{
    return valueOf(UpscaleFilterNames, text, out);
}
bool fromKey(const QString &text, ScalingMode &out)
{
    return valueOf(ScalingModeNames, text, out);
}

} // namespace gfxtune::settings
