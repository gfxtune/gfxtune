// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#version 450
#extension GL_GOOGLE_include_directive : require
#define SMAA_VERTEX
#include "smaa.glsl"

layout(location = 0) out vec2 texcoord;
layout(location = 1) out vec4 offset;

void main()
{
    texcoord = fullscreenTriangle();
    SMAANeighborhoodBlendingVS(texcoord, offset);
}
