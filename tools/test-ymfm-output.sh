#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
base="$root/research/StreetsOfRageProject/MegaDriveEnvironment"
mkdir -p "$root/build/tests"
for variant in pinned staged; do
 if [ "$variant" = pinned ]; then
  headers="$base/include/MegaDriveEnvironment/system/sound/mame_ymfm"
  sources="$base/src/system/sound/mame_ymfm"
 else
  headers="$root/build/native/upstream"; sources="$headers"
 fi
 span=; [ "$variant" = staged ] && span=-DSOR_SPAN
 clang++ -std=c++23 -O2 -g -fsanitize=address,undefined $span -I"$headers" \
  "$root/tests/ymfm_output_test.cpp" "$sources/ymfm_opn.cpp" \
  "$sources/ymfm_adpcm.cpp" "$sources/ymfm_ssg.cpp" \
  -o "$root/build/tests/ymfm-$variant"
 "$root/build/tests/ymfm-$variant" > "$root/build/tests/ymfm-$variant.pcm32"
done
cmp "$root/build/tests/ymfm-pinned.pcm32" "$root/build/tests/ymfm-staged.pcm32"
echo "ymfm: $(( $(wc -c < "$root/build/tests/ymfm-staged.pcm32") / 8 )) stereo samples match pinned output; staged renders random channel-major spans (LFO, DAC, SSG-EG, prepares, state restoration)"
