# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 gfxtune contributors

"""DirectX 11 and 12 games under Proton, started with Steam's command
line: the filters reach them through DXVK and VKD3D-Proton, and the frame
cap holds both through gfxtune's layer and, where the layer is locked,
through DXVK and VKD3D-Proton themselves."""

from __future__ import annotations

import time

import pytest

from lab import eventually, shows_colour, shows_grey, vulkan_device_extensions

GAME = 4200  # made-up app id
RED = "220,40,40"
CAP_30 = "[frames]\nframe_cap_enabled=true\nframe_cap=30\n"

# Wine needs time to make a prefix the first time, and lavapipe compiles
# DXVK's and VKD3D-Proton's shaders on the processor.
START = 300

pytestmark = pytest.mark.usefixtures("needs_proton")


@pytest.fixture(params=["d3d11", "d3d12"])
def windows_game(request, games):
    return getattr(games, request.param)


def test_filters_reach_a_directx_game(steam, windows_game, home):
    home.write_global("[filters]\nsaturation=-100\n")
    game = steam.start(GAME, windows_game, "--seconds", "8", "--colour", RED, windows=True)
    game.wait_started(START)
    assert game.environment()["GFXTUNE_FILTERS"] == "1"
    assert eventually(lambda: shows_grey(game.screenshot()), timeout=10)
    game.finish()
    assert home.filter_report(GAME)["state"] == "running"


def test_the_layer_caps_a_directx_game(steam, windows_game, home):
    home.write_global(CAP_30)
    game = steam.start(GAME, windows_game, "--seconds", "6", windows=True)
    game.wait_started(START)
    env = game.environment()
    # One limiter only: DXVK and VKD3D-Proton are not asked to cap as well.
    assert "maxFrameRate" not in env.get("DXVK_CONFIG", "")
    assert "VKD3D_FRAME_RATE" not in env
    fps = float(game.finish()["fps"])
    assert 28.5 <= fps <= 30.5, fps


def test_dxvk_and_vkd3d_cap_where_the_layer_is_locked(steam, windows_game, home):
    home.write_global(CAP_30)
    home.write_game(GAME, "anti_cheat=Easy Anti-Cheat\n")
    game = steam.start(GAME, windows_game, "--seconds", "6", windows=True)
    game.wait_started(START)
    env = game.environment()
    assert "GFXTUNE_FILTERS" not in env
    assert "dxgi.maxFrameRate = 30" in env["DXVK_CONFIG"]
    assert env["VKD3D_FRAME_RATE"] == "30"
    if windows_game.name.startswith("d3d12") and "VK_KHR_present_wait" not in vulkan_device_extensions():
        # VKD3D-Proton paces frames only where it can wait for presents
        # (libs/vkd3d/swapchain.c); RADV can, Mesa's software driver cannot.
        game.finish()
        pytest.skip("VKD3D-Proton's frame limiter needs VK_KHR_present_wait, which this Vulkan driver lacks")
    fps = float(game.finish()["fps"])
    # DXVK and VKD3D-Proton sleep to their cap less exactly than the layer.
    assert 27 <= fps <= 31, fps
    assert "Frame rate cap 30" in home.last_launch(GAME)["applied"]


def test_a_game_runs_unchanged_without_settings(steam, windows_game, home):
    # Measuring only: the game keeps its own picture, and is measured.
    game = steam.start(GAME, windows_game, "--seconds", "6", "--colour", RED, windows=True)
    game.wait_started(START)
    time.sleep(1.5)
    assert shows_colour(game.screenshot())
    game.finish()
    assert list(home.performance(GAME).glob("session-*"))
