#!/usr/bin/env bash
# Turns raw macOS screen recordings of the terminal (Resources/Diorascii_<Scene>_<Day|Night>.mov) into
# the files the README and the project page use, next to each recording:
#
#   Diorascii_<Scene>_<Day|Night>.mp4   only the terminal content (no title bar, scrollbar, shadow),
#                                       1920 px wide, 20 fps, H.264, no audio, no metadata
#   Diorascii_<Scene>_<Day|Night>.png   a still of it, 800 px wide, with rounded corners and a drop shadow
#
# The crop rectangle below is where the terminal content sits in a 2350x1930 recording of the
# window used so far (measured on the pixels: it starts after the title bar and ends before the
# scrollbar), starting 16 px right of the window edge so that the rounded bottom-left corner of the
# window stays out of it. If the recordings are made with another window size or position, adjust it; the
# script refuses recordings of another size rather than cutting the wrong part.
#
# usage: scripts/process-screengrabs.sh [directory]       (default: Resources)
#   DIORASCII_STILL_AT=4   second of the recording the still is taken from
#   a recording named Diorascii_Moon-Day.mov is treated as Diorascii_Moon_Day.mov
# requires: ffmpeg (with libx264) and ImageMagick (magick)
set -euo pipefail
cd "$(dirname "$0")/.."

dir="${1:-Resources}"
still_at="${DIORASCII_STILL_AT:-4}"
src_size="2350x1930"
crop="2074:1632:130:146"        # width:height:x:y of the terminal content
still_width=800                 # twice the 400 px the README shows it at (sharp on retina, small in the repo)
radius=22                       # corner radius of the still, in its pixels

command -v ffmpeg >/dev/null || { echo "error: ffmpeg not found" >&2; exit 1; }
command -v magick >/dev/null || { echo "error: ImageMagick (magick) not found" >&2; exit 1; }

shopt -s nullglob
recordings=("$dir"/Diorascii_*.mov)
[ ${#recordings[@]} -gt 0 ] || { echo "error: no Diorascii_*.mov in $dir" >&2; exit 1; }

for mov in "${recordings[@]}"; do
  base="$(basename "$mov" .mov)"
  base="${base//-/_}"
  size="$(ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=s=x:p=0 "$mov")"
  if [ "$size" != "$src_size" ]; then
    echo "error: $mov is $size, expected $src_size; adjust crop in $0" >&2
    exit 1
  fi
  echo "== $base"

  # Video: crop, scale to 1920 wide (height rounded to an even number), 20 fps.
  ffmpeg -v error -y -i "$mov" -an -vf "crop=$crop,scale=1920:-2:flags=lanczos,fps=20" \
    -c:v libx264 -crf 23 -preset slow -pix_fmt yuv420p -movflags +faststart -map_metadata -1 \
    "$dir/$base.mp4"

  # Still: one cropped frame, rounded corners (mask), then a soft shadow underneath.
  tmp="$(mktemp -d)"
  ffmpeg -v error -y -ss "$still_at" -i "$mov" -frames:v 1 -vf "crop=$crop,scale=$still_width:-2:flags=lanczos" "$tmp/frame.png"
  w="$(magick identify -format %w "$tmp/frame.png")"
  h="$(magick identify -format %h "$tmp/frame.png")"
  magick "$tmp/frame.png" -alpha set \
    \( -size "${w}x${h}" xc:none -fill white -draw "roundrectangle 0,0 $((w - 1)),$((h - 1)) $radius,$radius" \) \
    -compose DstIn -composite "$tmp/round.png"
  magick "$tmp/round.png" \( +clone -background black -shadow 30x13+0+8 \) +swap \
    -background none -gravity center -layers merge +repage -strip \
    -define png:compression-level=9 -define png:compression-strategy=1 "$dir/$base.png"
  rm -rf "$tmp"
done
echo "== done"
