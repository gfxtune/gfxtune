# Contributing to gfxtune

Thank you for helping. Bug reports, tests on other cards and games, and code
are all welcome. This page says how the project is built and checked, so a
change can be tested the same way before it is sent.

## Reporting a bug

Open an issue with the bug report form. The output of `gfxtune --version`
and, for anything about a game, `gfxtune --list-games` help most; the
second lists your installed Steam games and their launch options, so remove
anything you would rather not share. For security problems, see
[SECURITY.md](SECURITY.md) instead.

## Building and testing

Everything is built and tested on CachyOS, in its official container image,
with the scripts in `ci/`. They are what CI runs, and they need nothing on
your system but Docker or Podman:

```bash
docker run -d --name gfxtune-dev -v "$PWD:/src:ro" cachyos/cachyos-v3:latest sleep infinity
docker exec gfxtune-dev /src/ci/provision.sh
docker exec gfxtune-dev /src/ci/build.sh debug
```

`ci/provision.sh` installs the build tools, Mesa's software Vulkan driver,
a virtual screen and the tools the system tests start games with. Then:

| Command | What it does |
| --- | --- |
| `ci/build.sh debug` | Debug build with warnings as errors, then every unit test |
| `ci/build.sh asan` | The same with AddressSanitizer and UndefinedBehaviorSanitizer |
| `ci/build.sh lint` | `qmllint` on every QML file, after a debug build |
| `ci/build.sh checks` | Licence information (`reuse lint`) and C++ formatting |
| `ci/build.sh format` | Formats the C++ code as `.clang-format` says |
| `ci/build.sh package` | The Arch package with CachyOS's own build flags, tests included |
| `ci/system-tests.sh` | Builds and installs the package, then the system tests |

The unit tests (`tests/tst_*.cpp`, Qt Test) cover each part on its own: the
Steam file readers, settings, launch plans, the filters drawn on a real
Vulkan device and compared pixel by pixel with the same arithmetic in C++,
and the window's pages. The system tests (`tests/system/`, pytest) install
the package and start small test games the way Steam does, as native Vulkan
programs and as DirectX 11 and 12 programs under Proton, on a virtual
screen; they check what reaches the screen, the frame rate, and the files
gfxtune and Steam end up with. Arguments after `ci/system-tests.sh` go to
pytest, for example `-k vulkan`.

Software rendering cannot do everything a graphics card does: gamescope does
not run on it, VKD3D-Proton's frame limiter needs a driver feature it lacks,
and MangoHud does not start without `/sys/class/drm`. Those tests are
skipped there, with the reason, and run on a real system.

## Changes

- Every change comes with tests, edge cases included. A bug fix starts with
  a test that fails without it.
- Keep a change to one subject, and say in its description what changed
  and why.
- C++ follows `.clang-format` (KDE's style); run `ci/build.sh format`.
  QML must pass `ci/build.sh lint` without new warnings.
- Comments say why the code is the way it is, not what it does.
<!-- REUSE-IgnoreStart -->
- New files start with SPDX lines, like the existing ones:
  `SPDX-License-Identifier: GPL-3.0-or-later` and
  `SPDX-FileCopyrightText: <year> gfxtune contributors`.
<!-- REUSE-IgnoreEnd -->
- User-visible changes get a line in [CHANGELOG.md](CHANGELOG.md).

## Test data

Tests use made-up data only: invented Steam libraries, accounts and games,
and recorded readings from graphics cards with nothing that identifies the
card or its owner (`tests/data/`). Never add real Steam files, account
names, serial numbers or paths from your own system.

## Licence

gfxtune is licensed under the GNU General Public License, version 3 or later.
By contributing, you agree that your contribution is licensed the same way.
Third-party code keeps its own licence; see [NOTICE.md](NOTICE.md).
