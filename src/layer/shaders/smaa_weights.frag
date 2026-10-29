// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#version 450
#extension GL_GOOGLE_include_directive : require
#include "smaa.glsl"

layout(set = 0, binding = 0) uniform sampler2D edgesTex;  // linear
layout(set = 0, binding = 1) uniform sampler2D areaTex;   // linear
layout(set = 0, binding = 2) uniform sampler2D searchTex; // nearest

layout(location = 0) in vec2 texcoord;
layout(location = 1) in vec2 pixcoord;
layout(location = 2) in vec4 offset[3];
layout(location = 0) out vec4 weights;

void main()
{
    weights = SMAABlendingWeightCalculationPS(texcoord, pixcoord, offset, edgesTex, areaTex, searchTex, vec4(0.0));
}
