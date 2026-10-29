// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#version 450
#extension GL_GOOGLE_include_directive : require
#include "smaa.glsl"

// The game's picture through its own view (linear light for sRGB images,
// as SMAA prefers for blending), and the weights from the previous pass.
layout(set = 0, binding = 0) uniform sampler2D colorTex; // linear filtering
layout(set = 0, binding = 1) uniform sampler2D blendTex; // linear filtering

layout(location = 0) in vec2 texcoord;
layout(location = 1) in vec4 offset;
layout(location = 0) out vec4 colour;

void main()
{
    colour = SMAANeighborhoodBlendingPS(texcoord, offset, colorTex, blendTex);
}
