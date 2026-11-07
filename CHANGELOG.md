# Changelog

All notable changes to gfxtune. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/); before 1.0, a minor version may
change how settings are stored.

## Unreleased (0.4.3)

### Fixed

- A frame cap for a native game that neither gfxtune's layer nor gamescope
  reaches (a game with anti-cheat whose risk is not accepted, without
  gamescope) was listed under the last launch as applied, although nothing
  held it. It is now listed as not applied, with the reason.

### Added

- System tests: the package as installed, with test games started the way
  Steam starts them, Vulkan games and DirectX 11 and 12 games under Proton,
  on a virtual screen with software rendering. They check what reaches the
  screen (filters, anti-aliasing, live changes), the frame cap, measuring,
  the anti-cheat lock, GameMode and MangoHud, the window's launch options
  and the removal hook.
- Continuous integration on CachyOS's official container image, with the
  scripts in `ci/` that contributors run the same way.
- Licence information for every file ([REUSE](https://reuse.software)),
  a code format (`.clang-format`), and contributor documents.

## 0.4.2 and earlier

Development versions, not released: Steam library, per-game and global
settings, the in-game filter layer, performance measuring and the card
panel.
