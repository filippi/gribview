#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
exec python3 "$ROOT/tools/package_macos.py" --build "${BUILD_DIR:-$ROOT/build}" --dist "${DIST_DIR:-$ROOT/dist}"
