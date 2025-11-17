#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
BIN="$BUILD_DIR/bin/gribview"

if [[ ! -x "$BIN" ]]; then
  echo "Missing binary at $BIN. Build the project first (e.g. ./build.sh)." >&2
  exit 1
fi

DIST_DIR="$ROOT/dist"
APP_NAME="Gribview"
APP_DIR="$DIST_DIR/${APP_NAME}.app"
CONTENTS="$APP_DIR/Contents"
MACOS="$CONTENTS/MacOS"
FRAMEWORKS="$CONTENTS/Frameworks"
RESOURCES="$CONTENTS/Resources"
DMG_PATH="$DIST_DIR/${APP_NAME}.dmg"
ECCODES_RES="$RESOURCES/eccodes"

rm -rf "$APP_DIR"
mkdir -p "$MACOS" "$FRAMEWORKS" "$RESOURCES"
mkdir -p "$DIST_DIR"

cp "$BIN" "$MACOS/gribview"
chmod 755 "$MACOS/gribview"

# icons ------------------------------------------------------
ICON_PNG="$ROOT/tools/icon.png"
ICONSET_DIR="$ROOT/tools/AppIcon.iconset"
ICON_ICNS="$ROOT/tools/${APP_NAME}.icns"
if [[ -f "$ICON_PNG" && -x "$ROOT/tools/make_icon.sh" ]]; then
  echo "Generating icon set from $ICON_PNG"
  if ! (cd "$ROOT/tools" && ./make_icon.sh >/dev/null); then
    echo "Warning: icon generation failed; continuing without custom icon." >&2
  fi
fi
if [[ -d "$ICONSET_DIR" && -n "$(command -v iconutil)" ]]; then
  echo "Converting iconset to icns"
  if ! iconutil -c icns "$ICONSET_DIR" -o "$ICON_ICNS"; then
    echo "Warning: iconutil failed; continuing without custom icon." >&2
    ICON_ICNS=""
  fi
fi
if [[ -f "$ICON_ICNS" ]]; then
  cp "$ICON_ICNS" "$RESOURCES/${APP_NAME}.icns"
else
  echo "Note: using default macOS app icon (no custom icon found)." >&2
fi

cat > "$CONTENTS/Info.plist" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key>
  <string>Gribview</string>
  <key>CFBundleExecutable</key>
  <string>gribview</string>
  <key>CFBundleIdentifier</key>
  <string>org.firecaster.gribview</string>
  <key>CFBundleVersion</key>
  <string>1.0</string>
  <key>CFBundlePackageType</key>
  <string>APPL</string>
  <key>CFBundleShortVersionString</key>
  <string>1.0</string>
  <key>NSHighResolutionCapable</key>
  <true/>
  <key>LSApplicationCategoryType</key>
  <string>public.app-category.developer-tools</string>
  <key>CFBundleIconFile</key>
  <string>Gribview</string>
  <key>CFBundleDocumentTypes</key>
  <array>
    <dict>
      <key>CFBundleTypeName</key>
      <string>GRIB file</string>
      <key>CFBundleTypeRole</key>
      <string>Viewer</string>
      <key>LSHandlerRank</key>
      <string>Owner</string>
      <key>CFBundleTypeExtensions</key>
      <array>
        <string>grib</string>
        <string>grb</string>
        <string>grib2</string>
        <string>grb2</string>
      </array>
    </dict>
  </array>
</dict>
</plist>
EOF
ECCODES_PREFIX_GUESSED=""

resolve_path() {
  local dep="$1"
  local loader="$2"
  case "$dep" in
    /System/*|/usr/lib/*)
      return 1
      ;;
    @executable_path/*)
      local rel="${dep#@executable_path/}"
      printf "%s/%s" "$MACOS" "$rel"
      return 0
      ;;
    @loader_path/*)
      local dir
      dir="$(cd "$(dirname "$loader")" && pwd)"
      local rel="${dep#@loader_path/}"
      printf "%s/%s" "$dir" "$rel"
      return 0
      ;;
    @rpath/*)
      local sub="${dep#@rpath/}"
      local rpath
      while read -r marker value; do
        if [[ "$marker" == "path" ]]; then
          local base="$value"
          case "$base" in
            @loader_path/*)
              local dir
              dir="$(cd "$(dirname "$loader")" && pwd)"
              base="$dir/${base#@loader_path/}"
              ;;
            @executable_path/*)
              base="$MACOS/${base#@executable_path/}"
              ;;
          esac
          local candidate="$base/$sub"
          if [[ -f "$candidate" ]]; then
            printf "%s" "$candidate"
            return 0
          fi
        fi
      done < <(otool -l "$loader" | awk '/LC_RPATH/{getline; getline; print "path " $2}')
      return 1
      ;;
    *)
      printf "%s" "$dep"
      return 0
      ;;
  esac
}

PROCESSED_BINARIES=""

already_processed() {
  local path="$1"
  printf "%b" "$PROCESSED_BINARIES" | grep -Fx "$path" >/dev/null 2>&1
}

mark_processed() {
  local path="$1"
  PROCESSED_BINARIES="${PROCESSED_BINARIES}${path}\n"
}

bundle_binary() {
  local target="$1"
  if already_processed "$target"; then
    return
  fi
  mark_processed "$target"
  while read -r dep _; do
    [[ -z "$dep" ]] && continue
    [[ "$dep" == "$target" ]] && continue
    case "$dep" in
      /System/*|/usr/lib/*)
        continue
        ;;
      @executable_path/../Frameworks/*)
        continue
        ;;
    esac
    local resolved
    if ! resolved="$(resolve_path "$dep" "$target")"; then
      continue
    fi
    if [[ -z "$resolved" || ! -f "$resolved" ]]; then
      continue
    fi
    local base
    base="$(basename "$resolved")"
    if [[ -z "$ECCODES_PREFIX_GUESSED" && "$base" == libeccodes* ]]; then
      ECCODES_PREFIX_GUESSED="$(cd "$(dirname "$resolved")/.." && pwd)"
      echo "Detected ecCodes prefix: $ECCODES_PREFIX_GUESSED"
    fi
    local dest="$FRAMEWORKS/$base"
    if [[ ! -f "$dest" ]]; then
      cp "$resolved" "$dest"
      chmod 755 "$dest"
      install_name_tool -id "@executable_path/../Frameworks/$base" "$dest"
    fi
    install_name_tool -change "$dep" "@executable_path/../Frameworks/$base" "$target"
    bundle_binary "$dest"
  done < <(otool -L "$target" | tail -n +2 | awk '{print $1}')
}

bundle_binary "$MACOS/gribview"

copy_tree() {
  local src="$1"
  local dst="$2"
  mkdir -p "$dst"
  if command -v rsync >/dev/null 2>&1; then
    rsync -a "$src/" "$dst/"
  else
    ditto "$src" "$dst"
  fi
}

# bundle ecCodes definitions/samples ------------------------
ECCODES_PREFIX="${ECCODES_PREFIX:-}"
if [[ -z "$ECCODES_PREFIX" ]]; then
  if pkg-config --exists eccodes 2>/dev/null; then
    ECCODES_PREFIX="$(pkg-config --variable=prefix eccodes 2>/dev/null)"
  fi
fi
if [[ -z "$ECCODES_PREFIX" ]] && command -v brew >/dev/null 2>&1; then
  ECCODES_PREFIX="$(brew --prefix eccodes 2>/dev/null || true)"
fi
if [[ -z "$ECCODES_PREFIX" && -n "$ECCODES_PREFIX_GUESSED" ]]; then
  ECCODES_PREFIX="$ECCODES_PREFIX_GUESSED"
elif [[ -n "$ECCODES_PREFIX" && -n "$ECCODES_PREFIX_GUESSED" && ! -d "$ECCODES_PREFIX/share/eccodes/definitions" ]]; then
  ECCODES_PREFIX="$ECCODES_PREFIX_GUESSED"
fi
if [[ -n "$ECCODES_PREFIX" ]]; then
  echo "Using ecCodes prefix: $ECCODES_PREFIX"
fi

ECCODES_DEF_SRC=""
ECCODES_SAMPLES_SRC=""

if [[ -n "${ECCODES_DEFINITION_PATH:-}" ]]; then
  ECCODES_DEF_SRC="${ECCODES_DEFINITION_PATH%%:*}"
fi
if [[ -n "${ECCODES_SAMPLES_PATH:-}" ]]; then
  ECCODES_SAMPLES_SRC="${ECCODES_SAMPLES_PATH%%:*}"
fi

if [[ -z "$ECCODES_DEF_SRC" && -n "$ECCODES_PREFIX" && -d "$ECCODES_PREFIX/share/eccodes/definitions" ]]; then
  ECCODES_DEF_SRC="$ECCODES_PREFIX/share/eccodes/definitions"
fi
if [[ -z "$ECCODES_SAMPLES_SRC" && -n "$ECCODES_PREFIX" && -d "$ECCODES_PREFIX/share/eccodes/samples" ]]; then
  ECCODES_SAMPLES_SRC="$ECCODES_PREFIX/share/eccodes/samples"
fi

# Additional fallbacks (repo-relative or sibling projects)
CANDIDATE_BASES=()
if [[ -n "$ECCODES_PREFIX" ]]; then
  CANDIDATE_BASES+=("$ECCODES_PREFIX")
fi
CANDIDATE_BASES+=("$ROOT/external/eccodes")

LEGACY_PATHS=(
  "/Users/filippi_j/soft/meso-nh-fire-code/src/LIB/eccodes-2.25.0-Source"
  "/Users/filippi_j/soft/meso-nh-fire-code/src/LIB/eccodes-2.25.0"
  "/Users/filippi_j/soft/share/eccodes"
)

for legacy in "${LEGACY_PATHS[@]}"; do
  if [[ -d "$legacy" ]]; then
    CANDIDATE_BASES+=("$legacy")
  fi
done

seen=""
for base in "${CANDIDATE_BASES[@]}"; do
  [[ -d "$base" ]] || continue
  case "$seen" in *"|$base|"*) continue ;; esac
  seen="${seen}|$base|"
  if [[ -z "$ECCODES_DEF_SRC" && -d "$base/definitions" ]]; then
    ECCODES_DEF_SRC="$base/definitions"
  fi
  if [[ -z "$ECCODES_SAMPLES_SRC" && -d "$base/samples" ]]; then
    ECCODES_SAMPLES_SRC="$base/samples"
  fi
done

if [[ -n "$ECCODES_DEF_SRC" ]]; then
  echo "Copying ecCodes definitions from $ECCODES_DEF_SRC"
  copy_tree "$ECCODES_DEF_SRC" "$ECCODES_RES/definitions"
fi
if [[ -n "$ECCODES_SAMPLES_SRC" ]]; then
  echo "Copying ecCodes samples from $ECCODES_SAMPLES_SRC"
  copy_tree "$ECCODES_SAMPLES_SRC" "$ECCODES_RES/samples"
fi

if command -v codesign >/dev/null 2>&1; then
  codesign --force --deep --sign - "$APP_DIR"
fi

rm -f "$DMG_PATH"
hdiutil create -quiet -fs HFS+ -volname "$APP_NAME" -srcfolder "$APP_DIR" "$DMG_PATH"

echo "Packaged app:"
echo "  $APP_DIR"
echo "Disk image:"
echo "  $DMG_PATH"
