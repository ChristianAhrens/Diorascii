#!/usr/bin/env bash
# Regenerates the headless "screenshots" of the user manual: every block in docs/manual/*.md of the form
#
#     <!-- dump: --scene forest --seed 5 --size 80x20 --dump 200 -->
#     ```text
#     ...generated, do not edit...
#     ```
#     <!-- /dump -->
#
# is replaced by the output of `diorascii <the arguments of the marker>`, which is deterministic for a
# given seed. Because the output is plain text (no colors), it also shows on GitHub.
#
# usage: scripts/manual-dumps.sh           rewrite the blocks in place
#        scripts/manual-dumps.sh --check   change nothing, exit 1 when a block is out of date
#   DIORASCII_BIN=path   the binary (default: build/release/src/diorascii)
set -euo pipefail
cd "$(dirname "$0")/.."

bin="${DIORASCII_BIN:-build/release/src/diorascii}"
check=0; [ "${1:-}" = "--check" ] && check=1
[ -x "$bin" ] || { echo "error: $bin not found (scripts/build.sh release first, or set DIORASCII_BIN)" >&2; exit 1; }

stale=0
for md in docs/manual/*.md; do
  tmp="$(mktemp)"
  skipping=0
  while IFS= read -r line || [ -n "$line" ]; do
    if [ "$skipping" = 1 ]; then
      [ "$line" = "<!-- /dump -->" ] && { skipping=0; echo "$line" >> "$tmp"; }
      continue
    fi
    echo "$line" >> "$tmp"
    if [[ "$line" =~ ^'<!-- dump: '(.*)' -->'$ ]]; then
      args="${BASH_REMATCH[1]}"
      echo '```text' >> "$tmp"
      # shellcheck disable=SC2086  # the marker's arguments are meant to be split
      "$bin" $args | sed -E 's/[[:space:]]+$//' | sed -e :a -e '/^\n*$/{$d;N;ba' -e '}' >> "$tmp"
      echo '```' >> "$tmp"
      skipping=1
    fi
  done < "$md"
  if ! cmp -s "$md" "$tmp"; then
    if [ "$check" = 1 ]; then echo "out of date: $md" >&2; stale=1; else cp "$tmp" "$md"; echo "updated $md"; fi
  fi
  rm -f "$tmp"
done
exit $stale
