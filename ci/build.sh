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
#   ci/build.sh tidy      build with clang, warnings as errors, then clang-tidy
#   ci/build.sh checks    licence information (REUSE), C++ formatting, manual pages,
#                         the AppStream file, and that po/gfxtune.pot has every text
#   ci/build.sh format    formats the C++ code as .clang-format says
#   ci/build.sh messages  writes po/gfxtune.pot, the template for translations
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

# Every text the window, gfxtune-run and the command line show, for
# translators (KDE's i18n keywords; QML files read as C, as KDE does).
# Without line numbers, so the template only changes when a text does.
extract_messages() {
    local files
    mapfile -t files < <(cd "$source_dir" && find src \( -name '*.cpp' -o -name '*.h' -o -name '*.qml' \) \
        -not -path 'src/layer/*' | sort)
    (cd "$source_dir" && xgettext --from-code=UTF-8 -C --kde -ci18n \
        -ki18n:1 -ki18nc:1c,2 -ki18np:1,2 -ki18ncp:1c,2,3 -kki18n:1 -kki18nc:1c,2 -kki18np:1,2 -kki18ncp:1c,2,3 \
        -kxi18n:1 -kxi18nc:1c,2 -kxi18np:1,2 -kxi18ncp:1c,2,3 -kI18N_NOOP:1 -kI18NC_NOOP:1c,2 \
        --add-location=file --package-name=gfxtune --copyright-holder="gfxtune contributors" \
        -o "$1" "${files[@]}")
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
tidy)
    # clang-tidy reads the generated sources (moc, shaders) too: build first.
    cmake -S "$source_dir" -B /tmp/gfxtune-tidy -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DGFXTUNE_WERROR=ON >/dev/null
    cmake --build /tmp/gfxtune-tidy
    run-clang-tidy -p /tmp/gfxtune-tidy -config-file "$source_dir/.clang-tidy" -quiet -warnings-as-errors='*' "$source_dir/src/.*\.cpp$"
    ;;
checks)
    reuse --root "$source_dir" lint
    cpp_files | xargs -0 clang-format --dry-run --Werror
    # The translation template must list today's texts.
    template=$(mktemp)
    extract_messages "$template"
    if ! diff <(grep -v '^"POT-Creation-Date' "$template") <(grep -v '^"POT-Creation-Date' "$source_dir/po/gfxtune.pot") >/dev/null; then
        echo "po/gfxtune.pot is out of date: run ci/build.sh messages" >&2
        rm -f "$template"
        exit 1
    fi
    rm -f "$template"
    # The software centre entry, with AppStream's strictest checks; links are
    # checked where it is installed, not here.
    appstreamcli validate --no-net --pedantic "$source_dir"/data/*.metainfo.xml
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
messages)
    extract_messages "$source_dir/po/gfxtune.pot"
    ;;
*)
    echo "usage: $0 debug|asan|package|lint|tidy|checks|format|messages" >&2
    exit 2
    ;;
esac
