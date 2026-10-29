// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#version 450
#extension GL_GOOGLE_include_directive : require
#include "smaa.glsl"

// The game's picture, display-encoded (an sRGB image is read through a
// plain view), as SMAA's luma edge detection expects.
layout(set = 0, binding = 0) uniform sampler2D colorTex;

layout(location = 0) in vec2 texcoord;
layout(location = 1) in vec4 offset[3];
layout(location = 0) out vec2 edges;

void main()
{
    edges = SMAALumaEdgeDetectionPS(texcoord, offset, colorTex);
}
