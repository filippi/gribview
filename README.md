# gribview

`gribview` is a lightweight cross-platform visualiser for GRIB1/GRIB2 files. It combines ECMWF ecCodes for decoding, SDL2/OpenGL/GLEW for rendering and Dear ImGui for the UI so you can quickly inspect model fields, scrub through multiple messages, explore metadata and export snapshots without leaving your workstation. Written with an extensive use of AI Agents.

## Features
- Open one or more GRIB files and browse every message in a sortable, filterable table.
- Display field metadata (levels, parameters, dates, GRIB keys) and dig into the full key/value map through the inspector popup.
- Interactive OpenGL viewer with pan, zoom, auto-fit, manual min/max scaling and switchable colour maps defined in `src/colormap512.h`. Can be re-generated with python script.
- Multi-select rows to keep related messages together, regenerate textures on demand and save PNG captures with `stb_image_write`.
- Export a filtered set of messages to a new GRIB file directly from the UI.
- Drag-and-drop GRIB files onto the window or double-click them in Finder once the app is associated (the bundled macOS app declares `.grib/.grb/.grib2/.grb2` file types so they open directly in gribview).
- File menu with `Open…`, `Clear`, and shortcuts (`Cmd/Ctrl + O`, `Cmd/Ctrl + Shift + W`, `Cmd/Ctrl + Q`) so you can launch empty, load additional GRIBs later, or clear the workspace without restarting.

## Dependencies
| Library | Why it is needed | How to get it |
| --- | --- | --- |
| [ecCodes](https://confluence.ecmwf.int/display/ECC/ecCodes+Home) | Decoding GRIB messages. | `conda install eccodes`, `brew install eccodes`, `apt install libeccodes-dev`, etc. |
| [SDL2](https://www.libsdl.org/) | Windowing, input and GL context creation. | `conda install sdl2`, `brew install sdl2`, `apt install libsdl2-dev`, `choco install sdl2`. |
| [GLEW](http://glew.sourceforge.net/) | Modern OpenGL loader used by ImGui. | `conda install glew`, `brew install glew`, `apt install libglew-dev`, `choco install glew`. |
| [OpenGL 3.2+](https://www.opengl.org/) | Rendering backend. | Installed with the OS/toolchain. |
| [CMake ≥ 3.16](https://cmake.org/) & a C++17 compiler | Build system. | Already bundled with Xcode/Visual Studio, or install via your package manager. |
| [tinyfiledialogs](https://sourceforge.net/projects/tinyfiledialogs/) (bundled) | Native file chooser for `File → Open…`. | Included in `external/tinyfiledialogs`. |

All other third-party sources (Dear ImGui + backends, stb) live inside `external/` and `src/`.

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

## Continuous integration
CI lives in `.github/workflows/build.yml` and builds the project for Linux, macOS and Windows on every push or pull request. Each job:
1. Provisions dependencies with Micromamba on the respective runner.
2. Configures CMake with Ninja in Release mode.
3. Builds the executable and publishes the binaries as workflow artifacts so you can grab the latest snapshot without compiling locally.

## Tutorial / video slot
Drop your forthcoming tutorial video link here when it is ready. Until then, this README acts as the quick-start reference for contributors and users.
