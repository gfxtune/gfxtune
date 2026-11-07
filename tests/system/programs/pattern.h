// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

// The test games' shared parts: their options, the picture they show and
// the report they write. Plain C, so the Vulkan game (C) and the Windows
// games (C++, built with MinGW) use the same code.
//
// Options:
//   --seconds N        how long to run (default 5)
//   --size WxH         window size (default 320x240)
//   --colour R,G,B     the colour shown, 0-255 each (default 255,0,0)
//   --stairs           a second colour (the inverse) below a diagonal edge
//                      with hard steps, for anti-aliasing to smooth
//   --result PATH      where to write the report
//   --title TEXT       window title, for finding the window
//
// The report, written once the first frame is shown and again at the end:
//   started=1
//   frames=N, seconds=S, fps=F (fps over the whole run)

#ifndef GFXTUNE_TEST_PATTERN_H
#define GFXTUNE_TEST_PATTERN_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    double seconds;
    uint32_t width;
    uint32_t height;
    uint8_t colour[3];
    int stairs;
    const char *result;
    const char *title;
} GameOptions;

static int parse_options(int argc, char **argv, GameOptions *options)
{
    options->seconds = 5;
    options->width = 320;
    options->height = 240;
    options->colour[0] = 255;
    options->colour[1] = 0;
    options->colour[2] = 0;
    options->stairs = 0;
    options->result = NULL;
    options->title = "gfxtune test game";
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        const char *value = i + 1 < argc ? argv[i + 1] : NULL;
        if (strcmp(arg, "--stairs") == 0) {
            options->stairs = 1;
            continue;
        }
        if (!value) {
            fprintf(stderr, "%s needs a value\n", arg);
            return 0;
        }
        ++i;
        if (strcmp(arg, "--seconds") == 0) {
            options->seconds = atof(value);
        } else if (strcmp(arg, "--size") == 0) {
            unsigned w = 0, h = 0;
            if (sscanf(value, "%ux%u", &w, &h) != 2 || w == 0 || h == 0 || w > 4096 || h > 4096) {
                fprintf(stderr, "bad --size %s\n", value);
                return 0;
            }
            options->width = w;
            options->height = h;
        } else if (strcmp(arg, "--colour") == 0) {
            unsigned r = 0, g = 0, b = 0;
            if (sscanf(value, "%u,%u,%u", &r, &g, &b) != 3 || r > 255 || g > 255 || b > 255) {
                fprintf(stderr, "bad --colour %s\n", value);
                return 0;
            }
            options->colour[0] = (uint8_t)r;
            options->colour[1] = (uint8_t)g;
            options->colour[2] = (uint8_t)b;
        } else if (strcmp(arg, "--result") == 0) {
            options->result = value;
        } else if (strcmp(arg, "--title") == 0) {
            options->title = value;
        } else {
            fprintf(stderr, "unknown option %s\n", arg);
            return 0;
        }
    }
    return 1;
}

// The picture, as 8-bit pixels in the given channel order (0 = red first,
// 1 = blue first), four bytes each, rows top to bottom.
static void fill_pattern(const GameOptions *options, uint8_t *pixels, int blue_first)
{
    const uint8_t *a = options->colour;
    const uint8_t b[3] = {(uint8_t)(255 - a[0]), (uint8_t)(255 - a[1]), (uint8_t)(255 - a[2])};
    for (uint32_t y = 0; y < options->height; ++y) {
        for (uint32_t x = 0; x < options->width; ++x) {
            // A shallow diagonal, one pixel down every three across: long
            // hard steps, the case SMAA is made for.
            const int below = options->stairs && (int)y > (int)options->height / 3 + (int)x / 3;
            const uint8_t *c = below ? b : a;
            uint8_t *p = pixels + 4 * ((size_t)y * options->width + x);
            p[0] = blue_first ? c[2] : c[0];
            p[1] = c[1];
            p[2] = blue_first ? c[0] : c[2];
            p[3] = 255;
        }
    }
}

static void write_report(const GameOptions *options, long frames, double seconds)
{
    if (!options->result) {
        return;
    }
    FILE *out = fopen(options->result, "w");
    if (!out) {
        return;
    }
    fprintf(out, "started=1\n");
    if (seconds > 0) {
        fprintf(out, "frames=%ld\nseconds=%.3f\nfps=%.2f\n", frames, seconds, (double)frames / seconds);
    }
    fclose(out);
}

#endif
