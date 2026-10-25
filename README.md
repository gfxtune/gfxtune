# gfxtune

Per-game graphics settings and live GPU readings for Linux, made with AMD Radeon cards in mind.

**Status: early development. Nothing here is usable yet.**

On Windows, the GPU driver panel lets you set sharpening, anti-aliasing, frame caps and similar options for each game. Linux has the same building blocks (Mesa, DXVK, Proton, gamescope), but they are environment variables and config files spread across several projects. gfxtune puts them in one window: pick a game, choose its settings, and they apply the next time the game starts.

## What works so far

- Games from the native Steam client, with how each one runs (native or which Proton) and which graphics libraries it loads
- Per game: anisotropic filtering, vertical sync mode, frame cap, FSR 4 on or off for games that ship FSR 3.1, and an optional gamescope session
- In-game filters per game: sharpening (AMD FidelityFX CAS) and colour (brightness, contrast, gamma, saturation, vibrance, colour temperature), adjustable while the game runs
- A read-only panel with the card's temperatures, power, clocks and load

Still to come: anti-aliasing (SMAA) and visual effects for the in-game filters.

Options that do not apply to a game, for example FSR 4 under a Proton build without it, are shown as unavailable with the reason. Some Proton builds (Valve's Proton 11 and Experimental, proton-cachyos) turn FSR 4 on by themselves and others (GE-Proton) only when asked; gfxtune reads which from the build's own files, says so on the game's page, and lets you turn it on or off per game.

Not planned: recording, streaming, overlays or playtime tracking. Overclocking, undervolting and fan control are not part of the first version.

## How settings reach a game

When a game gets settings of its own, gfxtune adds `gfxtune-run` to that game's Steam launch options, right before `%command%`. Anything already there stays where it was: `gamemoderun %command% -dx12` becomes `gamemoderun gfxtune-run %command% -dx12`. When the game starts, `gfxtune-run` sets the matching options (Mesa, DXVK, VKD3D-Proton and Proton variables), starts gamescope if it is switched on, and then hands over to the game. A game without gfxtune settings starts exactly as before.

Steam rewrites its settings file from memory when it exits, so gfxtune only edits it while Steam is closed. If Steam is open, the change waits until it closes, and the game's page offers the line to paste in by hand. Only the one launch option is changed, and the result is read back and checked before it is saved.

Settings you put in a game's launch options yourself always win: if they already set a variable gfxtune would set, or already start gamescope, gfxtune leaves that alone and says so on the game's page.

## In-game filters

The filters are drawn by a small Vulkan layer that ships with gfxtune. When a game starts with a filter switched on, `gfxtune-run` asks Vulkan to load the layer into that game, and only that game; nothing else ever loads it. Just before each frame is shown, the layer copies the picture and draws it back filtered. It reads the game's gfxtune settings file while the game runs, so moving a slider changes the picture within a second, and the Filters switch turns them off for comparing without losing the values.

Vulkan games are reached directly, and DirectX games under Proton through DXVK and VKD3D-Proton, which draw with Vulkan. Games that draw with OpenGL are not, and neither is a game while it shows HDR; the game's page says so, and after a launch it shows whether the filters were drawn.

The layer runs inside the game, as MangoHud or vkBasalt do. No bans are known for this kind of layer, but nobody can promise there will never be one. In a game that ships anti-cheat (gfxtune looks for Easy Anti-Cheat, BattlEye and others in its folder, and for Valve Anti-Cheat in Steam's data) the filters stay locked until you accept that risk for that game. If an update adds anti-cheat to a game that already had filters, gfxtune switches them off and keeps the values until you decide. Setting `DISABLE_GFXTUNE_FILTERS=1` in a game's launch options keeps the layer out of it whatever its settings say.

## Removing gfxtune-run from Steam

To take `gfxtune-run` out of every game, for example before removing gfxtune, close Steam and run:

```
gfxtune --remove-launch-lines
```

## The card panel

The panel on the right shows the card's temperatures (edge, hotspot, memory), power and power limit, load, video memory, clocks, fan speed and voltage, with graphs of temperature and power over the last minute. It reads the amdgpu driver's files in `/sys` once a second, only while gfxtune's window is visible, and leaves a card that has gone to sleep alone rather than waking it to read it. If there are several AMD cards, the one with the most video memory is shown first.

## Where gfxtune keeps its settings

Per-game settings are plain text files in `~/.config/gfxtune/games/`, one per game, written only for games you changed. What `gfxtune-run` applied at a game's last start is kept next to them, in `last-launch/`. gfxtune reads Steam's files where they are and never copies them. "Remove All gfxtune Settings" in the menu deletes the `gfxtune` folder and nothing else, and takes `gfxtune-run` out of the games' launch options once Steam is closed.

## Installing on Arch Linux and CachyOS

```
git clone <this repository>
cd gfxtune/packaging/arch
makepkg -si
```

`makepkg` builds gfxtune, runs its tests (add `--nocheck` to skip them) and installs it with pacman, which pulls in the Qt and KDE libraries it needs. gfxtune then appears in the application menu as a settings program.

To remove it, close Steam and run `sudo pacman -R gfxtune`. Before anything is removed, gfxtune takes `gfxtune-run` out of every user's Steam launch options; if Steam is still open, the removal stops and asks you to close it first, so no game is left pointing at a program that is gone. gfxtune's own settings stay in `~/.config/gfxtune` until you delete that folder or use "Remove All gfxtune Settings" first.

## Building

Requirements: CMake 3.24 or newer, a C++20 compiler, Qt 6.8 or newer (Quick, Quick Controls 2, Widgets), KDE Kirigami and KDE's KQuickCharts, which draws the graphs and comes with every Plasma desktop. The filter layer needs the Vulkan headers and `glslangValidator` to build, and the Vulkan loader for its tests. The card names come from libdrm's `amdgpu.ids`.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

The tests build made-up Steam folders and programs; they never read a real Steam installation. The filter tests draw on the first Vulkan device they find (Mesa's software renderer works too) and compare every pixel with the same arithmetic done in C++; with Khronos' validation layer installed, it checks every Vulkan call as well.

`gfxtune --list-games` prints the Steam games gfxtune finds and what it reads about each one (how it runs, which graphics libraries it loads, FSR 3.1, what its Proton does with FSR 4, anti-cheat, whether Steam has saved its cover, current launch options). It only reads, and is meant for checking a system or attaching to a bug report.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE). The sharpening filter follows AMD FidelityFX CAS, under the MIT licence; see [NOTICE.md](NOTICE.md).

AMD, Radeon and FSR are trademarks of Advanced Micro Devices, Inc. gfxtune is an independent project and is not affiliated with or endorsed by AMD.
