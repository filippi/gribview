# Portable release plan

Audit date: 2026-09-21. The findings below record the original 1.3 audit.
Implementation has since begun; current commands and supported platforms are
in [packaging.md](packaging.md). The user explicitly selected Apple Silicon
only on macOS: no Intel builds and no universal binaries.

## What was tested

| Test | Result |
| --- | --- |
| Fresh Release build of the current working tree | Passed in `/private/tmp/gribview-release-audit`; compiler warnings in bundled tinyfiledialogs |
| CTest on that build | No tests found |
| Existing macOS executable and app bundle, with `docs/sample.grib` | Processes survived 8 seconds, then were terminated; this is startup evidence only, not visual or decoding verification |
| Fresh macOS executable, with sample | Survived 6 seconds, then was terminated |
| Existing macOS executable and app bundle, with `--help` | Stayed running, printed nothing; arguments are treated as filenames |
| macOS architecture inspection | Existing executable and app bundle are arm64 only |
| Installed Homebrew 1.3 executable | Aborted: `/opt/homebrew/opt/eccodes/lib/libeccodes.dylib` missing on this machine |
| `brew test filippi/gribview/gribview` | Could not run: installed dependency set is incomplete; existing formula only tests file existence |
| Published Linux portable archive on clean Ubuntu 24.04 amd64 in Docker | Loader failure: missing GLEW, ecCodes, SDL2, PNG and OpenGL libraries |
| Published `.deb` installed with apt on Ubuntu 24.04 amd64 | Installation succeeded, startup failed: `libeccodes.so` missing despite `libeccodes0` being installed |
| Same installed `.deb` under Xvfb with software rendering | Same loader failure; no application window created |
| Published `.deb` dependency resolution on Ubuntu 22.04 amd64 | Failed: requires glibc >= 2.38, newer GLEW/libstdc++, and `libpng16-16t64` |

Linux tests used `docker run --rm --platform linux/amd64` on an Apple Silicon
host, with the repository mounted read-only. Local Linux artifact SHA256 values
match GitHub's published v1.3 digests. The containers were removed automatically;
the Ubuntu images remain cached. Docker Desktop was started for these tests.
Homebrew initialized its Ruby dependencies while attempting its test.

The fresh macOS build linked ecCodes from `/Users/filippi_j/soft/lib`, not
Homebrew. Its success therefore does not demonstrate a clean Homebrew install.
No fresh Homebrew installation, Intel Mac execution, Windows execution, visual
rendering validation, or Python wheel installation was completed in this audit.

## Release blockers found in the repository

- `src/gribview.cpp:2004`: startup suppresses stdout/stderr and returns zero
  on SDL/OpenGL failures. There is no real help, version or headless check mode.
- `CMakeLists.txt:62`: the pkg-config fallback does not request an imported
  target and then references `PkgConfig::ECCODES`, inconsistent with its
  `ECCODES_PKG` prefix. This path needs correction and an explicit configure test.
- `.github/workflows/build.yml:144`: portable Linux packaging skips libraries
  under `/lib` and `/usr/lib`, only rewrites the executable RPATH, and omits
  ecCodes definitions/samples. The actual archive contains only the executable
  and `ld-linux-x86-64.so.2`.
- Linux CI mixes Micromamba libraries with distro dependency metadata. The
  observed unversioned ecCodes dependency does not match the installed runtime.
- macOS CI has one architecture, never calls `package_macos.sh`, and its release
  upload pattern excludes DMGs. The local DMG exists through a separate path.
- `tools/package_macos.sh` hardcodes bundle version 1.0, contains personal
  fallback paths, can skip unresolved dependencies, and uses ad-hoc signing.
- The Homebrew formula has no bottle block. The bottle workflow builds only one
  runner target and does not complete publication and formula merging.
- Release scripts can force-move tags; `release_homebrew.sh` passes VERSION as
  an argument to a script that reads it from the environment. Manual workflow
  dispatch can also derive the package version from a branch name.
- `generate_release.bash` requests artifact names ending in `.zip`, but CI
  names them `packages-Linux`, etc. Several failures are ignored with `|| true`.
- README, packaging documentation, scripts and actual asset names disagree.
  The local source tarball also differs from the published tarball; the formula
  checksum does match the published asset. Local archive regeneration must not
  silently replace an existing release asset.

## Proposed release products

| Channel | Deliverable | Runtime policy |
| --- | --- | --- |
| Homebrew formula | Apple Silicon bottles; source fallback | Homebrew owns native dependencies |
| macOS direct download | Apple Silicon `.app` inside a signed, notarized DMG | Bundle non-system libraries and ecCodes data |
| Homebrew cask | Installs the same signed app | Reuse the DMG rather than rebuilding |
| Linux desktop download | AppImage and unpackable portable tar, x86_64 and aarch64 | Bundle application libraries/data; document host graphics and glibc baseline |
| Debian/Ubuntu | Distro-targeted `.deb` files with architecture and baseline in names | Link against matching distro runtime packages |
| Python installer | Platform wheels exposing the `gribview` command | Include executable, native libraries and data inside the wheel |
| Windows | Keep the existing ZIP channel | Apply the same dependency and installed-artifact checks |

There is no single executable shared by macOS and Linux. The initial release
targets macOS arm64 and Linux x86_64 with an explicit libc baseline.

## Implementation order and acceptance criteria

### 1. Make failures observable and testable

Implement `--help`, `--version`, and a headless `--check FILE` before SDL video
initialization. The check should decode values and coordinates, report message
counts and basic statistics, and fail for unreadable, empty or malformed input.
Preserve stderr and return nonzero on initialization failures. GUI launches
should present actionable failures when a terminal is unavailable.

Centralize executable-relative resource lookup for app bundles, portable trees
and wheel layouts, preserving explicit ecCodes environment overrides. Report
the selected library/data versions and paths through a diagnostics command.
Correct CMake's dependency fallback and make the selected ecCodes prefix visible.

Register tests for real decoding, missing and invalid input, plus a bounded
graphics smoke mode that renders several frames and exits. Establish expected
values from committed GRIB fixtures, including GRIB1/2 and compressed data.
Make CI reject an empty test suite.

Acceptance: all these checks execute against installed artifacts outside the
source/build directory, without developer-specific environment variables.

### 2. Repair Linux packaging and prove it in Docker

Build `.deb` packages using distro development packages in pinned distro images,
without Micromamba libraries leaking into the result. Inspect ELF NEEDED entries,
particularly the ecCodes SONAME, and derive package dependencies from the actual
linked libraries. Start with Ubuntu 22.04 and 24.04 targets; add explicitly tested
Debian targets instead of labeling an Ubuntu package as universally compatible.

Build portable artifacts against a declared oldest-supported baseline. Collect
the complete non-system dependency closure, set executable and library-relative
loader paths, include ecCodes definitions/samples and third-party notices, and
reject unresolved libraries. Do not copy a lone glibc loader into the package.
Keep the host graphics-driver boundary explicit. Generate AppImage and tarball
from the same staged runtime tree; test the extracted AppImage in Docker without
requiring FUSE.

Acceptance: fresh runtime-only Ubuntu and Debian containers can install/unpack,
decode fixtures, and render under Xvfb/Mesa with `LIBGL_ALWAYS_SOFTWARE=1`.
Assert a nonblank field screenshot and message count, not just a surviving
process. Repeat from paths containing spaces. Native Linux desktop checks must
cover X11/Wayland, dialogs and actual GPU rendering; Docker is not evidence for
those interactions. Validate both Linux architectures before advertising them.

### 3. Make Homebrew installation dependable

Retain the formula for CLI users and compile against declared Homebrew
dependencies with standard CMake arguments. Install a small fixture into
`pkgshare` and replace the existence-only test with version and decode checks.
Build and test bottles for an explicit supported architecture/OS matrix, publish
them to a stable location, and merge the generated bottle metadata into the tap.
Run linkage checks and fresh installation tests on clean runners.

Acceptance: `brew install filippi/gribview/gribview` uses a matching bottle where
available, passes `brew test` and linkage checks, and opens the fixture. Source
fallback is tested separately. Add the app cask after the signed DMG is ready.

### 4. Ship an Apple Silicon macOS application

Build arm64 with an explicit minimum macOS version. Check every bundled
Mach-O file's architecture and minimum OS, not only the main executable.

Remove personal path fallbacks, fail on unresolved bundle dependencies, derive
Info.plist versions from CMake, and test Finder opening, file associations and
drag-and-drop. Sign nested code after relocation, sign the app with Developer ID
and hardened runtime, notarize, then staple. This requires the project's Apple
Developer signing credentials; unsigned artifacts must be labeled accordingly.

Acceptance: install the downloaded app on clean Apple Silicon Macs,
without Homebrew, and decode/render the sample. Validate Gatekeeper and offline
stapling behavior. Intel and universal binaries are explicitly out of scope.

### 5. Add `pip install gribview`

Keep the viewer in C++. Add a small Python launcher and package the validated
native runtime in architecture/platform-specific wheels. The launcher resolves
paths within the installed package, forwards arguments, returns the native exit
status and preserves useful diagnostics. There is no need to introduce Python
bindings merely to launch the application.

Use `py3-none-<platform>` tags if the launcher has no Python ABI dependency;
never label native payloads `py3-none-any`. Build Linux wheels against the chosen
manylinux policy and audit/repair native dependencies, including executables.
Audit macOS wheel dylibs and signatures too. Do not assume wrapping today's
binary in a wheel fixes its portability. Check PyPI name ownership and artifact
size limits before selecting the public package name.

Acceptance: in a fresh virtual environment, installation from the built wheel
needs no compiler or Homebrew; version/decode tests pass and the GUI starts on a
supported desktop. Test `pipx` and `uv tool install` as convenient alternatives.
No first-run native downloads. Linux still needs a compatible display/graphics
stack; managed system Python installations may require a venv or tool installer.

### 6. Unify release publication

Build from one immutable tag and derive all versions from CMake, checking the
tag matches. Pin build environments and dependency versions. Reuse tested native
runtime staging across DMG, portable Linux and wheel packaging, while keeping
distro packages and Homebrew linked to their own package-manager dependencies.

Publish a draft only after artifact tests pass, with explicit asset names,
SHA256 checksums, runtime licenses and supported-platform metadata. Wire DMG,
Linux, Windows, wheels and source assets into one workflow; update the tap/cask
from the resulting checksums. Use PyPI trusted publishing. Manual workflow runs
should create test artifacts unless explicitly selecting a release tag. Remove
force-retagging and permissive error handling from mandatory release steps.

Acceptance: a clean tagged build can produce every advertised asset, and README
download links/install commands correspond exactly to those tested assets.

## References

- [Published v1.3 artifacts](https://github.com/filippi/gribview/releases/tag/v1.3)
- [Homebrew bottles](https://docs.brew.sh/Bottles)
- [Apple universal binaries](https://developer.apple.com/documentation/apple-silicon/building-a-universal-macos-binary)
- [Apple distribution signing](https://developer.apple.com/documentation/xcode/creating-distribution-signed-code-for-the-mac/)
- [AppImage runtime and baseline concepts](https://docs.appimage.org/introduction/concepts.html)
- [Python platform compatibility tags](https://packaging.python.org/en/latest/specifications/platform-compatibility-tags/)
