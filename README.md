# gribview

`gribview` is a lightweight cross-platform GRIB viewer. Built on ECMWF ecCodes and OpenGL (SDL2/GLEW), it lets you:
- Open GRIB files at full speed and browse messages instantly.
- Reorganize/sort messages to see what’s inside at a glance.
- Filter out unwanted messages to make GRIB archives more compact.
- Select point values, track them over time, and export CSV time series.
- Render fields, tweak colour maps, export PNGs, and dig into full metadata.

<video controls playsinline loop muted autoplay width="720" poster="docs/screenshot.jpg">
  <source src="docs/screenview.mp4" type="video/mp4">
</video>

## Quick install (binaries)
- **macOS (Homebrew):** `brew tap filippi/gribview && brew install gribview`
- **macOS (DMG):** download `Gribview.dmg` from the latest release, drag `Gribview.app` to `Applications`, then allow under *System Settings → Privacy & Security → Open Anyway* (unsigned).
- **Windows:** download `gribview-<version>-Windows-AMD64.zip`, unzip, run `gribview.exe`.
- **Linux (Debian/Ubuntu):** download `gribview-<version>-Linux-x86_64.deb` and run `sudo apt install ./gribview-<version>-Linux-x86_64.deb` (pulls `libeccodes`, `libsdl2`, `libglew`). Other distros: use the tarball and install those libs from your package manager.
- **Source:** `gribview-<version>.tar.gz` is attached to each release for packagers or manual builds.

All artifacts live under <https://github.com/filippi/gribview/releases>. A sample GRIB (`docs/sample.grib`) is included for quick testing.

## What it does
- Open one or more GRIB files and browse messages in a sortable, filterable table.
- Inspect GRIB keys/metadata; view fields with interactive pan/zoom and switchable colour maps.
- Extract CSV time series or point values for arbitrary locations.
- Multi-select rows, export only those messages to a new GRIB, and save PNG snapshots.
- Drag-and-drop GRIB files or use `File → Open…` with shortcuts (`Cmd/Ctrl + O`, etc.).

## Usage
Launch gribview and drop GRIB/GRIB2 files onto the window, or start with:
```bash
gribview file1.grib file2.grib2
```
Pick messages from the table, tweak colour maps and scaling on the left, explore the canvas, export CSV time series/points, or “Save selection” to write only selected messages back to disk.

## Build from source
Install dependencies (`cmake`, `eccodes`, `sdl2`, `glew`, a C++17 compiler) via your package manager, then:
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
The binary ends up in `build/bin/gribview` (or `build/bin/Release/gribview.exe` on Windows). See `docs/packaging.md` if you need to regenerate installers.

## Building

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The executable is written to `build/bin/gribview` (or `build/bin/Release/gribview.exe` on Windows). `build.sh` and `cmake-build.sh` wrap those two commands if you prefer a scriptable workflow.

### macOS
```bash
brew install cmake eccodes sdl2 glew
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### Ubuntu / Debian
```bash
sudo apt update
sudo apt install build-essential cmake ninja-build libeccodes-dev libsdl2-dev libglew-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Windows (Visual Studio + vcpkg or micromamba)
Option 1 – [vcpkg](https://github.com/microsoft/vcpkg):
```powershell
vcpkg install eccodes glew sdl2
cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
```

Option 2 – [micromamba](https://github.com/mamba-org/micromamba) (same command works on macOS/Linux too):
```powershell
micromamba create -n gribview cmake ninja eccodes glew sdl2
micromamba activate gribview
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## Running gribview
```bash
./build/bin/gribview file1.grib file2.grib2
```

Launching without arguments is fine as well—use the menu bar (`File → Open…`) or the `Cmd/Ctrl + O` shortcut to browse for one or multiple GRIB files afterwards. Multi-select rows with Shift/Cmd/Ctrl, then click “Save selection” to export only that subset.

Key UI areas:
- **Left panel**: select colour map, auto-fit or lock min/max, export PNGs and trigger rescales.
- **Message table**: shows selected GRIB keys; click headers to sort, right-click to choose additional keys, and multi-select rows to compare related fields.
- **Inspector**: press *More info* on a row to load every available GRIB key/value pair (excluding large arrays).
- **Canvas**: mouse wheel to zoom, drag with the left button to pan. Hold space to temporarily switch to pan mode if multi-selecting rows.
- **Export**: choose `Save selection` to write currently selected messages back to a new `.grib` file.

## Development workflow
- `cmake --build build --target install` installs the binary under `build/bin`.
- Run `ctest --output-on-failure` from the build directory to confirm the build completes (there are no unit tests yet, but this keeps CI paths exercised).
- Code style is standard clang-format defaults from ImGui/STB; keep additions simple and comment only when non-obvious logic appears.

## Packaging (macOS DMG)
The repository bundles a helper script that constructs a standalone `.app` bundle and `.dmg` image so testers can run gribview without compiling dependencies:

```bash
./build.sh                       # produce build/bin/gribview
./tools/make_icon.sh             # optional: regenerate icons from tools/icon.png (requires ImageMagick); script also runs automatically if possible
./tools/package_macos.sh         # creates dist/Gribview.app and dist/Gribview.dmg
open dist/Gribview.dmg
```

The script copies SDL2, GLEW, ecCodes, **including its `definitions/` and `samples/` directories**, into the bundle so no external runtime is required, and rewrites the loader paths. The resulting DMG can be published on GitHub Releases or shared directly; recipients just drag the `Gribview.app` onto their system.
An ad-hoc `codesign --force --deep -` pass is applied automatically so macOS accepts the relocated dylibs; notarisation is still up to you if you plan to distribute broadly.

## Packaging (Homebrew)
`gribview` can also ship as a Homebrew formula. Use `tools/release_homebrew.sh <version>` to tag a release (you need to match `CMakeLists.txt` version), pack the sources into `dist/gribview-<version>.tar.gz`, and rewrite `homebrew/gribview.rb` so it references `https://github.com/filippi/gribview/releases/download/v<version>/gribview-<version>.tar.gz` with the generated SHA256. The script emits the archive path and digest, then you can:

1. `git add dist/gribview-<version>.tar.gz homebrew/gribview.rb`
2. `git commit -m "Release v<version>"`
3. `git push --follow-tags`
4. Create a GitHub release named `v<version>` and attach `dist/gribview-<version>.tar.gz`.

The formula depends on `cmake`, `pkg-config`, `eccodes`, `glew`, and `sdl2`, and uses `cmake --install` to drop the binary under Homebrew prefixes.

Homebrew requires the formula to live in a tap, so installing from `./homebrew/gribview.rb` directly shows `Error: Homebrew requires formulae to be in a tap`. Instead, either create a tap manually or run `tools/homebrew_tap_install.sh`:

```bash
# one-time tap setup (example tap name)
brew tap-new filippi/gribview
# copy the formula into the tap and install (the helper does this for you as well)
mkdir -p "$(brew --repo filippi/gribview)/Formula"
cp homebrew/gribview.rb "$(brew --repo filippi/gribview)/Formula/gribview.rb"
brew install --build-from-source filippi/gribview/gribview
```

Or simply:

```bash
tools/homebrew_tap_install.sh
```

After the tap installs the release formula, you can run `brew reinstall --build-from-source filippi/gribview/gribview` to rebuild against newer dependencies.

## Continuous integration
CI lives in `.github/workflows/build.yml` and builds the project for Linux, macOS and Windows on every push or pull request. Each job:
1. Provisions dependencies with Micromamba on the respective runner.
2. Configures CMake with Ninja in Release mode.
3. Builds the executable and publishes the binaries as workflow artifacts so you can grab the latest snapshot without compiling locally.

## Tutorial / video slot
Drop your forthcoming tutorial video link here when it is ready. Until then, this README acts as the quick-start reference for contributors and users.
