#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
DIST_DIR="${DIST_DIR:-$ROOT/dist}"
VERSION="$(tr -d '\n' < "$BUILD_DIR/version.txt")"
ARCH="$(uname -m)"
STAGE="$DIST_DIR/gribview-$VERSION-linux-$ARCH"
mkdir -p "$DIST_DIR"
python3 "$ROOT/tools/bundle_runtime.py" --build "$BUILD_DIR" --output "$STAGE"
cp "$ROOT/packaging/AppRun" "$STAGE/gribview"
cp "$ROOT/packaging/install-linux.sh" "$STAGE/install.sh"
chmod +x "$STAGE/gribview" "$STAGE/install.sh"
tar -C "$DIST_DIR" -czf "$STAGE.tar.gz" "$(basename "$STAGE")"
if [[ -n "${APPIMAGETOOL:-}" ]]; then
  cp "$ROOT/packaging/AppRun" "$STAGE/AppRun"
  cp "$ROOT/packaging/gribview.desktop" "$STAGE/gribview.desktop"
  cp "$ROOT/docs/icon.png" "$STAGE/gribview.png"
  chmod +x "$STAGE/AppRun"
  ARCH="$ARCH" "$APPIMAGETOOL" "$STAGE" "$DIST_DIR/Gribview-$VERSION-$ARCH.AppImage"
fi
echo "Portable runtime: $STAGE"
