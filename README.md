# gfxtune

Per-game graphics settings and live GPU readings for Linux, made with AMD Radeon cards in mind.

**Status: early development. Nothing here is usable yet.**

On Windows, the GPU driver panel lets you set sharpening, anti-aliasing, frame caps and similar options for each game. Linux has the same building blocks (Mesa, DXVK, Proton, gamescope), but they are environment variables and config files spread across several projects. gfxtune puts them in one window: pick a game, choose its settings, and they apply the next time the game starts.

## Planned for the first version

- Games from the native Steam client
- Per game: anisotropic filtering, vertical sync mode, frame cap, FSR 4 upgrade (with Proton builds that support it) and an optional gamescope session
- Sharpening and anti-aliasing through a Vulkan layer that ships with gfxtune
- A read-only panel with the card's temperatures, power, clocks and load

Options that do not apply to a game, for example FSR 4 under a Proton build without it, are shown as unavailable with the reason.

Not planned: recording, streaming, overlays or playtime tracking. Overclocking, undervolting and fan control are not part of the first version.

## Building

Requirements: CMake 3.24 or newer, a C++20 compiler, Qt 6.8 or newer (Quick, Quick Controls 2, Widgets) and KDE Kirigami.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).

AMD, Radeon and FSR are trademarks of Advanced Micro Devices, Inc. gfxtune is an independent project and is not affiliated with or endorsed by AMD.
