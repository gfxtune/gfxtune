# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 gfxtune contributors

"""Frame generation through lsfg-vk, installed from the distribution as on a
gaming PC: what gfxtune hands it, where gfxtune's layer sits next to it,
and that a game still starts when Lossless Scaling's file is missing. The
lab has no Lossless Scaling, so lsfg-vk never generates frames here; with
a stand-in file in its place, it reads gfxtune's settings and then stops
the game, as it would with a broken file."""

from __future__ import annotations

import re
import shutil
import subprocess
from pathlib import Path

import pytest

GAME = 4300  # made-up app id
LSFG_VK = Path("/usr/share/vulkan/implicit_layer.d/VkLayer_LSFGVK_frame_generation.json")


@pytest.fixture(autouse=True)
def lsfg_vk():
    if not LSFG_VK.exists():
        pytest.skip("lsfg-vk is not installed")


def note_dll(home, path: Path) -> None:
    """What the window notes after a Steam scan found Lossless Scaling."""
    note = home.config / "lsfg-vk" / "lossless-scaling"
    note.parent.mkdir(parents=True, exist_ok=True)
    note.write_text(f"{path}\n")


def stand_in_dll(home, tmp_path) -> Path:
    dll = tmp_path / "Lossless Scaling" / "lsfg-vk.dll"
    dll.parent.mkdir(parents=True)
    dll.write_bytes(b"MZ not Lossless Scaling")
    note_dll(home, dll)
    return dll


def ended(game, timeout: float = 60) -> str:
    """The game's output once it ended, however it ended."""
    try:
        game.process.wait(timeout)
    except subprocess.TimeoutExpired:
        game.process.kill()
        raise
    return game.log.read_text(errors="replace")


def lsfg_lines(log: str) -> str:
    return "\n".join(line for line in log.splitlines() if line.startswith("(lsfg-vk)"))


def test_a_game_starts_without_frame_generation_when_lossless_scaling_is_missing(steam, games, home, tmp_path):
    home.write_global("[frame_generation]\nmultiplier=2\n")
    note_dll(home, tmp_path / "gone" / "lsfg-vk.dll")
    game = steam.start(GAME, games.vulkan, "--seconds", "3")
    game.wait_started()
    environment = game.environment()
    game.finish()
    assert not any(name.startswith("LSFGVK_") for name in environment), environment
    assert "Frame generation: Lossless Scaling's frame generation file was not found" in home.last_launch(GAME)["not_applied"]


def test_lsfg_vk_reads_gfxtunes_settings_for_the_game(steam, games, home, tmp_path):
    dll = stand_in_dll(home, tmp_path)
    home.write_global("[frame_generation]\nmultiplier=3\nflow_scale=60\nperformance_mode=true\n")
    game = steam.start(GAME, games.vulkan, "--seconds", "3", env={"VK_LOADER_DEBUG": "layer"})
    log = ended(game)
    lines = lsfg_lines(log)
    assert "Using profile with name 'gfxtune' (identified via environment)" in lines, log[-3000:]
    assert "Multiplier: 3" in lines and "Flow scale: 0.60" in lines and "Performance mode: true" in lines, lines
    assert "Frame generation 3× (lsfg-vk)" in home.last_launch(GAME)["applied"]

    # gfxtune's layer caps and measures the frames the game draws, before
    # lsfg-vk adds its own: the Vulkan loader reads /etc, where gfxtune's
    # manifest is, before /usr/share, where lsfg-vk's is.
    assert "vkCreateDevice layer callstack setup to:" in log, log[-3000:]
    stack = log.split("vkCreateDevice layer callstack setup to:", 1)[1]
    order = re.findall(r"VK_LAYER_(GFXTUNE_filters|LSFGVK_frame_generation)", stack)
    assert order[:2] == ["GFXTUNE_filters", "LSFGVK_frame_generation"], stack[:2000]

    # lsfg-vk's own check of the file gfxtune wrote.
    config = home.config / "lsfg-vk" / str(GAME) / "conf.toml"
    if shutil.which("lsfg-vk-cli"):
        result = subprocess.run(["lsfg-vk-cli", "validate", "-c", str(config), "-p"], capture_output=True, text=True)
        output = result.stdout + result.stderr  # it writes to both
        assert result.returncode == 0 and "The configuration file is valid." in output, output
        assert f"DLL override: {dll}" in output, output


def test_frame_generation_off_leaves_lsfg_vk_alone(steam, games, home, tmp_path):
    stand_in_dll(home, tmp_path)
    game = steam.start(GAME, games.vulkan, "--seconds", "3")
    game.wait_started()
    environment = game.environment()
    game.finish()
    assert not any(name.startswith("LSFGVK_") for name in environment), environment
    assert not (home.config / "lsfg-vk" / str(GAME)).exists()
    assert "identified via environment" not in lsfg_lines(game.log.read_text(errors="replace"))
