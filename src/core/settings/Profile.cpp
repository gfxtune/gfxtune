// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "Profile.h"

namespace gfxtune::settings
{

namespace
{

// Keeps the table below to one line per setting.
#define GFXTUNE_COPY(field) [](const GameSettings &from, GameSettings &to) { to.field = from.field; }

} // namespace

const QList<SettingKind> &settingKinds()
{
    static const QList<SettingKind> kinds{
        {QStringLiteral("anisotropicFiltering"), {QStringLiteral("image/anisotropic_filtering")}, GFXTUNE_COPY(anisotropicFiltering)},
        {QStringLiteral("lodBias"), {QStringLiteral("image/lod_bias")}, GFXTUNE_COPY(lodBias)},
        {QStringLiteral("frameCap"),
         {QStringLiteral("frames/frame_cap_enabled"), QStringLiteral("frames/frame_cap")},
         [](const GameSettings &from, GameSettings &to) {
             to.frameCapEnabled = from.frameCapEnabled;
             to.frameCap = from.frameCap;
         }},
        {QStringLiteral("verticalSync"), {QStringLiteral("frames/vertical_sync")}, GFXTUNE_COPY(verticalSync)},
        {QStringLiteral("fsr4"), {QStringLiteral("upscaling/fsr4")}, GFXTUNE_COPY(fsr4)},
        {QStringLiteral("filtersEnabled"), {QStringLiteral("filters/enabled")}, GFXTUNE_COPY(filters.enabled)},
        {QStringLiteral("antiAliasing"), {QStringLiteral("filters/anti_aliasing")}, GFXTUNE_COPY(filters.antiAliasing)},
        {QStringLiteral("filterSharpening"), {QStringLiteral("filters/sharpening")}, GFXTUNE_COPY(filters.sharpening)},
        {QStringLiteral("filterBrightness"), {QStringLiteral("filters/brightness")}, GFXTUNE_COPY(filters.brightness)},
        {QStringLiteral("filterContrast"), {QStringLiteral("filters/contrast")}, GFXTUNE_COPY(filters.contrast)},
        {QStringLiteral("filterGamma"), {QStringLiteral("filters/gamma")}, GFXTUNE_COPY(filters.gamma)},
        {QStringLiteral("filterSaturation"), {QStringLiteral("filters/saturation")}, GFXTUNE_COPY(filters.saturation)},
        {QStringLiteral("filterVibrance"), {QStringLiteral("filters/vibrance")}, GFXTUNE_COPY(filters.vibrance)},
        {QStringLiteral("filterTemperature"), {QStringLiteral("filters/temperature")}, GFXTUNE_COPY(filters.temperature)},
        {QStringLiteral("gamescope"), {QStringLiteral("gamescope/enabled")}, GFXTUNE_COPY(gamescope)},
        {QStringLiteral("renderScale"), {QStringLiteral("gamescope/render_scale")}, GFXTUNE_COPY(renderScale)},
        {QStringLiteral("upscaleFilter"), {QStringLiteral("gamescope/filter")}, GFXTUNE_COPY(upscaleFilter)},
        {QStringLiteral("sharpness"), {QStringLiteral("gamescope/sharpness")}, GFXTUNE_COPY(sharpness)},
        {QStringLiteral("scalingMode"), {QStringLiteral("gamescope/scaling")}, GFXTUNE_COPY(scalingMode)},
        {QStringLiteral("gameMode"), {QStringLiteral("launch/gamemode")}, GFXTUNE_COPY(gameMode)},
        {QStringLiteral("mangoHud"), {QStringLiteral("launch/mangohud")}, GFXTUNE_COPY(mangoHud)},
        {QStringLiteral("measurePerformance"), {QStringLiteral("performance/measure")}, GFXTUNE_COPY(measurePerformance)},
    };
    return kinds;
}

#undef GFXTUNE_COPY

const SettingKind *findSetting(const QString &id)
{
    for (const SettingKind &kind : settingKinds()) {
        if (kind.id == id) {
            return &kind;
        }
    }
    return nullptr;
}

bool sameSetting(const SettingKind &kind, const GameSettings &a, const GameSettings &b)
{
    GameSettings mixed = a;
    kind.copy(b, mixed);
    return mixed == a;
}

bool GameProfile::operator==(const GameProfile &other) const
{
    if (usesGlobal != other.usesGlobal || changed != other.changed || own.filtersRiskAccepted != other.own.filtersRiskAccepted) {
        return false;
    }
    for (const QString &id : changed) {
        const SettingKind *kind = findSetting(id);
        if (kind && !sameSetting(*kind, own, other.own)) {
            return false;
        }
    }
    return true;
}

GameSettings untouchedSettings()
{
    GameSettings s;
    s.measurePerformance = false;
    return s;
}

GameSettings effectiveSettings(const GameSettings &global, const GameProfile &profile)
{
    GameSettings s = profile.usesGlobal ? global : untouchedSettings();
    for (const SettingKind &kind : settingKinds()) {
        if (profile.changed.contains(kind.id)) {
            kind.copy(profile.own, s);
        }
    }
    s.filtersRiskAccepted = profile.own.filtersRiskAccepted;
    return s;
}

bool holdsBackForAntiCheat(const GameSettings &global, const GameProfile &profile, const GameFacts &facts)
{
    return !facts.antiCheat.isEmpty() && !profile.own.filtersRiskAccepted && profile.usesGlobal && profile.changed.isEmpty()
        && (global.filters.active() || global.measurePerformance);
}

bool addsItselfTo(const GameSettings &global, const GameProfile &profile, const GameFacts &facts)
{
    return needsGfxtuneRun(effectiveSettings(global, profile), facts) && !holdsBackForAntiCheat(global, profile, facts);
}

GameProfile profileFrom(const GameSettings &settings, const GameSettings &base)
{
    GameProfile profile;
    profile.own = settings;
    for (const SettingKind &kind : settingKinds()) {
        if (!sameSetting(kind, settings, base)) {
            profile.changed.insert(kind.id);
        }
    }
    return profile;
}

} // namespace gfxtune::settings
