#!/usr/bin/env bash
# End-to-end release helper. Builds artifacts locally (source tarball, macOS DMG, Homebrew bottle),
# pushes the tag, dispatches the GitHub Actions release workflow, polls every 4s, downloads CI
# artifacts, and uploads any missing assets to the GitHub release.
#
# Requirements: gh (authenticated), git, cmake+ninja toolchain, brew, jq, and a macOS build
# environment with deps installed. Optional env toggles:
#   ALLOW_DIRTY=1    skip clean-check
#   DRY_RUN=1        skip pushes/release uploads
#   POLL_INTERVAL=4  seconds between gh poll loops

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

log() { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
require() { command -v "$1" >/dev/null 2>&1 || { echo "Missing required command: $1" >&2; exit 1; }; }

require gh
require git
require cmake
require brew
require jq

POLL_INTERVAL="${POLL_INTERVAL:-4}"
DRY_RUN="${DRY_RUN:-0}"

if [[ -z "${ALLOW_DIRTY:-}" && -n "$(git status --porcelain)" ]]; then
  git status --short >&2
  echo "Refusing to continue with a dirty working tree. Set ALLOW_DIRTY=1 to override." >&2
  exit 1
fi

VERSION="$(python - <<'PY'
import pathlib, re
text = pathlib.Path("CMakeLists.txt").read_text()
m = re.search(r'project\(gribview VERSION ([0-9]+(?:\.[0-9]+)*)', text)
print(m.group(1) if m else "", end="")
PY)"
if [[ -z "$VERSION" ]]; then
  echo "Could not parse version from CMakeLists.txt" >&2
  exit 1
fi
TAG="v${VERSION}"
log "Releasing version ${VERSION} (${TAG})"

log "Checking gh auth"
gh auth status >/dev/null

log "Cleaning dist and previous artifacts"
rm -rf dist release_artifacts artifacts
mkdir -p dist
rm -rf build  # ensure clean configure (generator can differ across runs)

log "Configuring and building (Release)"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

log "Packaging source archive and tagging (${TAG})"
ALLOW_DIRTY="${ALLOW_DIRTY:-}" ./tools/release_homebrew.sh "$VERSION"

log "Building macOS DMG (needs built binary)"
./tools/package_macos.sh

log "Building Homebrew bottle locally"
brew uninstall --force gribview >/dev/null 2>&1 || true
brew install --build-bottle ./homebrew/gribview.rb
brew bottle --json ./homebrew/gribview.rb
mv ./*.bottle.tar.gz dist/ || true
mv ./*.bottle.json dist/ || true

if [[ "$DRY_RUN" == "1" ]]; then
  log "DRY_RUN=1 set; skipping push, workflow dispatch, and release upload."
  exit 0
fi

log "Pushing tag ${TAG} to origin"
git push origin "${TAG}"

log "Dispatching CI workflow on ${TAG}"
gh workflow run Build --ref "${TAG}"
sleep "$POLL_INTERVAL"

log "Waiting for CI run to finish (polling every ${POLL_INTERVAL}s)"
RUN_ID=""
for _ in {1..60}; do
  RUN_ID="$(gh run list --workflow Build --json databaseId,headBranch,status,conclusion --limit 5 --branch "${TAG}" | jq -r 'map(select(.headBranch=="'"${TAG}"'"))[0].databaseId')"
  [[ "$RUN_ID" != "null" && -n "$RUN_ID" ]] && break
  sleep "$POLL_INTERVAL"
done
if [[ -z "$RUN_ID" || "$RUN_ID" == "null" ]]; then
  echo "Could not find the workflow run for ${TAG}" >&2
  exit 1
fi

while true; do
  status_json="$(gh run view "$RUN_ID" --json status,conclusion)"
  status="$(echo "$status_json" | jq -r '.status')"
  conclusion="$(echo "$status_json" | jq -r '.conclusion')"
  log "Run ${RUN_ID}: status=${status} conclusion=${conclusion}"
  [[ "$status" == "completed" ]] && break
  sleep "$POLL_INTERVAL"
done
if [[ "$conclusion" != "success" ]]; then
  echo "CI run ${RUN_ID} failed with conclusion=${conclusion}" >&2
  exit 1
fi

log "Downloading CI artifacts"
mkdir -p dist/ci
gh run download "$RUN_ID" -n packages-Linux.zip -D dist/ci || true
gh run download "$RUN_ID" -n packages-macOS.zip -D dist/ci || true
gh run download "$RUN_ID" -n packages-Windows.zip -D dist/ci || true
gh run download "$RUN_ID" -n gribview.tar.gz -D dist/ci || true

log "Ensuring GitHub release ${TAG} exists"
if ! gh release view "${TAG}" >/dev/null 2>&1; then
  gh release create "${TAG}" dist/gribview-${VERSION}.tar.gz dist/Gribview.dmg --title "${TAG}" --notes "Release ${VERSION}"
fi

log "Uploading extra assets to release (${TAG})"
gh release upload "${TAG}" dist/Gribview.dmg --clobber
for f in dist/*.bottle.tar.gz dist/*.bottle.json dist/ci/packages-*.zip; do
  [[ -f "$f" ]] && gh release upload "${TAG}" "$f" --clobber
done

log "Release ${TAG} done."
