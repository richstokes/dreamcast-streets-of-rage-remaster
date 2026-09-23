#!/bin/sh
# Build and run the host-side tests under AddressSanitizer and UBSan.
# Usage: tools/test-host.sh [NAME...]     (default: every test)
# Most need the staged sources (tools/build-headless.sh); the DAC tests need the
# ROM (original_rom/..., or SOR_ROM); ymfm-output needs research/ as well.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
rom=${SOR_ROM:-"$root/original_rom/Bare Knuckle - Ikari no Tetsuken ~ Streets of Rage (World).md"}
upstream="$root/build/native/upstream"
out="$root/build/tests"
cc=${CC:-cc}
cxx=${CXX:-c++}
flags="-O2 -g -fsanitize=address,undefined"
ymfm="$upstream/ymfm_opn.cpp $upstream/ymfm_adpcm.cpp $upstream/ymfm_ssg.cpp"
audio="$root/src/audio/audio_core.cpp $root/src/audio/dac_driver.cpp"
render="$root/src/render/vdp_scene.cpp $root/src/render/sprite_probe.cpp $root/src/render/art_catalog.cpp
        $root/src/render/scene_light.cpp $root/src/render/scene_particles.cpp $root/src/render/scene_weather.cpp"
vdp="$upstream/VDPRenderer.cpp $upstream/VDPState.cpp $upstream/VDPTile.cpp"
mkdir -p "$out"

# build NAME [compiler flags and sources]: the test binary at build/tests/NAME.
build() { name=$1; shift; "$cxx" -std=c++23 $flags "$@" -o "$out/$name"; }
core_object() {
    "$cc" -std=c11 $flags -I"$root/include" -c "$root/src/core/memory.c" -o "$out/memory.o"
}
dac_reference() { python3 "$root/tools/extract-dac-reference.py" "$rom" "$out/dac-reference.bin"; }

test_core() {
    "$cc" -std=c11 -Wall -Wextra -Werror $flags -I"$root/include" \
        "$root/tests/core_test.c" "$root/src/core/memory.c" -o "$out/core-test" && "$out/core-test"
}
test_native_memory() {
    core_object
    build native-memory-test -I"$root/include" -I"$root/src/dreamcast/compat" -I"$upstream" \
        "$root/tests/native_memory_test.cpp" "$out/memory.o" && "$out/native-memory-test"
}
test_cheats() {
    core_object
    build cheats-test -Wall -Wextra -Werror -I"$root/include" -I"$root/src/dreamcast" -I"$root/src/dreamcast/compat" \
        -I"$upstream" "$root/tests/cheats_test.cpp" "$root/src/dreamcast/cheats.cpp" "$out/memory.o" && "$out/cheats-test"
}
test_replay() {
    build replay-test -I"$root/src/dreamcast" -I"$root/src/dreamcast/compat" \
        "$root/tests/replay_test.cpp" "$root/src/dreamcast/replay.cpp" && "$out/replay-test"
}
test_pvr_tiles() {
    build pvr-tiles -I"$root/src/render" "$root/tests/pvr_tiles_test.cpp" && "$out/pvr-tiles"
}
test_scene() {
    inc="-I$root/src/dreamcast/compat -I$root/src/render -I$upstream"
    build scene-test $inc "$root/tests/scene_test.cpp" $render $vdp -lz && "$out/scene-test"
    build enhanced-scene-test $inc "$root/tests/enhanced_scene_test.cpp" $render $vdp -lz && "$out/enhanced-scene-test"
}
test_audio() {
    build audio-test -I"$root/src/audio" -I"$upstream" "$root/tests/audio_test.cpp" $audio $ymfm && "$out/audio-test"
}
test_psg_events() {
    build psg-events-test -I"$root/src/audio" -I"$upstream" "$root/tests/psg_events_test.cpp" $audio $ymfm && "$out/psg-events-test"
}
test_fm_quiet() {
    build fm-quiet-test -I"$upstream" "$root/tests/fm_quiet_test.cpp" "$upstream/ymfm_adpcm.cpp" "$upstream/ymfm_ssg.cpp" \
        && "$out/fm-quiet-test"
}
test_z80_hot() {
    build z80-hot-test -I"$root/src/audio" -I"$upstream" "$root/tests/z80_hot_test.cpp" && "$out/z80-hot-test"
}
test_dac_driver() {
    dac_reference
    build dac-driver-test -I"$root/src/audio" -I"$upstream" "$root/tests/dac_driver_test.cpp" "$root/src/audio/dac_driver.cpp" \
        && "$out/dac-driver-test" "$rom" "$out/dac-reference.bin"
}
test_dac_integration() {
    dac_reference
    build dac-integration-test -I"$root/src/audio" -I"$upstream" "$root/tests/dac_integration_test.cpp" $audio $ymfm \
        && "$out/dac-integration-test" "$rom" "$out/dac-reference.bin"
}
# The staged ymfm (single-channel spans, SOR_SPAN) must render exactly what the
# pinned upstream ymfm renders for the same random register writes.
test_ymfm_output() {
    pinned="$root/research/StreetsOfRageProject/MegaDriveEnvironment"
    build ymfm-pinned -I"$pinned/include/MegaDriveEnvironment/system/sound/mame_ymfm" "$root/tests/ymfm_output_test.cpp" \
        "$pinned/src/system/sound/mame_ymfm/ymfm_opn.cpp" "$pinned/src/system/sound/mame_ymfm/ymfm_adpcm.cpp" \
        "$pinned/src/system/sound/mame_ymfm/ymfm_ssg.cpp"
    build ymfm-staged -DSOR_SPAN -I"$upstream" "$root/tests/ymfm_output_test.cpp" $ymfm
    "$out/ymfm-pinned" > "$out/ymfm-pinned.pcm32"
    "$out/ymfm-staged" > "$out/ymfm-staged.pcm32"
    cmp "$out/ymfm-pinned.pcm32" "$out/ymfm-staged.pcm32"
    echo "ymfm: $(( $(wc -c < "$out/ymfm-staged.pcm32") / 8 )) stereo samples match the pinned output"
}

all="core native-memory cheats replay pvr-tiles scene audio psg-events fm-quiet z80-hot dac-driver dac-integration ymfm-output"
for name in ${@:-$all}; do
    case " $all " in *" $name "*) ;; *) echo "unknown test $name (one of: $all)" >&2; exit 2;; esac
    echo "== $name"
    "test_$(echo "$name" | tr - _)"
done
