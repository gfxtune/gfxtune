# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 gfxtune contributors

"""GameMode and MangoHud, started by gfxtune-run around the game, and left
alone when the game's Steam launch options already start them."""

from __future__ import annotations

import shutil
from pathlib import Path

import pytest

from lab import eventually, shows_grey

GAME = 4300  # made-up app id


def needs(program: str) -> None:
    if not shutil.which(program):
        pytest.skip(f"{program} is not installed")


def test_gamemode_starts_around_the_game(steam, games, home):
    needs("gamemoderun")
    home.write_global("[launch]\ngamemode=true\n")
    game = steam.start(GAME, games.vulkan, "--seconds", "4")
    game.wait_started()
    # gamemoderun asks for GameMode from inside the game through its library.
    assert "libgamemodeauto" in game.mapped_libraries()
    game.finish()
    assert "GameMode" in home.last_launch(GAME)["applied"]


def test_mangohud_and_the_filters_together(steam, games, home):
    needs("mangohud")
    if not Path("/sys/class/drm").is_dir():
        pytest.skip("MangoHud stops at start where /sys/class/drm is missing, as in containers without a GPU")
    home.write_global("[launch]\nmangohud=true\n[filters]\nsaturation=-100\n")
    game = steam.start(GAME, games.vulkan, "--seconds", "6", "--colour", "220,40,40", "--size", "640x480")
    game.wait_started()
    libraries = game.mapped_libraries()
    assert "libMangoHud" in libraries
    assert "libVkLayer_gfxtune.so" in libraries
    # Both layers draw: the game's picture grey, and MangoHud's overlay in it.
    assert eventually(lambda: shows_grey(game.screenshot()))
    game.finish()
    assert "MangoHud" in home.last_launch(GAME)["applied"]


def test_launch_options_that_set_mangohud_win(steam, games, home):
    needs("mangohud")
    home.write_global("[launch]\nmangohud=true\n")
    # "MANGOHUD=0 %command%" in Steam: MangoHud kept out of this one game.
    game = steam.start(GAME, games.vulkan, "--seconds", "4", env={"MANGOHUD": "0"})
    game.wait_started()
    assert "libMangoHud" not in game.mapped_libraries()
    game.finish()
    launch = home.last_launch(GAME)
    assert "MangoHud" in launch["kept_from_steam"]
    assert "MangoHud" not in launch.get("applied", "")


def test_a_missing_program_still_starts_the_game(steam, games, home):
    # GameMode switched on where it is not installed: the game starts anyway.
    home.write_global("[launch]\ngamemode=true\n")
    steam.environment["PATH"] = "/usr/local/sbin/none"
    game = steam.start(GAME, games.vulkan, "--seconds", "3")
    assert "fps" in game.finish()
    assert "GameMode: not installed" in home.last_launch(GAME)["not_applied"]
