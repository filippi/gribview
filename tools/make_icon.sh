#!/bin/bash

SRC="icon.png"
OUT="AppIcon.iconset"

mkdir -p "$OUT"
rm -f "$OUT"/*.png

if ! command -v magick >/dev/null 2>&1 && ! command -v convert >/dev/null 2>&1; then
  echo "ImageMagick 'magick' or 'convert' command not found." >&2
  exit 1;
fi

run_convert() {
  if command -v magick >/dev/null 2>&1; then
    magick "$@"
  else
    convert "$@"
  fi
}

# macOS icon set
run_convert "$SRC" -resize 16x16   "$OUT/icon_16x16.png"
run_convert "$SRC" -resize 32x32   "$OUT/icon_16x16@2x.png"
run_convert "$SRC" -resize 32x32   "$OUT/icon_32x32.png"
run_convert "$SRC" -resize 64x64   "$OUT/icon_32x32@2x.png"
run_convert "$SRC" -resize 128x128 "$OUT/icon_128x128.png"
run_convert "$SRC" -resize 256x256 "$OUT/icon_128x128@2x.png"
run_convert "$SRC" -resize 256x256 "$OUT/icon_256x256.png"
run_convert "$SRC" -resize 512x512 "$OUT/icon_256x256@2x.png"
run_convert "$SRC" -resize 512x512 "$OUT/icon_512x512.png"
run_convert "$SRC" -resize 1024x1024 "$OUT/icon_512x512@2x.png"

echo "Done. Icons written to $OUT/"
