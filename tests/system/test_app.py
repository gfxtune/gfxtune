# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 gfxtune contributors

"""The gfxtune window on a virtual screen with a made-up Steam library: it
finds the games, puts gfxtune-run into the launch options of those it has
something to do in, holds back where anti-cheat asks for it, waits while
Steam is open, and its pages load without QML warnings."""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import time

import pytest

from lab import FakeSteam, SteamGame, eventually

NATIVE, WINDOWS, ANTI_CHEAT, WRAPPED = 4501, 4502, 4503, 4504

# Lines from QML itself (file:line: message) and binding loops.
QML_WARNING = re.compile(r"\.qml:\d+|[Bb]inding loop")


@pytest.fixture
def library(home, games) -> FakeSteam:
    return FakeSteam(home.path, [
        SteamGame(NATIVE, "Native Test Game", games.vulkan),
        SteamGame(WINDOWS, "Windows Test Game", games.d3d11 or games.vulkan, windows=True, launch_options="-windowed"),
        SteamGame(ANTI_CHEAT, "Guarded Test Game", games.vulkan, anti_cheat=True),
        SteamGame(WRAPPED, "Wrapped Test Game", games.vulkan, launch_options="gamemoderun %command%"),
    ])


@pytest.fixture
def desktop(home, screen):
    """What the window needs from a desktop session: a screen, a session
    bus, KDE's look. The settings and Steam are the made-up home's."""
    if not shutil.which("dbus-run-session"):
        pytest.skip("needs dbus-run-session")
    return {
        "PATH": os.environ.get("PATH", "/usr/bin"),
        "LANG": "C.UTF-8",
        "HOME": str(home.path),
        "XDG_CONFIG_HOME": str(home.path / ".config"),
        "QT_QPA_PLATFORM": "xcb",
        **screen,
    }


class Window:
    def __init__(self, environment: dict[str, str], log):
        self.log = log
        with log.open("w") as out:
            self.process = subprocess.Popen(["dbus-run-session", "--", "gfxtune"], env=environment, stdout=out,
                                            stderr=subprocess.STDOUT)

    def close(self) -> str:
        self.process.terminate()
        try:
            self.process.wait(20)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        return self.log.read_text(errors="replace")


@pytest.fixture
def window(desktop, tmp_path):
    opened: list[Window] = []

    def open_window() -> Window:
        opened.append(Window(desktop, tmp_path / f"window-{len(opened)}.log"))
        return opened[-1]

    yield open_window
    for each in opened:
        if each.process.poll() is None:
            each.close()


def test_list_games_reports_what_it_finds(library, desktop):
    out = subprocess.run(["gfxtune", "--list-games"], env={**desktop, "QT_QPA_PLATFORM": "offscreen"},
                         capture_output=True, text=True, check=True).stdout
    for name in ("Native Test Game", "Windows Test Game", "Guarded Test Game", "Wrapped Test Game"):
        assert name in out
    assert "Easy Anti-Cheat" in out
    assert "-windowed" in out


def test_the_window_adds_gfxtune_run_where_it_has_something_to_do(library, window):
    app = window()
    # gfxtune's defaults measure every game, so every game but the guarded one.
    expected = {
        NATIVE: "gfxtune-run %command%",
        WINDOWS: "gfxtune-run %command% -windowed",
        WRAPPED: "gamemoderun gfxtune-run %command%",
    }
    changed = eventually(lambda: library.launch_options() == expected, timeout=60, every=0.5)
    log = app.close()
    assert changed, library.launch_options()
    assert not QML_WARNING.findall(log), log


def test_the_window_waits_while_steam_is_open(library, window, tmp_path):
    fake_steam = tmp_path / "steam"
    shutil.copy("/usr/bin/sleep", fake_steam)
    steam = subprocess.Popen([str(fake_steam), "300"])
    try:
        app = window()
        time.sleep(15)
        # Steam rewrites its file when it exits: nothing may change meanwhile.
        assert library.launch_options() == {WINDOWS: "-windowed", WRAPPED: "gamemoderun %command%"}
    finally:
        steam.kill()
        steam.wait()
    assert eventually(lambda: library.launch_options().get(NATIVE) == "gfxtune-run %command%", timeout=30, every=0.5)
    app.close()


def test_measuring_off_takes_gfxtune_run_out_again(library, window, home):
    first = window()
    assert eventually(lambda: NATIVE in library.launch_options(), timeout=60, every=0.5)
    first.close()
    home.write_global("[performance]\nmeasure=false\n")
    second = window()
    # What was the game's own stays: Steam reads "%command% -windowed" as "-windowed".
    removed = eventually(lambda: library.launch_options() == {WINDOWS: "%command% -windowed", WRAPPED: "gamemoderun %command%"},
                         timeout=60, every=0.5)
    second.close()
    assert removed, library.launch_options()
