#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 gfxtune contributors
#
# Prepares a CachyOS container (cachyos/cachyos-v3) to build and test
# gfxtune: the build dependencies, a software Vulkan and OpenGL driver for
# the tests, a virtual screen, the tools the system tests start games with,
# and the code checkers. Run as root inside the container.
#
#   ci/provision.sh [build] [tests] [games] [checks]    the default is all four
set -euo pipefail

build=(
    base-devel cmake ninja git
    qt6-base qt6-declarative kirigami kquickcharts qqc2-desktop-style ki18n
    libdrm glslang vulkan-headers vulkan-icd-loader
)
tests=(
    # Software rendering: lavapipe (Vulkan) and llvmpipe (OpenGL), checked
    # by Khronos' validation layer.
    vulkan-swrast mesa vulkan-validation-layers vulkan-tools mesa-utils
    # A virtual screen with KDE's look, for the window and screenshots.
    xorg-server-xvfb xdotool imagemagick dbus breeze breeze-icons plasma-integration noto-fonts
    gdb python python-pytest python-pillow
    # Checks of the installed menu entry and icon.
    desktop-file-utils librsvg
)
games=(
    # What the system tests run games through, as on a gaming PC.
    umu-launcher proton-cachyos gamescope mangohud gamemode
    # Builds the Windows test programs that run under Proton.
    mingw-w64-gcc
)
checks=(clang reuse groff gettext)

# pacman runs its install hooks in a sandbox that cuts them off from the
# network, which a container does not allow; they need no network anyway.
if ! grep -q '^DisableSandboxNetwork' /etc/pacman.conf; then
    sed -i 's/^\[options\]$/[options]\nDisableSandboxNetwork/' /etc/pacman.conf
fi

groups=("$@")
if ((${#groups[@]} == 0)); then
    groups=(build tests games checks)
fi
packages=()
for group in "${groups[@]}"; do
    case $group in
    build) packages+=("${build[@]}") ;;
    tests) packages+=("${tests[@]}") ;;
    games) packages+=("${games[@]}") ;;
    checks) packages+=("${checks[@]}") ;;
    *)
        echo "unknown group: $group (build, tests, games or checks)" >&2
        exit 2
        ;;
    esac
done
pacman -Syu --noconfirm --needed "${packages[@]}"

# The translation tests need a locale other than C: gettext does not
# translate in the C locale.
if [[ " ${groups[*]} " == *" tests "* ]] && ! locale -a | grep -qi '^en_US\.utf-\?8$'; then
    sed -i 's/^#en_US.UTF-8 UTF-8/en_US.UTF-8 UTF-8/' /etc/locale.gen
    locale-gen
fi

# makepkg refuses to run as root: an ordinary user builds the package.
if ! id builder >/dev/null 2>&1; then
    useradd --create-home builder
    echo 'builder ALL=(ALL) NOPASSWD: /usr/bin/pacman' > /etc/sudoers.d/builder
    chmod 0440 /etc/sudoers.d/builder
fi
