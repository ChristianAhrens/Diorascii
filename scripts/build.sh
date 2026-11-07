#!/usr/bin/env sh
# Build, test and package on macOS / Linux.   usage: scripts/build.sh [dev|release]
set -eu
cd "$(dirname "$0")/.."
mode="${1:-release}"
preset="$mode"
[ "$mode" = release ] && [ "$(uname -s)" = Darwin ] && preset=release-macos

cmake --preset "$preset"
cmake --build --preset "$preset" -j
if [ "$mode" = dev ]; then
  ctest --preset dev
else
  ctest --test-dir "build/$preset" --output-on-failure
  (cd "build/$preset" && cpack)
  echo "binary: build/$preset/src/diorascii"
fi
