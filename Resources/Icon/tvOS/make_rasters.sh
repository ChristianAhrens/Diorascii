#!/usr/bin/env bash
# Renders the raster assets of the tvOS brand assets from the layer SVGs (needs ImageMagick):
#   - the 2x images of every layer of the large (1280x768) and small (400x240) app icon
#   - the Top Shelf images (standard 1920x720 and wide 2320x720, each at 1x and 2x), cropped from the
#     composite of the three layers
# The 1x layer images are written by make_icons.py. Draft art: replace the Top Shelf crop with a
# dedicated design when there is one.
#
# usage: tvOS/make_rasters.sh [variant-dir]     default: this directory (day); tvOS/night for the night variant
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
dir="${1:-$here}"
brand="$dir/App Icon & Top Shelf Image.brandassets"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

render() {  # render <layer> <width> <height> <out>   (transparent PNG, from the 1280x768 SVG)
  magick -background none -density 600 "$dir/layers/$1.svg" -resize "${2}x${3}!" PNG32:"$4"
}

# 2x layer images for both icon sizes
for layer in back middle front; do
  Layer="$(printf '%s' "${layer:0:1}" | tr a-z A-Z)${layer:1}"
  render "$layer" 2560 1536 "$brand/App Icon - Large.imagestack/$Layer.imagestacklayer/Content.imageset/$Layer@2x.png"
  render "$layer" 800 480 "$brand/App Icon - Small.imagestack/$Layer.imagestacklayer/Content.imageset/$Layer@2x.png"
done

# Top Shelf: composite the layers large, then crop the band with the eagles, clouds and peaks
for layer in back middle front; do render "$layer" 5120 3072 "$tmp/$layer.png"; done
magick "$tmp/back.png" "$tmp/middle.png" -composite "$tmp/front.png" -composite "$tmp/composite.png"

shelf() {  # shelf <name> <width> <height> <crop-y>   (crop the full-width band, then scale)
  local name="$1" w="$2" h="$3" y="$4"
  # full-width band of the right aspect from the 5120x3072 composite
  local bandh=$((5120 * h / w))
  magick "$tmp/composite.png" -crop "5120x${bandh}+0+${y}" +repage -resize "${w}x${h}!" -alpha off PNG24:"$name"
}
std="$brand/Top Shelf Image.imageset"; wide="$brand/Top Shelf Image Wide.imageset"
mkdir -p "$std" "$wide"
shelf "$std/Top Shelf Image.png" 1920 720 180
shelf "$std/Top Shelf Image@2x.png" 3840 1440 180
shelf "$wide/Top Shelf Image Wide.png" 2320 720 700
shelf "$wide/Top Shelf Image Wide@2x.png" 4640 1440 700
# Catalog descriptions: 1x and 2x for every layer, the Top Shelf image sets, and the brand asset list
for size in Large Small; do
  for Layer in Back Middle Front; do
    cat > "$brand/App Icon - $size.imagestack/$Layer.imagestacklayer/Content.imageset/Contents.json" <<JSON
{
  "images": [
    { "idiom": "tv", "filename": "$Layer.png", "scale": "1x" },
    { "idiom": "tv", "filename": "$Layer@2x.png", "scale": "2x" }
  ],
  "info": { "author": "xcode", "version": 1 }
}
JSON
  done
done
for set in "Top Shelf Image" "Top Shelf Image Wide"; do
  cat > "$brand/$set.imageset/Contents.json" <<JSON
{
  "images": [
    { "idiom": "tv", "filename": "$set.png", "scale": "1x" },
    { "idiom": "tv", "filename": "$set@2x.png", "scale": "2x" }
  ],
  "info": { "author": "xcode", "version": 1 }
}
JSON
done
cat > "$brand/Contents.json" <<JSON
{
  "assets": [
    { "filename": "App Icon - Large.imagestack", "idiom": "tv", "role": "primary-app-icon", "size": "1280x768" },
    { "filename": "App Icon - Small.imagestack", "idiom": "tv", "role": "primary-app-icon", "size": "400x240" },
    { "filename": "Top Shelf Image.imageset", "idiom": "tv", "role": "top-shelf-image", "size": "1920x720" },
    { "filename": "Top Shelf Image Wide.imageset", "idiom": "tv", "role": "top-shelf-image-wide", "size": "2320x720" }
  ],
  "info": { "author": "xcode", "version": 1 }
}
JSON
echo "rendered into $brand"
