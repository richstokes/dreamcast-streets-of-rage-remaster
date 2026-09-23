#!/bin/sh
# Check that what a build step needs is installed, and say where to get it if not.
#
# Usage: tools/check-requirements.sh GROUP...   (or: all)
#   rom=PATH   the ROM file, validated with tools/rom.py
#   python     python3, and the interpreter for code generation ($PYTHON, default python3.14)
#   git        for the first-build clone of the research repositories
#   kos        KallistiOS environment ($KOS_ENV) with an SH-4 GCC that has C++
#   bin2o      KOS's bin2o, used to embed the ROM in the test ELF
#   mkdcdisc   for the disc image ($MKDCDISC, build/mkdcdisc/build/mkdcdisc, or on PATH)
#   flycast    Flycast.app on macOS ($FLYCAST_BIN)
#   host       cmake and a C++ compiler for the host (headless) build
#   research   the research checkouts (python3 tools/bootstrap.py)
#   art        what tools/make-art-set.sh needs: headless build, reference core, numpy + Pillow
# Exits 1 with every problem listed if anything is missing; `all` also lists what was found.
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
missing=0; verbose=0
ok() { [ "$verbose" = 1 ] && echo "  ok       $1"; }
fail() { missing=1; echo "  MISSING  $1"; shift; for line in "$@"; do echo "           $line"; done; }
have() { command -v "$1" > /dev/null 2>&1; }

check_rom() {
    rom=$1
    if [ ! -f "$rom" ]; then
        fail "ROM: $rom" \
            'Supply your own Streets of Rage / Bare Knuckle (World) revision 00 ROM:' \
            '524,288 bytes, plain big-endian, no header (see README.md, "What you need").' \
            'Put it at original_rom/Bare Knuckle - Ikari no Tetsuken ~ Streets of Rage (World).md' \
            'or pass its path as the first argument (or SOR_ROM).'
    elif ! err=$(python3 "$root/tools/rom.py" "$rom" --require-known 2>&1 > /dev/null); then
        fail "ROM: $rom is not the supported dump" "$err" \
            'Needed: Streets of Rage (World) revision 00, 524,288 bytes, plain big-endian.' \
            'Other revisions, headered or byte-swapped dumps are not supported.'
    else ok "ROM: $rom"; fi
}
check_python() {
    if have python3; then ok "python3 ($(python3 --version 2>&1))"; else
        fail 'python3' 'Install Python 3 (https://www.python.org/downloads/ or `brew install python`).'; fi
    py=${PYTHON:-python3.14}
    if have "$py"; then ok "$py for code generation ($("$py" --version 2>&1))"; else
        fail "$py (code generation)" \
            'Install Python 3.14 (`brew install python@3.14`, or https://www.python.org/downloads/),' \
            'or point PYTHON at another recent interpreter: PYTHON=python3.13 ./build-cdi.sh'; fi
}
check_git() {
    if have git; then ok 'git'; else
        fail 'git' 'Needed to fetch the research repositories on the first build.' \
            'Install it with `xcode-select --install` (macOS) or your package manager.'; fi
}
check_kos() {
    env=${KOS_ENV:-$HOME/.local/share/dreamcast/kos/environ.sh}
    if [ ! -f "$env" ]; then
        fail "KallistiOS environment: $env" \
            'Install KallistiOS and its SH-4 toolchain with C++ (kos-chain with enable_cpp=1):' \
            '  https://kos-docs.dreamcast.wiki/  (Getting started)' \
            'Versions this project builds with are in docs/TOOLCHAIN.md.' \
            'If KOS is installed elsewhere: KOS_ENV=/path/to/kos/environ.sh'
        return
    fi
    ok "KallistiOS environment: $env"
    # environ.sh reads variables that may be unset.
    set +u; . "$env" > /dev/null 2>&1; set -u
    if ! have sh-elf-gcc; then
        fail 'SH-4 compiler (sh-elf-gcc) not on PATH after sourcing environ.sh' \
            'Check the KOS toolchain build finished and environ.sh points at it (KOS_CC_BASE).'
    elif ! have sh-elf-g++; then
        fail 'SH-4 C++ compiler (sh-elf-g++)' \
            'The toolchain was built without C++. Rebuild it with C++ enabled:' \
            '  make -C "$KOS_BASE/utils/kos-chain" ... enable_cpp=1   (docs/TOOLCHAIN.md)'
    else ok "SH-4 C++ compiler ($(sh-elf-g++ -dumpversion))"; fi
    if have kos-c++; then ok 'kos-c++ wrapper'; else
        fail 'kos-c++ wrapper not on PATH' 'environ.sh should add $KOS_BASE/utils/gnu_wrappers; check your KOS install.'; fi
    if have make; then ok 'make'; else fail 'make' 'Install build tools (`xcode-select --install` on macOS).'; fi
}
check_bin2o() {
    env=${KOS_ENV:-$HOME/.local/share/dreamcast/kos/environ.sh}
    [ -f "$env" ] || return    # reported by check_kos
    set +u; . "$env" > /dev/null 2>&1; set -u
    if [ -x "$KOS_BASE/utils/bin2o/bin2o" ]; then ok 'bin2o'; else
        fail "bin2o: $KOS_BASE/utils/bin2o/bin2o" \
            'KOS builds it with its utilities: make -C "$KOS_BASE/utils/bin2o"'; fi
}
check_mkdcdisc() {
    m=${MKDCDISC:-$root/build/mkdcdisc/build/mkdcdisc}
    if [ -x "$m" ]; then ok "mkdcdisc: $m"
    elif have mkdcdisc; then ok "mkdcdisc: $(command -v mkdcdisc)"
    else
        fail 'mkdcdisc (makes the disc image)' \
            'Build it from https://gitlab.com/simulant/mkdcdisc (needs meson, ninja, libisofs):' \
            '  brew install meson ninja libisofs     # macOS' \
            "  git clone https://gitlab.com/simulant/mkdcdisc.git $root/build/mkdcdisc" \
            "  meson setup $root/build/mkdcdisc/build $root/build/mkdcdisc && ninja -C $root/build/mkdcdisc/build" \
            'Or install it anywhere and set MKDCDISC=/path/to/mkdcdisc.'
    fi
}
check_flycast() {
    if [ "$(uname -s)" != Darwin ]; then
        fail 'tools/run-flycast.sh launches Flycast on macOS only' \
            "On this system open dist/sor.cdi in Flycast yourself: https://github.com/flyinghead/flycast/releases"
        return
    fi
    f=${FLYCAST_BIN:-$HOME/.local/share/dreamcast/flycast/Flycast.app/Contents/MacOS/Flycast}
    [ -x "$f" ] || f=/Applications/Flycast.app/Contents/MacOS/Flycast
    if [ -x "$f" ]; then ok "Flycast: $f"; else
        fail 'Flycast.app' \
            'Download Flycast from https://github.com/flyinghead/flycast/releases and put' \
            'Flycast.app in /Applications, or set FLYCAST_BIN=/path/to/Flycast.app/Contents/MacOS/Flycast'; fi
}
check_host() {
    if have cmake; then ok "cmake ($(cmake --version | head -1))"; else
        fail 'cmake (3.24 or newer)' 'Install it: `brew install cmake` or https://cmake.org/download/'; fi
    if have c++; then ok 'host C++ compiler'; else
        fail 'a host C++ compiler (c++)' 'Install Xcode command line tools (`xcode-select --install`) or g++/clang.'; fi
}
check_research() {
    if [ -d "$root/research/StreetsOfRageProject/RageDecompiler/tools" ] && [ -d "$root/research/Genesis-Plus-GX" ]; then
        ok 'research checkouts'
    else
        fail 'research checkouts (research/)' \
            'The build scripts fetch them on the first run; to do it now: python3 tools/bootstrap.py' \
            '(needs git and network access; nothing ROM-related is downloaded).'
    fi
}
check_art() {
    if [ -x "$root/build/headless/sor-headless" ]; then ok 'headless build'; else
        fail 'headless build (build/headless/sor-headless)' 'Run tools/build-headless.sh first.'; fi
    if [ -f "$root/build/gpgx-profile/genesis_plus_gx_libretro.dylib" ]; then ok 'reference core'; else
        fail 'reference core (build/gpgx-profile/genesis_plus_gx_libretro.dylib)' 'Run tools/build-profile-core.sh first.'; fi
    py="$root/build/tools-venv/bin/python3"
    if [ ! -x "$py" ]; then
        fail 'Python environment build/tools-venv' \
            "  python3 -m venv $root/build/tools-venv && $root/build/tools-venv/bin/pip install numpy pillow"
    elif ! "$py" -c 'import numpy, PIL' > /dev/null 2>&1; then
        fail 'numpy and Pillow in build/tools-venv' "  $root/build/tools-venv/bin/pip install numpy pillow"
    else ok 'build/tools-venv with numpy and Pillow'; fi
}

[ $# -gt 0 ] || { echo 'usage: tools/check-requirements.sh GROUP... | all' >&2; exit 2; }
if [ "$1" = all ]; then
    verbose=1
    set -- "rom=${SOR_ROM:-$root/original_rom/Bare Knuckle - Ikari no Tetsuken ~ Streets of Rage (World).md}" \
        python git kos bin2o mkdcdisc flycast host research art
fi
echo 'Checking requirements:'
for group in "$@"; do
    case $group in
        rom=*) check_rom "${group#rom=}";;
        python) check_python;; git) check_git;; kos) check_kos;; bin2o) check_bin2o;;
        mkdcdisc) check_mkdcdisc;; flycast) check_flycast;; host) check_host;;
        research) check_research;; art) check_art;;
        *) echo "unknown requirement group: $group" >&2; exit 2;;
    esac
done
if [ "$missing" = 1 ]; then
    echo 'Something is missing (see above). README.md, "What you need", covers the setup.' >&2
    exit 1
fi
