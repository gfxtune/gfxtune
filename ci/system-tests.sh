#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 gfxtune contributors
#
# The system tests: builds the gfxtune package, installs it, and starts test
# games through it the way Steam does, on a virtual screen with software
# rendering (tests/system). Runs as root in a container prepared by
# ci/provision.sh; the tests themselves run as its "builder" user.
#
#   ci/system-tests.sh [PYTEST OPTIONS...]    e.g. -k vulkan
set -euo pipefail

source_dir=$(cd "$(dirname "$0")/.." && pwd)
if [[ $(id -u) != 0 ]] || ! id builder >/dev/null 2>&1; then
    echo "Run as root in the container made by ci/provision.sh." >&2
    exit 2
fi

"$source_dir/ci/build.sh" package
version=$(sed -n 's/^pkgver=//p' "$source_dir/packaging/arch/PKGBUILD")
release=$(sed -n 's/^pkgrel=//p' "$source_dir/packaging/arch/PKGBUILD")
package=/home/builder/gfxtune/packaging/arch/gfxtune-$version-$release-x86_64.pkg.tar.zst
pacman -U --noconfirm "$package"

# A copy the test user owns, so nothing is written into the source tree.
tests=/home/builder/system-tests
rm -rf "$tests"
cp -r "$source_dir/tests/system" "$tests"
chown -R builder: "$tests"
cd "$tests"
exec runuser -u builder -- env GFXTUNE_PACKAGE="$package" PYTHONDONTWRITEBYTECODE=1 python -m pytest "$@"
