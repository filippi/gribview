#!/usr/bin/env bash
set -euo pipefail
# Run inside a fresh runtime-only container with /src and /out mounted.
apt-get update -qq
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
  python3 python3-venv python3-pil xvfb xauth libgl1-mesa-dri libglx-mesa0 libopengl0 squashfs-tools >/tmp/apt.log 2>&1
export LIBGL_ALWAYS_SOFTWARE=1
mkdir -p '/tmp/relocated app'
tar -xzf /out/gribview-*-linux-*.tar.gz -C '/tmp/relocated app'
RUNTIME=(/tmp/relocated\ app/gribview-*)
xvfb-run -a python3 /src/tools/test_artifact.py "${RUNTIME[0]}/bin/gribview" /src/docs/sample.grib --screenshot /tmp/portable.png
"${RUNTIME[0]}/install.sh"
"$HOME/.local/bin/gribview" --check /src/docs/sample.grib
python3 -m venv /tmp/venv
/tmp/venv/bin/pip install --no-index /out/*.whl
/tmp/venv/bin/gribview --check /src/docs/sample.grib
xvfb-run -a /tmp/venv/bin/gribview --smoke-test /src/docs/sample.grib
if compgen -G '/out/*.AppImage' >/dev/null; then
  mkdir /tmp/appimage
  python3 /src/tools/extract_appimage.py /out/*.AppImage /tmp/appimage/squashfs-root >/dev/null
  xvfb-run -a python3 /src/tools/test_artifact.py /tmp/appimage/squashfs-root/bin/gribview /src/docs/sample.grib --screenshot /tmp/appimage.png
fi
DEBIAN_FRONTEND=noninteractive apt-get install -y /out/*.deb >/tmp/apt-deb.log 2>&1 || { cat /tmp/apt-deb.log; exit 1; }
xvfb-run -a python3 /src/tools/test_artifact.py /usr/bin/gribview /src/docs/sample.grib --screenshot /tmp/deb.png
