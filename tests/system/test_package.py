# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 gfxtune contributors

"""The installed package: its files, its version, a layer that keeps to
itself inside games, and the removal hook that takes gfxtune-run out of
Steam first."""

from __future__ import annotations

import json
import os
import pwd
import re
import shutil
import subprocess
from pathlib import Path
from xml.etree import ElementTree

import pytest

from lab import FakeSteam, SteamGame, eventually

LAYER = Path("/usr/lib/gfxtune/libVkLayer_gfxtune.so")
MANIFEST = Path("/usr/share/vulkan/implicit_layer.d/gfxtune_filters.json")
METAINFO = Path("/usr/share/metainfo/io.github.gfxtune.gfxtune.metainfo.xml")
FILES = {
    "/usr/bin/gfxtune",
    "/usr/bin/gfxtune-run",
    str(LAYER),
    "/usr/share/applications/gfxtune.desktop",
    str(METAINFO),
    "/usr/share/icons/hicolor/scalable/apps/gfxtune.svg",
    "/usr/share/icons/hicolor/16x16/apps/gfxtune.svg",
    "/usr/share/gfxtune/remove-launch-lines",
    "/usr/share/libalpm/hooks/gfxtune.hook",
    "/usr/share/licenses/gfxtune/NOTICE.md",
    "/usr/share/man/man1/gfxtune.1.gz",
    "/usr/share/man/man1/gfxtune-run.1.gz",
    str(MANIFEST),
}


def run(*command: str, **options) -> subprocess.CompletedProcess:
    return subprocess.run(command, capture_output=True, text=True, **options)


def installed_version() -> str:
    # "gfxtune 1.2.3-1": the version without the package release.
    return run("pacman", "-Q", "gfxtune").stdout.split()[1].rsplit("-", 1)[0]


def test_the_package_holds_its_files_and_nothing_else():
    listed = run("pacman", "-Qlq", "gfxtune", check=True).stdout.split()
    assert {path for path in listed if not path.endswith("/")} == FILES


def test_the_programs_say_the_packaged_version():
    out = run("gfxtune", "--version", env={**os.environ, "QT_QPA_PLATFORM": "offscreen"}, check=True).stdout
    assert out.strip() == f"gfxtune {installed_version()}"


def test_the_layer_shows_only_its_entry_point():
    exported = run("nm", "-D", "--defined-only", str(LAYER), check=True).stdout
    names = {line.split()[-1] for line in exported.splitlines() if line.strip()}
    assert names == {"vkNegotiateLoaderLayerInterfaceVersion"}


def test_the_layer_brings_no_libraries_a_game_could_clash_with():
    # Its C++ library is built in, so a game with its own copy is unaffected.
    dynamic = run("readelf", "-d", str(LAYER), check=True).stdout
    needed = set(re.findall(r"\(NEEDED\)\s+Shared library: \[(.+?)\]", dynamic))
    assert needed <= {"libc.so.6", "libm.so.6", "libgcc_s.so.1", "ld-linux-x86-64.so.2"}, needed


def test_the_layer_loads_only_when_asked():
    layer = json.loads(MANIFEST.read_text())["layer"]
    assert Path(layer["library_path"]) == LAYER
    assert layer["enable_environment"] == {"GFXTUNE_FILTERS": "1"}
    assert layer["disable_environment"] == {"DISABLE_GFXTUNE_FILTERS": "1"}


def test_the_menu_entry_follows_the_desktop_entry_spec():
    if not shutil.which("desktop-file-validate"):
        pytest.skip("desktop-file-validate is not installed")
    result = run("desktop-file-validate", "/usr/share/applications/gfxtune.desktop")
    assert result.returncode == 0 and not result.stdout.strip(), result.stdout + result.stderr


def test_software_centres_find_the_app_its_menu_entry_and_icon(tmp_path):
    # validate-tree checks the entry together with the menu entry and icon it
    # names, as a software centre reads them; only gfxtune's files go in.
    if not shutil.which("appstreamcli"):
        pytest.skip("appstreamcli is not installed")
    for path in FILES:
        if any(part in path for part in ("/metainfo/", "/applications/", "/icons/")):
            target = tmp_path / path.lstrip("/")
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy(path, target)
    result = run("appstreamcli", "validate-tree", "--no-net", "--pedantic", str(tmp_path))
    assert result.returncode == 0, result.stdout + result.stderr


def test_the_software_centre_entry_lists_this_release_first():
    releases = [release.get("version") for release in ElementTree.parse(METAINFO).getroot().iter("release")]
    assert releases and releases[0] == installed_version(), releases


@pytest.mark.parametrize("size", [16, 22, 48, 256])
def test_the_icon_renders_from_the_icon_theme(size, tmp_path):
    # The size a desktop asks for picks the file, as the icon theme spec says.
    if not shutil.which("rsvg-convert"):
        pytest.skip("rsvg-convert is not installed")
    source = "/usr/share/icons/hicolor/16x16/apps/gfxtune.svg" if size == 16 else "/usr/share/icons/hicolor/scalable/apps/gfxtune.svg"
    png = tmp_path / "icon.png"
    run("rsvg-convert", "-w", str(size), "-h", str(size), source, "-o", str(png), check=True)
    from PIL import Image
    image = Image.open(png).convert("RGBA")
    assert image.size == (size, size)
    # Something is drawn, and the gauge's blue is in it.
    pixels = list(zip(*[iter(image.tobytes())] * 4))
    assert sum(1 for p in pixels if p[3] > 0) > size * size // 2
    assert any(b > 180 and b > r + 60 for r, g, b, a in pixels if a > 200)


# ---- Removal ----------------------------------------------------------


@pytest.fixture
def lab_user_home():
    """The test user's real home, where the removal hook looks for Steam.
    Only in the test container, never where a Steam folder already is."""
    package = os.environ.get("GFXTUNE_PACKAGE")
    if not package:
        pytest.skip("run by ci/system-tests.sh, which says which package to put back")
    if run("sudo", "-n", "pacman", "-V").returncode != 0:
        pytest.skip("needs to run pacman through sudo without a password")
    home = Path(pwd.getpwuid(os.getuid()).pw_dir)
    steam = [home / ".steam", home / ".local" / "share" / "Steam"]
    if any(path.exists() or path.is_symlink() for path in steam):
        pytest.skip(f"{home} already has a Steam folder; this test only runs in the test container")
    yield home
    for path in steam:
        if path.is_symlink():
            path.unlink()
        elif path.exists():
            shutil.rmtree(path)
    if run("pacman", "-Q", "gfxtune").returncode != 0:
        run("sudo", "-n", "pacman", "-U", "--noconfirm", package, check=True)


def test_removal_takes_gfxtune_run_out_of_steam_first(lab_user_home, games, tmp_path):
    steam = FakeSteam(lab_user_home, [
        SteamGame(4401, "Removal Test One", games.vulkan, launch_options="gfxtune-run %command%"),
        SteamGame(4402, "Removal Test Two", games.vulkan, launch_options="gamemoderun gfxtune-run %command% -dx12"),
    ])
    # A process named "steam", as the real client's is.
    fake_steam = tmp_path / "steam"
    shutil.copy("/usr/bin/sleep", fake_steam)
    running = subprocess.Popen([str(fake_steam), "300"])
    try:
        refused = run("sudo", "-n", "pacman", "-R", "--noconfirm", "gfxtune")
        assert refused.returncode != 0
        assert "Close Steam" in refused.stdout + refused.stderr
        assert run("pacman", "-Q", "gfxtune").returncode == 0
        assert steam.launch_options()[4401] == "gfxtune-run %command%"
    finally:
        running.kill()
        running.wait()

    removed = run("sudo", "-n", "pacman", "-R", "--noconfirm", "gfxtune")
    assert removed.returncode == 0, removed.stdout + removed.stderr
    assert run("pacman", "-Q", "gfxtune").returncode != 0
    options = steam.launch_options()
    assert options.get(4401, "") == ""
    assert options[4402] == "gamemoderun %command% -dx12"
    # The layer went with the package: nothing is left for Vulkan to find.
    assert eventually(lambda: not MANIFEST.exists())
