// SPDX-License-Identifier: GPL-3.0-or-later AND MIT
// SPDX-FileCopyrightText: 2026 gfxtune contributors
// SPDX-FileCopyrightText: 2020 Advanced Micro Devices, Inc.

#pragma once

// The in-game filters as numbers: what the user sets, the ranges both the app
// and the Vulkan layer accept, the values handed to the shader, and the same
// arithmetic as the shader in C++ so tests can check what the GPU drew.
//
// Plain C++ without Qt: the layer loads into games and must not pull Qt in.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gfxtune::filters
{

struct Range {
    int min;
    int max;
    int neutral;

    constexpr int clamp(int value) const { return std::clamp(value, min, max); }
};

inline constexpr Range Sharpening{0, 100, 0};    // percent
inline constexpr Range Brightness{-50, 50, 0};   // ±50 is one photographic stop
inline constexpr Range Contrast{-50, 50, 0};     // ±50 halves or adds half
inline constexpr Range Gamma{50, 200, 100};      // hundredths: 0.50 to 2.00
inline constexpr Range Saturation{-100, 100, 0}; // -100 is greyscale
inline constexpr Range Vibrance{-100, 100, 0};   // like saturation, mostly on dull colours
inline constexpr Range Temperature{-100, 100, 0}; // negative cooler, positive warmer
inline constexpr Range AntiAliasing{0, 1, 0};    // off, or SMAA (its "high" preset)
inline constexpr Range LodBias{-200, 100, 0};    // hundredths, added to the game's own

// Anti-aliasing by name, as the settings file writes it.
inline constexpr const char *AntiAliasingNames[] = {"off", "smaa"};

struct Values {
    bool enabled = true; // the switch for comparing with the original picture
    int sharpening = Sharpening.neutral;
    int brightness = Brightness.neutral;
    int contrast = Contrast.neutral;
    int gamma = Gamma.neutral;
    int saturation = Saturation.neutral;
    int vibrance = Vibrance.neutral;
    int temperature = Temperature.neutral;
    int antiAliasing = AntiAliasing.neutral;

    bool colourNeutral() const
    {
        return brightness == Brightness.neutral && contrast == Contrast.neutral && gamma == Gamma.neutral
            && saturation == Saturation.neutral && vibrance == Vibrance.neutral && temperature == Temperature.neutral;
    }
    bool neutral() const { return sharpening == Sharpening.neutral && antiAliasing == AntiAliasing.neutral && colourNeutral(); }
    // Whether the layer has anything to draw.
    bool active() const { return enabled && !neutral(); }

    bool operator==(const Values &) const = default;
};

// What the layer reads from a game's settings file while the game runs.
struct LayerConfig {
    Values filters;
    int frameCap = 0; // frames per second; 0 is no cap
    int lodBias = 0;  // texture LOD bias in hundredths, added to the game's

    bool operator==(const LayerConfig &) const = default;
};

// The shader's push constants; the layout matches filters.frag.
struct ShaderParams {
    float sharpness = 0; // CAS, 0 to 1
    float exposure = 1;  // linear-light gain
    float contrast = 1;  // slope around mid-grey
    float invGamma = 1;
    float saturation = 1;
    float vibrance = 0;
    float redGain = 1; // linear-light white balance
    float blueGain = 1;
    std::uint32_t flags = 0;

    static constexpr std::uint32_t Sharpen = 1u << 0;
    static constexpr std::uint32_t Colour = 1u << 1;
    // The image holds linear light (an sRGB swapchain read through an sRGB
    // view); the filters always work on display-encoded values.
    static constexpr std::uint32_t LinearInput = 1u << 2;
};
static_assert(sizeof(ShaderParams) == 36);

inline ShaderParams shaderParams(const Values &v, bool linearInput)
{
    ShaderParams p;
    if (!v.enabled) {
        return p;
    }
    const int sharpening = Sharpening.clamp(v.sharpening);
    if (sharpening != Sharpening.neutral) {
        p.flags |= ShaderParams::Sharpen;
        p.sharpness = float(sharpening) / 100.0f;
    }
    if (!v.colourNeutral()) {
        p.flags |= ShaderParams::Colour;
        p.exposure = std::exp2(float(Brightness.clamp(v.brightness)) / 50.0f);
        p.contrast = 1.0f + float(Contrast.clamp(v.contrast)) / 100.0f;
        p.invGamma = 100.0f / float(Gamma.clamp(v.gamma));
        p.saturation = 1.0f + float(Saturation.clamp(v.saturation)) / 100.0f;
        p.vibrance = float(Vibrance.clamp(v.vibrance)) / 100.0f;
        const float warmth = float(Temperature.clamp(v.temperature)) / 100.0f;
        p.redGain = 1.0f + 0.2f * warmth;
        p.blueGain = 1.0f - 0.2f * warmth;
    }
    if (linearInput) {
        p.flags |= ShaderParams::LinearInput;
    }
    return p;
}

// ---- The shader's arithmetic, for tests ----

using Rgb = std::array<float, 3>;

inline float toLinear(float c)
{
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

inline float toEncoded(float c)
{
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

inline float luma(const Rgb &c)
{
    return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
}

// Colour adjustments on one display-encoded pixel, in the shader's order.
inline Rgb adjustColour(Rgb c, const ShaderParams &p)
{
    if (p.exposure != 1.0f) {
        for (float &x : c) {
            x = toEncoded(toLinear(x) * p.exposure);
        }
    }
    for (float &x : c) {
        x = (x - 0.5f) * p.contrast + 0.5f;
        x = std::pow(std::max(x, 0.0f), p.invGamma);
    }
    const float y = luma(c);
    for (float &x : c) {
        x = y + (x - y) * p.saturation;
    }
    const float spread = std::clamp(*std::max_element(c.begin(), c.end()) - *std::min_element(c.begin(), c.end()), 0.0f, 1.0f);
    const float vividness = 1.0f + p.vibrance * (1.0f - spread);
    const float y2 = luma(c);
    for (float &x : c) {
        x = y2 + (x - y2) * vividness;
    }
    if (p.redGain != 1.0f || p.blueGain != 1.0f) {
        c[0] = toEncoded(toLinear(std::max(c[0], 0.0f)) * p.redGain);
        c[2] = toEncoded(toLinear(std::max(c[2], 0.0f)) * p.blueGain);
    }
    for (float &x : c) {
        x = std::clamp(x, 0.0f, 1.0f);
    }
    return c;
}

// AMD FidelityFX Contrast Adaptive Sharpening for one pixel, from its 3x3
// neighbourhood (row by row, centre at index 4). Follows AMD's MIT-licensed
// CAS; the notice is in filters.frag and NOTICE.md.
inline Rgb sharpen(const std::array<Rgb, 9> &n, float sharpness)
{
    const float peak = -1.0f / (8.0f + (5.0f - 8.0f) * std::clamp(sharpness, 0.0f, 1.0f));
    Rgb out{};
    for (int ch = 0; ch < 3; ++ch) {
        const auto at = [&](int i) { return n[size_t(i)][size_t(ch)]; };
        // Cross (b, d, e, f, h), then with the corners: soft minimum and maximum.
        const float crossMin = std::min({at(1), at(3), at(4), at(5), at(7)});
        const float crossMax = std::max({at(1), at(3), at(4), at(5), at(7)});
        const float boxMin = std::min({crossMin, at(0), at(2), at(6), at(8)});
        const float boxMax = std::max({crossMax, at(0), at(2), at(6), at(8)});
        const float softMin = crossMin + boxMin;
        const float softMax = crossMax + boxMax;
        const float amount = std::sqrt(std::clamp(std::min(softMin, 2.0f - softMax) / std::max(softMax, 1.0f / 65536.0f), 0.0f, 1.0f));
        const float w = amount * peak;
        const float value = (at(1) * w + at(3) * w + at(5) * w + at(7) * w + at(4)) / (1.0f + 4.0f * w);
        out[size_t(ch)] = std::clamp(value, 0.0f, 1.0f);
    }
    return out;
}

} // namespace gfxtune::filters
