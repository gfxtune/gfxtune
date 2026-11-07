# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 gfxtune contributors

"""A native Vulkan game started through gfxtune-run: the in-game filters as
they reach the screen, live setting changes, the frame cap, measuring, and
the cases where the layer must stay out."""

from __future__ import annotations

import time

from lab import blended_pixels, eventually, shows_colour, shows_grey

GAME = 4100  # made-up app id
RED = (220, 40, 40)
LAYER = "libVkLayer_gfxtune.so"
GREY = "[filters]\nsaturation=-100\n"


def colour(rgb: tuple[int, int, int]) -> str:
    return ",".join(map(str, rgb))


def test_filters_reach_the_picture(steam, games, home):
    home.write_global(GREY)
    game = steam.start(GAME, games.vulkan, "--seconds", "6", "--colour", colour(RED))
    game.wait_started()
    assert LAYER in game.mapped_libraries()
    assert eventually(lambda: shows_grey(game.screenshot()))
    game.finish()
    assert home.filter_report(GAME)["state"] == "running"
    assert "In-game filters" in home.last_launch(GAME)["applied"]


def test_measuring_alone_leaves_the_picture_as_drawn(steam, games, home):
    # gfxtune's defaults measure every game: the layer loads, draws nothing.
    game = steam.start(GAME, games.vulkan, "--seconds", "6", "--colour", colour(RED))
    game.wait_started()
    time.sleep(1)
    assert LAYER in game.mapped_libraries()
    assert shows_colour(game.screenshot())
    game.finish()


def test_a_change_reaches_the_running_game(steam, games, home):
    game = steam.start(GAME, games.vulkan, "--seconds", "12", "--colour", colour(RED))
    game.wait_started()
    time.sleep(1)
    assert shows_colour(game.screenshot())
    # On the game's page, then on the All Games page, while it runs.
    home.write_game(GAME, GREY)
    assert eventually(lambda: shows_grey(game.screenshot()))
    home.write_game(GAME, "")
    assert eventually(lambda: shows_colour(game.screenshot()))
    home.write_global(GREY)
    assert eventually(lambda: shows_grey(game.screenshot()))
    game.finish()


def test_the_games_own_setting_wins_over_the_global_one(steam, games, home):
    home.write_global(GREY)
    home.write_game(GAME, "[filters]\nsaturation=0\n")
    game = steam.start(GAME, games.vulkan, "--seconds", "6", "--colour", colour(RED))
    game.wait_started()
    time.sleep(1.5)
    assert shows_colour(game.screenshot())
    game.finish()


def test_a_game_out_of_the_global_settings_is_left_alone(steam, games, home):
    home.write_global(GREY)
    home.write_game(GAME, "uses_global=false\n")
    game = steam.start(GAME, games.vulkan, "--seconds", "5", "--colour", colour(RED))
    game.wait_started()
    time.sleep(1)
    assert LAYER not in game.mapped_libraries()
    assert shows_colour(game.screenshot())
    game.finish()
    # Nothing to do, so gfxtune-run handed over at once and kept no record.
    assert home.last_launch(GAME) == {}


def test_anti_aliasing_smooths_hard_edges(steam, games, home):
    # Two colours meet along a staircase: without SMAA no pixel is between them.
    a = (230, 30, 30)
    b = tuple(255 - c for c in a)
    plain = steam.start(GAME, games.vulkan, "--seconds", "5", "--colour", colour(a), "--stairs")
    plain.wait_started()
    time.sleep(1)
    before = blended_pixels(plain.screenshot(), a, b)
    plain.finish()

    home.write_global("[filters]\nanti_aliasing=smaa\n")
    smooth = steam.start(GAME, games.vulkan, "--seconds", "5", "--colour", colour(a), "--stairs")
    smooth.wait_started()
    time.sleep(1.5)
    after = blended_pixels(smooth.screenshot(), a, b)
    smooth.finish()
    assert before == 0
    # The edge runs across the whole 320-pixel window.
    assert after > 150, after


def test_the_frame_cap_holds(steam, games, home):
    free = steam.start(GAME, games.vulkan, "--seconds", "4")
    uncapped = float(free.finish()["fps"])
    assert uncapped > 100, "the software renderer is too slow to show a cap"

    home.write_global("[frames]\nframe_cap_enabled=true\nframe_cap=30\n")
    capped = steam.start(GAME, games.vulkan, "--seconds", "6")
    fps = float(capped.finish()["fps"])
    assert 28.5 <= fps <= 30.5, fps
    assert "Frame rate cap 30" in home.last_launch(GAME)["applied"]


def test_measuring_records_the_session(steam, games, home):
    game = steam.start(GAME, games.vulkan, "--seconds", "7")
    game.wait_started()
    env = game.environment()
    assert env["GFXTUNE_PERF_DIR"] == str(home.performance(GAME))
    game.finish()
    sessions = list(home.performance(GAME).glob("session-*"))
    assert len(sessions) == 1
    text = sessions[0].read_text()
    assert "histogram=" in text and "start=" in text
    assert "fps=" in (home.performance(GAME) / "live").read_text()


def test_measuring_off_and_nothing_else_leaves_the_game_alone(steam, games, home):
    home.write_global("[performance]\nmeasure=false\n")
    game = steam.start(GAME, games.vulkan, "--seconds", "4")
    game.wait_started()
    assert LAYER not in game.mapped_libraries()
    assert "GFXTUNE_FILTERS" not in game.environment()
    game.finish()
    assert not home.performance(GAME).exists()


def test_anti_cheat_keeps_the_layer_out_until_the_risk_is_accepted(steam, games, home):
    # With only in-game settings, gfxtune-run would have nothing to do and
    # hand over at once, writing nothing; see test_vulkan's
    # test_a_game_out_of_the_global_settings_is_left_alone.
    # Anisotropic filtering is applied from outside the game: gfxtune-run
    # still has that to do, and says why the filters are not there.
    home.write_global(GREY + "[image]\nanisotropic_filtering=8\n")
    home.write_game(GAME, "anti_cheat=Easy Anti-Cheat\n")
    locked = steam.start(GAME, games.vulkan, "--seconds", "5", "--colour", colour(RED))
    locked.wait_started()
    time.sleep(1)
    assert LAYER not in locked.mapped_libraries()
    assert shows_colour(locked.screenshot())
    locked.finish()
    launch = home.last_launch(GAME)
    assert "locked until the anti-cheat risk is accepted" in launch["not_applied"]
    assert "Anisotropic filtering 8" in launch["applied"]

    home.write_game(GAME, "anti_cheat=Easy Anti-Cheat\n[filters]\nrisk_accepted=true\n")
    accepted = steam.start(GAME, games.vulkan, "--seconds", "5", "--colour", colour(RED))
    accepted.wait_started()
    assert eventually(lambda: shows_grey(accepted.screenshot()))
    accepted.finish()


def test_a_cap_nothing_can_hold_is_not_claimed(steam, games, home):
    # A native game with the layer locked and no gamescope: DXVK and
    # VKD3D-Proton are not there to cap it, and the last launch says so.
    home.write_global("[frames]\nframe_cap_enabled=true\nframe_cap=30\n")
    home.write_game(GAME, "anti_cheat=Easy Anti-Cheat\n")
    game = steam.start(GAME, games.vulkan, "--seconds", "4")
    assert float(game.finish()["fps"]) > 100
    launch = home.last_launch(GAME)
    assert "Frame rate cap" not in launch.get("applied", "")
    assert "Frame rate cap" in launch["not_applied"]


def test_disable_gfxtune_filters_keeps_the_layer_out(steam, games, home):
    home.write_global(GREY)
    game = steam.start(GAME, games.vulkan, "--seconds", "5", "--colour", colour(RED), env={"DISABLE_GFXTUNE_FILTERS": "1"})
    game.wait_started()
    time.sleep(1)
    assert LAYER not in game.mapped_libraries()
    assert shows_colour(game.screenshot())
    game.finish()


def test_games_without_gfxtune_run_never_load_the_layer(steam, games, home):
    # The layer is installed for everything but only loads when asked.
    home.write_global(GREY)
    game = steam.start(GAME, games.vulkan, "--seconds", "4", helper=False)
    game.wait_started()
    assert LAYER not in game.mapped_libraries()
    game.finish()
