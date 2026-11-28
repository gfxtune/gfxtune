# Changelog

All notable changes to gfxtune. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/); before 1.0, a minor version may
change how settings are stored.

## Unreleased (0.6.0)

### Added

- Frame generation through lsfg-vk, for all games or per game: 2×, 3× or
  4×, with lsfg-vk's flow scale and performance mode, which change in a
  running game within a second or two. lsfg-vk and Lossless Scaling (on
  its lsfg-vk branch) are installed separately; a game's page says which
  is missing. gfxtune writes lsfg-vk's settings to a file of its own and
  leaves the user's lsfg-vk settings alone.
- `gfxtune --list-games` says whether lsfg-vk is installed, and which
  branch and file of Lossless Scaling it found.

### Changed

- gfxtune's Vulkan layer is registered in `/etc/vulkan/implicit_layer.d`
  instead of `/usr/share/vulkan/implicit_layer.d`. Vulkan reads `/etc`
  first, so the layer sits closest to the game and caps and measures the
  frames the game draws, before lsfg-vk adds generated ones.
- Lossless Scaling is never listed as a game.

## 0.5.0 - 2026-11-19

The first release: settings for all Steam games and for each game,
in-game filters and performance measuring through gfxtune's own Vulkan
layer, and a live panel with the graphics card's readings, for CachyOS.

### Added

- An AppStream file, so software centres such as Discover show gfxtune
  with its description, screenshots and releases.

## 0.4.4 - 2026-11-18

### Changed

- The project's home is now the gfxtune organisation on GitHub:
  <https://github.com/gfxtune/gfxtune>.

### Fixed

- In the smallest window, the settings page was wider than the space for
  it: notes and the right end of some settings were cut off. The button
  that puts a setting back to its global value now goes under the setting
  when the two do not fit side by side, and the lines under a game's name
  wrap.
- The launch line to copy, when longer than its box, showed its end; it
  now shows its start.

## 0.4.3 - 2026-11-11

### Fixed

- Choosing another game or All Games kept the page scrolled where the last
  one was; each now opens at the top.

- A frame cap for a native game that neither gfxtune's layer nor gamescope
  reaches (a game with anti-cheat whose risk is not accepted, without
  gamescope) was listed under the last launch as applied, although nothing
  held it. It is now listed as not applied, with the reason.

### Changed

- Updating Steam's launch options for many games at once is much faster:
  for 1,000 games, 72 ms instead of 7.6 s, so the first start with a large
  library no longer freezes the window. The file is still checked before
  it is written, and only the launch options change.
- gfxtune no longer reads Steam's settings file again when neither the file
  nor gfxtune's settings changed.

### Added

- An icon of its own: a performance gauge, drawn to KDE's icon guidelines,
  with a version redrawn for 16 px.
- A native game whose files do not say what it draws with is marked as
  drawing with OpenGL once it has run for a minute with gfxtune's layer in
  it without showing a Vulkan picture. Its filters and measuring then show
  as unavailable, and it gets no launch option just for measuring. "Check
  Again" on its page undoes this.
- Translations: every text of the window, `gfxtune-run` and the command
  line can be translated (KDE's i18n); `po/gfxtune.pot` is the template.
- Problems that do not show in the window (a Steam file that could not be
  read, a settings file that could not be written) are logged; see
  `man gfxtune`.
- Manual pages for `gfxtune` and `gfxtune-run`.
- System tests: the package as installed, with test games started the way
  Steam starts them, Vulkan games and DirectX 11 and 12 games under Proton,
  on a virtual screen with software rendering. They check what reaches the
  screen (filters, anti-aliasing, live changes), the frame cap, measuring,
  the anti-cheat lock, GameMode and MangoHud, the window's launch options
  and the removal hook.
- Continuous integration on CachyOS's official container image, with the
  scripts in `ci/` that contributors run the same way.
- Licence information for every file ([REUSE](https://reuse.software)),
  a code format (`.clang-format`), static checks (`.clang-tidy`), and
  contributor documents.

## 0.4.2 and earlier

Development versions, not released: Steam library, per-game and global
settings, the in-game filter layer, performance measuring and the card
panel.
