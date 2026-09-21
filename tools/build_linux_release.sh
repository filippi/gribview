#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-/tmp/gribview-build}"
DIST_DIR="${DIST_DIR:-/out}"
cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$BUILD_DIR" -j "${JOBS:-4}"
ctest --test-dir "$BUILD_DIR" --output-on-failure --no-tests=error
mkdir -p "$DIST_DIR"
(cd "$BUILD_DIR" && cpack -G DEB -B "$DIST_DIR")
BUILD_DIR="$BUILD_DIR" DIST_DIR="$DIST_DIR" bash "$ROOT/tools/package_linux.sh"
VERSION="$(tr -d '\n' < "$BUILD_DIR/version.txt")"
RUNTIME="$DIST_DIR/gribview-$VERSION-linux-$(uname -m)"
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a python3 "$ROOT/tools/test_artifact.py" "$RUNTIME/bin/gribview" "$ROOT/docs/sample.grib" --screenshot "$DIST_DIR/linux.png"
python3 "$ROOT/tools/wheel_backend.py" --runtime "$RUNTIME" --output "$DIST_DIR"
if [[ "${BUILD_APPIMAGE:-1}" == 1 ]]; then
  bash "$ROOT/tools/make_appimage.sh" "$RUNTIME" "$DIST_DIR/Gribview-$VERSION-$(uname -m).AppImage"
fi
