#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF' >&2
Usage: release_homebrew.sh <version>

Creates a Git tag named v<version>, builds the Homebrew source archive,
and refreshes homebrew/gribview.rb so it installs the release tarball.

You still have to upload dist/gribview-<version>.tar.gz as the GitHub
release asset for v<version> and push the tag/changes.
EOF
  exit 1
}

if [[ $# -ne 1 ]]; then
  usage
fi

VERSION="$1"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FORMULA="$ROOT/homebrew/gribview.rb"

cd "$ROOT"

if [[ -z "${ALLOW_DIRTY:-}" && -n "$(git status --porcelain)" ]]; then
  echo "Please clean your working tree before releasing." >&2
  git status --short >&2
  exit 1
fi

ARCHIVE_NAME="gribview-${VERSION}.tar.gz"
ARCHIVE_PATH="$ROOT/dist/$ARCHIVE_NAME"

./tools/package_homebrew.sh VERSION="$VERSION"

if [[ ! -f "$ARCHIVE_PATH" ]]; then
  echo "Missing expected archive $ARCHIVE_PATH" >&2
  exit 1
fi

SHA="$(shasum -a 256 "$ARCHIVE_PATH" | awk '{print $1}')"

BREW_URL="https://github.com/filippi/gribview/releases/download/v${VERSION}/${ARCHIVE_NAME}"

BREW_URL="$BREW_URL" SHA="$SHA" python - <<PY
from pathlib import Path
import os
import re

formula = Path("$FORMULA")
text = formula.read_text()
brew_url = os.environ["BREW_URL"]
sha = os.environ["SHA"]
text = re.sub(r'url "[^"]+"', f'url "{brew_url}"', text, count=1)
text = re.sub(r'sha256 "[^"]+"', f'sha256 "{sha}"', text, count=1)
formula.write_text(text)
PY

git tag -f "v${VERSION}"

echo "Prepared release v${VERSION}"
echo "  archive: $ARCHIVE_PATH"
echo "  sha256 : $SHA"
echo "Run:"
echo "  git add dist/${ARCHIVE_NAME} homebrew/gribview.rb"
echo "  git commit -m \"Release v${VERSION}\""
echo "  git push --follow-tags"
echo "Then create a GitHub release v${VERSION} and attach dist/${ARCHIVE_NAME}."
