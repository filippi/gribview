#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST_DIR="${DIST_DIR:-$ROOT/dist}"

cd "$ROOT"
VERSION="${VERSION:-$(python - <<'PY'
import pathlib, re

text = pathlib.Path("CMakeLists.txt").read_text()
match = re.search(r'project\(gribview VERSION ([0-9]+(?:\.[0-9]+)*)', text)
print(match.group(1) if match else "0.0")
PY)}"
ARCHIVE_NAME="gribview-${VERSION}.tar.gz"
ARCHIVE_PATH="$DIST_DIR/$ARCHIVE_NAME"

mkdir -p "$DIST_DIR"
rm -f "$ARCHIVE_PATH"

echo "Packing source archive: $ARCHIVE_NAME"
git archive --format=tar.gz --prefix="${ARCHIVE_NAME%.tar.gz}/" HEAD -o "$ARCHIVE_PATH"

sha="$(shasum -a 256 "$ARCHIVE_PATH" | awk '{print $1}')"

echo "Created $ARCHIVE_PATH"
echo "SHA256: $sha"
