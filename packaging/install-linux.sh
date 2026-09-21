#!/bin/sh
set -eu
SOURCE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DEST="${XDG_DATA_HOME:-$HOME/.local/share}/gribview"
mkdir -p "$DEST" "$HOME/.local/bin" "${XDG_DATA_HOME:-$HOME/.local/share}/applications"
if [ "$SOURCE" != "$DEST" ]; then
  cp -R "$SOURCE/." "$DEST/"
fi
ln -sf "$DEST/gribview" "$HOME/.local/bin/gribview"
DESKTOP="${XDG_DATA_HOME:-$HOME/.local/share}/applications/gribview.desktop"
python3 - "$DEST" "$DESKTOP" <<'PY'
import pathlib, sys
root, desktop = map(pathlib.Path, sys.argv[1:])
def quote(value):
    return '"' + str(value).replace('\\', '\\\\').replace('"', '\\"').replace('`', '\\`').replace('$', '\\$').replace('%', '%%') + '"'
desktop.write_text('[Desktop Entry]\nType=Application\nName=Gribview\nExec=' + quote(root / 'gribview') + ' %F\nIcon=' + str(root / 'share/gribview/icon.png') + '\nTerminal=false\nCategories=Science;DataVisualization;\nMimeType=application/x-grib;\n')
PY
echo "Installed Gribview. Open it from your applications menu."
