# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 gfxtune contributors

"""System tests: gfxtune as installed by its package, with test games started
the way Steam starts them, on a virtual screen with software rendering.

Run by ci/system-tests.sh, in a container prepared by ci/provision.sh with
the gfxtune package installed. Never run against a real Steam installation:
every file the tests use is made up."""

from __future__ import annotations

import os
import shutil
import subprocess
import time
from collections.abc import Iterator
from pathlib import Path

import pytest

from lab import Games, Home, Steamlike, find_proton

PROGRAMS = Path(__file__).parent / "programs"


@pytest.fixture(scope="session")
def screen(tmp_path_factory):
    """A virtual X screen, and a runtime folder like a desktop session's."""
    runtime = tmp_path_factory.mktemp("runtime")
    runtime.chmod(0o700)
    number = 90 + os.getpid() % 100
    server = subprocess.Popen(["Xvfb", f":{number}", "-screen", "0", "1280x720x24", "-nolisten", "tcp"],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    socket = Path(f"/tmp/.X11-unix/X{number}")
    deadline = time.monotonic() + 10
    while not socket.exists():
        if time.monotonic() > deadline or server.poll() is not None:
            raise RuntimeError("Xvfb did not start")
        time.sleep(0.1)
    yield {"DISPLAY": f":{number}", "XDG_RUNTIME_DIR": str(runtime)}
    server.terminate()
    server.wait()


@pytest.fixture(scope="session")
def games(tmp_path_factory) -> Games:
    """The test games, built for this run. The Windows ones need MinGW."""
    out = tmp_path_factory.mktemp("games")
    flags = ["-O2", "-Wall", "-Wextra", "-Werror"]
    subprocess.run(["cc", "-std=c11", *flags, "-o", out / "vulkan_game", PROGRAMS / "vulkan_game.c", "-lvulkan", "-lxcb"],
                   check=True)
    windows = {}
    mingw = shutil.which("x86_64-w64-mingw32-g++")
    for name, libraries in (("d3d11", ["-ld3d11", "-ldxgi"]), ("d3d12", ["-ld3d12", "-ldxgi"])):
        if mingw:
            exe = out / f"{name}_game.exe"
            # A window program: as a console one, Wine would open a console
            # window over the game's.
            subprocess.run([mingw, "-std=c++17", *flags, "-static", "-mwindows", "-o", exe, PROGRAMS / f"{name}_game.cpp", *libraries],
                           check=True)
            windows[name] = exe
    return Games(out / "vulkan_game", windows.get("d3d11"), windows.get("d3d12"))


@pytest.fixture(scope="session")
def compat_data(tmp_path_factory) -> Path:
    """One Proton prefix for every Windows game: Wine takes a while to make
    one, and the games share nothing through it."""
    return tmp_path_factory.mktemp("compatdata")


@pytest.fixture
def home(tmp_path) -> Home:
    path = tmp_path / "home"
    (path / ".config").mkdir(parents=True)
    return Home(path)


@pytest.fixture
def steam(home, tmp_path, screen, compat_data) -> Iterator[Steamlike]:
    environment = {
        "PATH": os.environ.get("PATH", "/usr/bin"),
        "LANG": "C.UTF-8",
        **screen,
    }
    work = tmp_path / "work"
    work.mkdir()
    steamlike = Steamlike(home, work, find_proton(), compat_data, environment)
    yield steamlike
    steamlike.stop_all()


@pytest.fixture
def needs_proton(games):
    if not find_proton():
        pytest.skip("Proton is not installed")
    if not games.d3d11 or not games.d3d12:
        pytest.skip("MinGW is not installed to build the Windows test games")
