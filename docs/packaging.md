# Packaging and release

The 1.4 release targets macOS 15+ on Apple Silicon and Linux x86_64 with
glibc 2.35+ (Ubuntu 22.04+, Debian 12+). No Intel or universal macOS build is
produced. Linux needs a graphical desktop with OpenGL 3.2. Xvfb/Mesa is used
for automated rendering checks, not as a replacement for a desktop.

## Local verification

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
./build/bin/gribview --diagnostics
./build/bin/gribview --check docs/sample.grib
./build/bin/gribview --smoke-test docs/sample.grib --screenshot build/smoke.png
```

`--check` decodes coordinates and values without creating a window. The smoke
test renders five frames, checks that a field texture was created, and exits.
`tools/test_artifact.py` adds error, relocation and optional screenshot pixel
checks (Pillow required for screenshots). Run it on the installed artifact.
The About window and `--version` identify the source revision.

## macOS DMG

```sh
python3 tools/package_macos.py --build build --dist dist/macos
```

Use a fresh output directory. The script refuses to overwrite an existing app.
It stages the executable, every non-system dylib, font, icon, sample GRIB and
ecCodes definitions/samples, fixes loader paths and signs the relocated code.
The DMG contains Gribview.app and an Applications shortcut.

Without credentials, the artifact ends in `-unsigned.dmg` and macOS may require
Privacy & Security > Open Anyway. That is suitable for local testing, but a
frictionless public download requires Developer ID signing and notarization:

```sh
export CODESIGN_IDENTITY='Developer ID Application: your identity'
export NOTARY_PROFILE='your-notarytool-keychain-profile'
python3 tools/package_macos.py --build build --dist dist/macos-signed
```

CI accepts secrets `MACOS_CERTIFICATE_BASE64`, `MACOS_CERTIFICATE_PASSWORD`,
`MACOS_KEYCHAIN_PASSWORD`, `MACOS_CODESIGN_IDENTITY`, `APPLE_ID`,
`APPLE_TEAM_ID`, and `APPLE_APP_PASSWORD`. It imports the identity into a
temporary runner keychain, notarizes the DMG, and staples the ticket.

## Linux in Docker

```sh
docker build -f packaging/Dockerfile.linux -t gribview-builder packaging
mkdir -p dist/linux
docker run --rm --init -v "$PWD:/src:ro" -v "$PWD/dist/linux:/out" \
  gribview-builder bash tools/build_linux_release.sh
docker run --rm --init -v "$PWD:/src:ro" -v "$PWD/dist/linux:/out:ro" \
  ubuntu:24.04 bash /src/tools/test_linux_clean.sh
```

On an ARM Mac, add `--platform linux/amd64` to both the image build and runs.
Use fresh output directories; build artifacts are never overwritten silently.
The builder produces a distro-linked `.deb`, a portable tarball, an AppImage
and a wheel. AppImage tooling is pinned to 1.9.1 with a verified SHA256. Its
runtime is obtained by that tooling from the upstream runtime release.

Portable packages contain the native dependency closure, ecCodes data, font,
sample and runtime license notices. Host libc and graphics drivers remain
system responsibilities. The tarball includes `gribview` and `install.sh`;
the latter installs for the current user and adds an applications-menu entry.
File dialogs use the desktop's zenity/kdialog provider. Drag-and-drop and
opening filenames from the command line work without either provider.

Clean tests run the portable app and wheel before installing the Debian
package, so distro ecCodes/SDL/GLEW cannot mask a broken portable package.
They also test an extracted AppImage without FUSE. Repeat on Ubuntu 22.04,
Ubuntu 24.04 and Debian 12. Native Wayland and hardware GPU interaction still
need a desktop release check.

## Wheels

```sh
python3 tools/bundle_runtime.py --build build --output dist/wheel-runtime
python3 -m pip wheel . --no-deps \
  --config-settings "runtime=$PWD/dist/wheel-runtime" -w dist/wheels
python3 -m venv /tmp/gribview-wheel-test
/tmp/gribview-wheel-test/bin/pip install --no-index dist/wheels/*.whl
/tmp/gribview-wheel-test/bin/gribview --check docs/sample.grib
```

The PEP 517 backend wraps a prebuilt native runtime; it does not download or
compile dependencies during user installation. Tags are `py3-none` with
`macosx_15_0_arm64` or `manylinux_2_35_x86_64`. Linux packaging audits every
ELF's glibc version requirements and rejects unresolved/nonbundled dependencies
outside the declared system boundary. The launcher supports Python 3.9+.

Configure the PyPI project (or pending publisher) for this repository,
`pypi.yml`, and the `pypi` environment. After publishing a tested GitHub release,
run the Publish wheels workflow with its tag. No PyPI API token is needed.
The project name must be available or owned by the maintainer. Until uploaded,
use `pip install /path/to/the.whl`; a bare `pip install gribview` is not yet a
verified public installation route.

## Homebrew and publishing

1. Commit the desired source and version. Tag that exact commit `v1.4.0` (or
   the next CMake version). Do not move an existing release tag.
2. Push the tag. Build CI tests Linux, macOS and Windows, then prepares a draft
   GitHub release with packages, wheels, source, SHA256SUMS and a tap archive.
3. Review the draft's assets and signing status, then publish it.
4. The bottle workflow builds/tests the ARM Homebrew formula and uploads its
   bottle. With `TAP_TOKEN` configured it updates `filippi/homebrew-gribview`;
   otherwise download its artifact and copy the formula into that tap. The
   generated cask is `gribview-app`, distinct from the CLI formula `gribview`.
5. Dispatch Publish wheels after configuring PyPI trusted publishing.

`tools/prepare_tap.py` derives formulas/casks from actual archive checksums.
The checked-in legacy formula remains the published 1.3 source formula until
the next release assets exist; do not point users at nonexistent 1.4 URLs.
Manual Build workflow runs only create artifacts, never publish a release.

Expected user commands once the new release and tap are published:

```sh
brew install filippi/gribview/gribview
brew install --cask filippi/gribview/gribview-app
pipx install gribview
```

No release was published by implementing these scripts. Signing credentials,
tap credentials and PyPI ownership are external setup requirements.
