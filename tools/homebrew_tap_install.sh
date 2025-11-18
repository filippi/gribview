#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FORMULA_SRC="$ROOT/homebrew/gribview.rb"
TAP="${TAP:-filippi/gribview}"

if ! command -v brew >/dev/null 2>&1; then
  echo "Homebrew is required to run this script." >&2
  exit 1
fi

if ! brew tap | grep -q "^${TAP}$"; then
  echo "Creating tap ${TAP}"
  brew tap-new "$TAP"
fi

TAP_REPO="$(brew --repo "$TAP")"
FORMULA_DIR="$TAP_REPO/Formula"
mkdir -p "$FORMULA_DIR"
cp "$FORMULA_SRC" "$FORMULA_DIR/gribview.rb"

brew install --build-from-source "${TAP}/gribview"
