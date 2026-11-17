#!/usr/bin/env sh
# CPU cost of the ambient synth per scene and speaker layout (not part of ctest).
# usage: scripts/bench-audio.sh [seconds-per-cell]   (builds the release preset first)
set -eu
cd "$(dirname "$0")/.."
preset=release
[ "$(uname -s)" = Darwin ] && preset=release-macos

cmake --preset "$preset" >/dev/null
cmake --build --preset "$preset" --target diorascii_bench_audio -j
"build/$preset/tests/diorascii_bench_audio" "$@"
