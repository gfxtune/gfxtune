#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 gfxtune contributors
#
# Builds and tests gfxtune from this source tree, the way CI does. Runs in
# a container prepared by ci/provision.sh.
#
#   ci/build.sh debug     Debug build, warnings as errors, every test
#   ci/build.sh asan      the same with AddressSanitizer and UBSan
#   ci/build.sh package   the Arch package with this system's makepkg
#                         settings (CachyOS's in its container), tests included
#   ci/build.sh lint      QML lint, after a debug build
#   ci/build.sh checks    licence information (REUSE), C++ formatting, manual pages
#   ci/build.sh format    formats the C++ code as .clang-format says
set -euo pipefail

source_dir=$(cd "$(dirname "$0")/.." && pwd)
export LANG=C.UTF-8 QT_QPA_PLATFORM=offscreen

# A compiler job on this code takes up to about a gigabyte: never more jobs
# than that allows, which matters in containers with little memory.
jobs() {
    local available_mb
    available_mb=$(awk '/^MemAvailable:/ { print int($2 / 1024) }' /proc/meminfo)
    local by_memory=$((available_mb / 1024))
    local cpus
    cpus=$(nproc)
    echo $((by_memory < 1 ? 1 : (by_memory < cpus ? by_memory : cpus)))
}
export CMAKE_BUILD_PARALLEL_LEVEL=${CMAKE_BUILD_PARALLEL_LEVEL:-$(jobs)}

# Our C and C++ files; third-party code keeps its own style.
cpp_files() {
    find "$source_dir/src" "$source_dir/tests" \( -name '*.cpp' -o -name '*.h' -o -name '*.c' \) \
        -not -path '*/third_party/*' -print0
}

configure_and_test() {
    local dir=$1
    shift
    cmake -S "$source_dir" -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DGFXTUNE_WERROR=ON "$@" >/dev/null
    cmake --build "$dir"
    ctest --test-dir "$dir" --output-on-failure
}

case "${1:-debug}" in
debug)
    configure_and_test /tmp/gfxtune-build
    ;;
asan)
    # Leaks are not checked: Qt and the graphics drivers keep some memory
    # until the process ends on purpose.
    ASAN_OPTIONS=detect_leaks=0 configure_and_test /tmp/gfxtune-asan -DGFXTUNE_SANITIZE=ON
    ;;
package)
    # makepkg refuses to run as root; the provisioned "builder" user does.
    if [[ $(id -u) == 0 ]]; then
        rm -rf /home/builder/gfxtune
        cp -r "$source_dir" /home/builder/gfxtune
        chown -R builder: /home/builder/gfxtune
        exec runuser -u builder -- /home/builder/gfxtune/ci/build.sh package
    fi
    cd "$source_dir/packaging/arch"
    # This system's makepkg settings, with only the job count limited.
    config=$(mktemp)
    sed "s/^MAKEFLAGS=.*/MAKEFLAGS=\"-j$CMAKE_BUILD_PARALLEL_LEVEL\"/" /etc/makepkg.conf > "$config"
    makepkg --force --noconfirm --config "$config"
    rm -f "$config"
    ;;
lint)
    cmake --build /tmp/gfxtune-build --target all_qmllint
    ;;
checks)
    reuse --root "$source_dir" lint
    cpp_files | xargs -0 clang-format --dry-run --Werror
    # groff's own checks of the manual pages: any warning fails.
    for page in "$source_dir"/doc/*.1; do
        warnings=$(sed 's/@PROJECT_VERSION@/0/' "$page" | groff -man -ww -z -rCHECKSTYLE=3 2>&1)
        if [[ -n $warnings ]]; then
            echo "$page: $warnings" >&2
            exit 1
        fi
    done
    ;;
format)
    cpp_files | xargs -0 clang-format -i
    ;;
*)
    echo "usage: $0 debug|asan|package|lint|checks|format" >&2
    exit 2
    ;;
esac
