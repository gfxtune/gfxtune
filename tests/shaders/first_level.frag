// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#version 450

// Reads the source at its first level of detail, plus the bias its sampler
// adds: what the layer's texture detail changes.
layout(binding = 0) uniform sampler2D source;
layout(location = 0) out vec4 color;

void main()
{
    color = textureLod(source, vec2(0.5), 0.0);
}
