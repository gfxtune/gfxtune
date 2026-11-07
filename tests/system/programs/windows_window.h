// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

// The Windows test games' window and clock. Built with MinGW; runs under
// Proton.

#ifndef GFXTUNE_TEST_WINDOWS_WINDOW_H
#define GFXTUNE_TEST_WINDOWS_WINDOW_H

#include <windows.h>

#include "pattern.h"

static HWND create_game_window(const GameOptions *options)
{
    WNDCLASSA type = {};
    type.lpfnWndProc = DefWindowProcA;
    type.hInstance = GetModuleHandleA(nullptr);
    type.lpszClassName = "gfxtune_test_game";
    RegisterClassA(&type);
    // The client area is the requested size: the screenshots look at it.
    RECT area = {0, 0, LONG(options->width), LONG(options->height)};
    AdjustWindowRect(&area, WS_OVERLAPPEDWINDOW, FALSE);
    return CreateWindowA(type.lpszClassName, options->title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, area.right - area.left,
                         area.bottom - area.top, nullptr, nullptr, type.hInstance, nullptr);
}

static void pump_messages()
{
    MSG message;
    while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
}

static double seconds_now()
{
    static LARGE_INTEGER frequency;
    if (frequency.QuadPart == 0) {
        QueryPerformanceFrequency(&frequency);
    }
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return double(counter.QuadPart) / double(frequency.QuadPart);
}

#endif
