// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors
//
// The sharpening step follows AMD FidelityFX Contrast Adaptive Sharpening:
//
//   Copyright (c) 2020 Advanced Micro Devices, Inc. All rights reserved.
//
//   Permission is hereby granted, free of charge, to any person obtaining a
//   copy of this software and associated documentation files (the
//   "Software"), to deal in the Software without restriction, including
//   without limitation the rights to use, copy, modify, merge, publish,
//   distribute, sublicense, and/or sell copies of the Software, and to permit
//   persons to whom the Software is furnished to do so, subject to the
//   following conditions:
//
//   The above copyright notice and this permission notice shall be included
//   in all copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
//   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
//   NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
//   DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
//   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
//   USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// src/filters/Filters.h does the same arithmetic in C++ for the tests; keep
// the two in step.

#version 450

layout(set = 0, binding = 0) uniform sampler2D source;

layout(push_constant) uniform Params {
    float sharpness;
    float exposure;
    float contrast;
    float invGamma;
    float saturation;
    float vibrance;
    float redGain;
    float blueGain;
    uint flags;
} p;

layout(location = 0) out vec4 colour;

const uint Sharpen = 1u;
const uint Colour = 2u;
const uint LinearInput = 4u;
const vec3 Luma = vec3(0.2126, 0.7152, 0.0722);

float toLinear(float c)
{
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

float toEncoded(float c)
{
    return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

vec3 toLinear(vec3 c)
{
    return vec3(toLinear(c.r), toLinear(c.g), toLinear(c.b));
}

vec3 toEncoded(vec3 c)
{
    return vec3(toEncoded(c.r), toEncoded(c.g), toEncoded(c.b));
}

// The filters work on display-encoded values whatever the swapchain format.
vec3 fetch(ivec2 at, ivec2 size)
{
    const vec3 c = texelFetch(source, clamp(at, ivec2(0), size - 1), 0).rgb;
    return (p.flags & LinearInput) != 0u ? toEncoded(c) : c;
}

vec3 sharpen(ivec2 at, ivec2 size)
{
    // a b c
    // d e f
    // g h i
    const vec3 a = fetch(at + ivec2(-1, -1), size);
    const vec3 b = fetch(at + ivec2(0, -1), size);
    const vec3 c = fetch(at + ivec2(1, -1), size);
    const vec3 d = fetch(at + ivec2(-1, 0), size);
    const vec3 e = fetch(at, size);
    const vec3 f = fetch(at + ivec2(1, 0), size);
    const vec3 g = fetch(at + ivec2(-1, 1), size);
    const vec3 h = fetch(at + ivec2(0, 1), size);
    const vec3 i = fetch(at + ivec2(1, 1), size);

    // Soft minimum and maximum: the cross plus the whole 3x3 box, 0 to 2.
    const vec3 crossMin = min(min(min(d, e), min(f, b)), h);
    const vec3 crossMax = max(max(max(d, e), max(f, b)), h);
    const vec3 softMin = crossMin + min(min(min(crossMin, a), min(c, g)), i);
    const vec3 softMax = crossMax + max(max(max(crossMax, a), max(c, g)), i);

    // Less sharpening where the area is already near black or white.
    const vec3 amount = sqrt(clamp(min(softMin, 2.0 - softMax) / max(softMax, vec3(1.0 / 65536.0)), 0.0, 1.0));
    const float peak = -1.0 / mix(8.0, 5.0, clamp(p.sharpness, 0.0, 1.0));
    const vec3 w = amount * peak;
    return clamp((b * w + d * w + f * w + h * w + e) / (1.0 + 4.0 * w), 0.0, 1.0);
}

vec3 adjustColour(vec3 c)
{
    if (p.exposure != 1.0) {
        c = toEncoded(toLinear(c) * p.exposure);
    }
    c = (c - 0.5) * p.contrast + 0.5;
    c = pow(max(c, 0.0), vec3(p.invGamma));

    const float y = dot(c, Luma);
    c = y + (c - y) * p.saturation;

    // Vibrance saturates dull colours more than vivid ones.
    const float spread = clamp(max(max(c.r, c.g), c.b) - min(min(c.r, c.g), c.b), 0.0, 1.0);
    const float y2 = dot(c, Luma);
    c = y2 + (c - y2) * (1.0 + p.vibrance * (1.0 - spread));

    // Warmer or cooler: red and blue gain in linear light.
    if (p.redGain != 1.0 || p.blueGain != 1.0) {
        c.r = toEncoded(toLinear(max(c.r, 0.0)) * p.redGain);
        c.b = toEncoded(toLinear(max(c.b, 0.0)) * p.blueGain);
    }
    return clamp(c, 0.0, 1.0);
}

void main()
{
    const ivec2 size = textureSize(source, 0);
    const ivec2 at = ivec2(gl_FragCoord.xy);
    const float alpha = texelFetch(source, at, 0).a;

    vec3 c = (p.flags & Sharpen) != 0u ? sharpen(at, size) : fetch(at, size);
    if ((p.flags & Colour) != 0u) {
        c = adjustColour(c);
    }
    colour = vec4((p.flags & LinearInput) != 0u ? toLinear(c) : c, alpha);
}
