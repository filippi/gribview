#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STAGE="$(cd "$1" && pwd)"
OUTPUT="$2"
ARCH="$(uname -m)"
case "$ARCH" in
  x86_64) SHA=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0 ;;
  aarch64) SHA=f0837e7448a0c1e4e650a93bb3e85802546e60654ef287576f46c71c126a9158 ;;
  *) echo "Unsupported architecture: $ARCH" >&2; exit 1 ;;
esac
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
curl -fL --retry 3 "https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-$ARCH.AppImage" -o "$WORK/appimagetool"
printf '%s  %s\n' "$SHA" "$WORK/appimagetool" | sha256sum -c -
chmod +x "$WORK/appimagetool"
python3 "$ROOT/tools/extract_appimage.py" "$WORK/appimagetool" "$WORK/squashfs-root" >/dev/null
cp "$ROOT/packaging/AppRun" "$STAGE/AppRun"
cp "$ROOT/packaging/gribview.desktop" "$STAGE/gribview.desktop"
cp "$ROOT/docs/icon.png" "$STAGE/gribview.png"
chmod +x "$STAGE/AppRun"
ARCH="$ARCH" "$WORK/squashfs-root/AppRun" "$STAGE" "$OUTPUT"
