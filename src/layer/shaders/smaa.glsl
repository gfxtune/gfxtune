// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

// Shared set-up for the three SMAA passes: SMAA's own code (MIT, see
// third_party/smaa) with its "high" preset, the target's size from a push
// constant, and a triangle that covers the whole target.


layout(push_constant) uniform Params {
    vec4 metrics; // 1/width, 1/height, width, height
} p;

#define SMAA_RT_METRICS p.metrics
#define SMAA_GLSL_4
#define SMAA_PRESET_HIGH

#ifdef SMAA_VERTEX
#define SMAA_INCLUDE_PS 0
#else
#define SMAA_INCLUDE_VS 0
#endif

#include "SMAA.hlsl"

#ifdef SMAA_VERTEX
// Texture coordinates 0 to 1 across the target, top left first.
vec2 fullscreenTriangle()
{
    const vec2 corner = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
    return corner;
}
#endif
