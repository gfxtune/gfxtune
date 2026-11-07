# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 gfxtune contributors

"""Helpers for the system tests: a home folder with gfxtune's settings, test
games started the way Steam starts them, screenshots, and a made-up Steam
library. Every name and file here is invented."""

from __future__ import annotations

import colorsys
import io
import os
import shutil
import signal
import struct
import subprocess
import time
from dataclasses import dataclass, field
from pathlib import Path

from PIL import Image

# Steam's Proton for CachyOS, as the proton-cachyos package installs it.
PROTON_TOOLS = Path("/usr/share/steam/compatibilitytools.d")


def find_proton() -> Path | None:
    for tool in sorted(PROTON_TOOLS.glob("proton-cachyos*")):
        if (tool / "proton").is_file():
            return tool / "proton"
    return None


def eventually(check, timeout: float = 5, every: float = 0.25):
    """Calls `check` until it returns something true, and returns that.
    For what follows a change with a delay, such as the layer reading a
    settings file again (once a second)."""
    deadline = time.monotonic() + timeout
    while True:
        result = check()
        if result or time.monotonic() > deadline:
            return result
        time.sleep(every)


def vulkan_device_extensions() -> set[str]:
    """The extensions of the first Vulkan device, as vulkaninfo lists them."""
    out = subprocess.run(["vulkaninfo"], capture_output=True, text=True).stdout
    return {word for word in out.split() if word.startswith("VK_") and word.isascii()}


# ---- gfxtune's files ----------------------------------------------------


def read_keys(path: Path) -> dict[str, str]:
    """key=value lines, sections ignored, as gfxtune's reports are written."""
    keys: dict[str, str] = {}
    if path.is_file():
        for line in path.read_text(errors="replace").splitlines():
            if "=" in line and not line.startswith(("#", "[")):
                key, value = line.split("=", 1)
                keys[key.strip()] = value.strip()
    return keys


@dataclass
class Home:
    """A user's home folder, as gfxtune and Steam see it."""

    path: Path

    @property
    def config(self) -> Path:
        return self.path / ".config" / "gfxtune"

    def write_global(self, text: str) -> None:
        """Settings for all games, as on the All Games page."""
        self.config.mkdir(parents=True, exist_ok=True)
        _write_atomically(self.config / "global.ini", text)

    def write_game(self, app_id: int, text: str) -> None:
        """A game's own file: [game] format=2 and the settings it changes."""
        path = self.config / "games" / f"{app_id}.ini"
        path.parent.mkdir(parents=True, exist_ok=True)
        _write_atomically(path, f"[game]\nformat=2\n{text}")

    def last_launch(self, app_id: int) -> dict[str, str]:
        return read_keys(self.config / "last-launch" / f"{app_id}.ini")

    def filter_report(self, app_id: int) -> dict[str, str]:
        return read_keys(self.config / "last-launch" / f"{app_id}.filters")

    def performance(self, app_id: int) -> Path:
        return self.config / "performance" / str(app_id)


def _write_atomically(path: Path, text: str) -> None:
    # The layer follows these files while a game runs: never let it see half.
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(text)
    temporary.replace(path)


# ---- Games ----------------------------------------------------------------


@dataclass
class Games:
    """The test games, built for this run (see programs/pattern.h)."""

    vulkan: Path
    d3d11: Path | None
    d3d12: Path | None


@dataclass
class Running:
    """A test game started through gfxtune-run."""

    process: subprocess.Popen
    result: Path
    title: str
    log: Path
    display: dict[str, str]  # the screen the game shows on

    def wait_started(self, timeout: float = 120) -> None:
        """Until the game has shown its first frame."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if read_keys(self.result).get("started") == "1":
                return
            if self.process.poll() is not None:
                raise AssertionError(f"the game ended before its first frame:\n{self.log.read_text(errors='replace')[-3000:]}")
            time.sleep(0.2)
        raise AssertionError(f"no first frame within {timeout} s:\n{self.log.read_text(errors='replace')[-3000:]}")

    def screenshot(self) -> Image.Image:
        """What the game's window shows on the screen right now."""
        window = _find_window(self.title, self.display)
        png = subprocess.run(["import", "-window", window, "png:-"], check=True, capture_output=True, env=self.display).stdout
        return Image.open(io.BytesIO(png)).convert("RGB")

    def finish(self, timeout: float = 120) -> dict[str, str]:
        """The game's report once it ended on its own."""
        try:
            self.process.wait(timeout)
        except subprocess.TimeoutExpired:
            self.process.kill()
            raise
        if self.process.returncode != 0:
            raise AssertionError(f"the game failed ({self.process.returncode}):\n{self.log.read_text(errors='replace')[-3000:]}")
        return read_keys(self.result)

    def mapped_libraries(self) -> str:
        """The libraries loaded into the game's process. gfxtune-run and the
        programs it starts replace themselves with the next, so the process is
        the one first started. Under Proton it is Proton's script, not the
        game."""
        return Path(f"/proc/{self.process.pid}/maps").read_text(errors="replace")

    def environment(self) -> dict[str, str]:
        """The variables the process was last started with: what gfxtune-run
        handed to the game (or to the wrapper it starts)."""
        raw = Path(f"/proc/{self.process.pid}/environ").read_bytes()
        return dict(item.split("=", 1) for item in raw.decode(errors="replace").split("\0") if "=" in item)


def _find_window(title: str, display: dict[str, str]) -> str:
    found = subprocess.run(["xdotool", "search", "--name", f"^{title}$"], capture_output=True, text=True, env=display).stdout.split()
    if not found:
        raise AssertionError(f"no window named {title!r}")
    # Wine may make a frame and a client window: the largest is the whole game.
    def area(window: str) -> int:
        shell = subprocess.run(["xdotool", "getwindowgeometry", "--shell", window], capture_output=True, text=True, env=display).stdout
        values = dict(line.split("=", 1) for line in shell.splitlines() if "=" in line)
        return int(values.get("WIDTH", 0)) * int(values.get("HEIGHT", 0))

    return max(found, key=area)


@dataclass
class Steamlike:
    """Starts games as Steam does: `gfxtune-run %command%`, with SteamAppId
    set, and for Windows games Proton's own command line."""

    home: Home
    work: Path
    proton: Path | None
    compat_data: Path
    environment: dict[str, str] = field(default_factory=dict)
    _count: int = 0
    _started: list[Running] = field(default_factory=list)
    _proton_used: bool = False

    def start(self, app_id: int, game: Path, *game_options: str, windows: bool = False, env: dict[str, str] | None = None,
              helper: bool = True) -> Running:
        """Starts a game as `gfxtune-run %command%` would, or with `helper`
        off as a game without gfxtune in its launch options."""
        self._count += 1
        # Steam starts a game in its folder under steamapps/common, which
        # Proton's protonfixes reads from PWD.
        folder = self.work / "steamapps" / "common" / f"game-{app_id}"
        folder.mkdir(parents=True, exist_ok=True)
        game = Path(shutil.copy2(game, folder / game.name))
        result = self.work / f"result-{app_id}-{self._count}.txt"
        result.unlink(missing_ok=True)
        log = self.work / f"game-{app_id}-{self._count}.log"
        title = f"gfxtune test {app_id}-{self._count}"
        options = ["--title", title, *game_options]
        if windows:
            if not self.proton:
                raise AssertionError("no Proton installed")
            # Steam's command for a Proton game: the tool's script, a verb, the game.
            command = [str(self.proton), "waitforexitandrun", str(game), *options, "--result", "Z:" + str(result).replace("/", "\\")]
        else:
            command = [str(game), *options, "--result", str(result)]
        environment = {
            **self.environment,
            "HOME": str(self.home.path),
            "XDG_CONFIG_HOME": str(self.home.path / ".config"),
            "SteamAppId": str(app_id),
            "STEAM_COMPAT_DATA_PATH": str(self.compat_data),
            "STEAM_COMPAT_CLIENT_INSTALL_PATH": str(self.work / "steam-client"),
            "PWD": str(folder),
            **(env or {}),
        }
        (self.work / "steam-client").mkdir(exist_ok=True)
        with log.open("w") as out:
            # Found once on the tests' own PATH, so a test can give the game another.
            helper_path = shutil.which("gfxtune-run") or "gfxtune-run"
            # A session of its own, so everything the game starts can be stopped with it.
            process = subprocess.Popen([helper_path, *command] if helper else command, env=environment, cwd=folder,
                                       stdout=out, stderr=subprocess.STDOUT, start_new_session=True)
        self._proton_used = self._proton_used or windows
        self._started.append(Running(process, result, title, log, self.environment))
        return self._started[-1]

    def stop_all(self) -> None:
        """Ends every game this started that still runs, as after a failed
        test, so none is left on the screen for the next."""
        for running in self._started:
            if running.process.poll() is None:
                try:
                    os.killpg(running.process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                running.process.wait()
        if self._proton_used and self.proton:
            # Wine's server outlives the game for a few seconds; end it now.
            wineserver = self.proton.parent / "files" / "bin" / "wineserver"
            subprocess.run([str(wineserver), "-k"], env={**self.environment, "WINEPREFIX": str(self.compat_data / "pfx")},
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            subprocess.run([str(wineserver), "-w"], env={**self.environment, "WINEPREFIX": str(self.compat_data / "pfx")},
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)


# ---- Pictures -------------------------------------------------------------


def centre(image: Image.Image, share: float = 0.5) -> Image.Image:
    """The middle of a picture, away from any window frame."""
    w, h = image.size
    dx, dy = int(w * (1 - share) / 2), int(h * (1 - share) / 2)
    return image.crop((dx, dy, w - dx, h - dy))


def pixels(image: Image.Image) -> list[tuple[int, int, int]]:
    data = image.convert("RGB").tobytes()
    return list(zip(data[0::3], data[1::3], data[2::3]))


def saturation(image: Image.Image) -> float:
    """Mean colour saturation, 0 for grey, 1 for pure colours."""
    each = pixels(image)
    return sum(colorsys.rgb_to_hsv(*(c / 255 for c in p))[1] for p in each) / len(each)


def brightness(image: Image.Image) -> float:
    """Mean of the brightest channel, 0 to 255."""
    each = pixels(image)
    return sum(max(p) for p in each) / len(each)


def shows_grey(image: Image.Image) -> bool:
    """The game's red turned grey, as saturation -100 does. Not merely a
    black window, which has no colour either."""
    middle = centre(image)
    return saturation(middle) < 0.05 and brightness(middle) > 30


def shows_colour(image: Image.Image) -> bool:
    """The game's red, as it drew it."""
    return saturation(centre(image)) > 0.7


def blended_pixels(image: Image.Image, a: tuple[int, int, int], b: tuple[int, int, int], tolerance: int = 24) -> int:
    """Pixels that are neither colour: what anti-aliasing adds along an edge
    between them."""

    def near(p, c):
        return all(abs(p[i] - c[i]) <= tolerance for i in range(3))

    return sum(1 for p in pixels(image) if not near(p, a) and not near(p, b))


# ---- A made-up Steam library ----------------------------------------------

ACCOUNT = 76561198000000001  # invented


def _text_vdf(node: dict, depth: int = 0) -> str:
    out = ""
    for key, value in node.items():
        pad = "\t" * depth
        if isinstance(value, dict):
            out += f'{pad}"{key}"\n{pad}{{\n{_text_vdf(value, depth + 1)}{pad}}}\n'
        else:
            out += f'{pad}"{key}"\t\t"{value}"\n'
    return out


def _binary_vdf(node: dict, keys: list[str]) -> bytes:
    out = b""
    for key, value in node.items():
        if key not in keys:
            keys.append(key)
        index = struct.pack("<i", keys.index(key))
        if isinstance(value, dict):
            out += b"\x00" + index + _binary_vdf(value, keys) + b"\x08"
        else:
            out += b"\x01" + index + str(value).encode() + b"\x00"
    return out


def _appinfo(apps: dict[int, dict]) -> bytes:
    """appinfo.vdf version 29: entries, then the table of their keys."""
    keys: list[str] = []
    body = b""
    for app_id, info in apps.items():
        payload = _binary_vdf({"appinfo": info}, keys) + b"\x08"
        body += struct.pack("<IIIIQ", app_id, 60 + len(payload), 2, 1700000000, 0) + b"\0" * 20
        body += struct.pack("<I", 1) + b"\0" * 20 + payload
    body += struct.pack("<I", 0)
    head = struct.pack("<II", 0x07564429, 1)
    table = struct.pack("<i", len(keys)) + b"".join(k.encode() + b"\0" for k in keys)
    return head + struct.pack("<q", len(head) + 8 + len(body)) + body + table


@dataclass
class SteamGame:
    app_id: int
    name: str
    program: Path  # copied in as the game's own program
    windows: bool = False
    anti_cheat: bool = False
    launch_options: str = ""


class FakeSteam:
    """A Steam folder with installed games, an account and its launch
    options, in a home folder. Steam itself is not there: gfxtune only
    reads its files."""

    def __init__(self, home: Path, games: list[SteamGame]):
        self.root = home / ".local" / "share" / "Steam"
        self.games = games
        account_folder = ACCOUNT & 0xFFFFFFFF
        self.local_config = self.root / "userdata" / str(account_folder) / "config" / "localconfig.vdf"
        steamapps = self.root / "steamapps"
        (steamapps / "common").mkdir(parents=True, exist_ok=True)
        (steamapps / "libraryfolders.vdf").write_text(_text_vdf({"libraryfolders": {"0": {"path": str(self.root)}}}))
        apps = {}
        for game in games:
            folder = game.name.replace(" ", "")
            install = steamapps / "common" / folder
            install.mkdir(parents=True, exist_ok=True)
            program = install / game.program.name
            program.write_bytes(game.program.read_bytes())
            program.chmod(0o755)
            if game.anti_cheat:
                marker = install / "EasyAntiCheat" / "EasyAntiCheat_x64.dll"
                marker.parent.mkdir(exist_ok=True)
                marker.write_bytes(b"made up")
            (steamapps / f"appmanifest_{game.app_id}.acf").write_text(_text_vdf({"AppState": {
                "appid": game.app_id, "name": game.name, "StateFlags": 4, "installdir": folder, "buildid": 1}}))
            os_list = "windows" if game.windows else "linux"
            apps[game.app_id] = {
                "common": {"name": game.name, "type": "Game", "oslist": os_list},
                "config": {"launch": {"0": {"executable": game.program.name, "config": {"oslist": os_list}}}},
            }
        (self.root / "appcache").mkdir(exist_ok=True)
        (self.root / "appcache" / "appinfo.vdf").write_bytes(_appinfo(apps))
        (self.root / "config").mkdir(exist_ok=True)
        (self.root / "config" / "loginusers.vdf").write_text(_text_vdf({"users": {str(ACCOUNT): {"AccountName": "player", "MostRecent": 1}}}))
        self.local_config.parent.mkdir(parents=True, exist_ok=True)
        options = {str(g.app_id): {"LaunchOptions": g.launch_options} for g in games if g.launch_options}
        self.local_config.write_text(_text_vdf({"UserLocalConfigStore": {"Software": {"Valve": {"Steam": {"apps": options}}}}}))
        (home / ".steam").mkdir(exist_ok=True)
        link = home / ".steam" / "steam"
        if not link.exists():
            link.symlink_to(self.root)

    def launch_options(self) -> dict[int, str]:
        """Each game's launch options as Steam's file holds them now; empty
        ones, which Steam treats as none, are left out."""
        text = self.local_config.read_text()
        found: dict[int, str] = {}
        for game in self.games:
            marker = f'"{game.app_id}"'
            start = text.find(marker)
            if start < 0:
                continue
            block = text[start:text.find("}", start)]
            for line in block.splitlines():
                parts = line.strip().split('"')
                if len(parts) >= 4 and parts[1] == "LaunchOptions" and parts[3]:
                    found[game.app_id] = parts[3]
        return found
