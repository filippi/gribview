# Packaging & release guide

This document describes how gribview's release automation works and how to
produce the installable packages that are published on GitHub Releases,
Homebrew, and Debian/Ubuntu through `.deb` artifacts.

## Overview

- Every push or pull request is built on Linux, macOS, and Windows by
  `.github/workflows/build.yml` to keep the build healthy.
- When you push a Git tag that matches `v*`, the workflow rebuilds on all
  platforms, packages the resulting binaries with CPack, and uploads the
  artifacts to the GitHub release that belongs to the tag.
- The Linux job also runs `tools/package_homebrew.sh` so a fresh source
  archive is ready for Homebrew taps.
- Manual macOS app bundles (`tools/package_macos.sh`) remain available when a
  standalone `.app`/`.dmg` is required.

## Creating a release

1. Update `project(gribview VERSION …)` in `CMakeLists.txt` and ensure all
   platform fixes are merged.
2. Run `tools/release_homebrew.sh <version>` locally. The helper script:
   - Packages the repository sources into `dist/gribview-<version>.tar.gz`.
   - Rewrites `homebrew/gribview.rb` so the formula references the new archive
     URL and SHA256.
   - Creates/updates the `v<version>` Git tag.
3. `git add dist/gribview-<version>.tar.gz homebrew/gribview.rb` and commit the
   release bump.
4. `git push --follow-tags` so GitHub picks up the new tag.
5. Wait for the "Release packages" workflow jobs to finish. They attach the
   following files to the `v<version>` release automatically:
   - `gribview-<version>-Linux.tar.gz`
   - `gribview-<version>-Linux.deb`
   - `gribview-<version>-Darwin.tar.gz`
   - `gribview-<version>-windows.zip`
   - `gribview-<version>.tar.gz` (source archive for Homebrew taps)
6. Edit the drafted GitHub release description if necessary and publish it.

## Installing prebuilt packages

### Linux (Debian/Ubuntu)

1. Download `gribview-<version>-Linux.deb` from the release page.
2. Install with `sudo apt install ./gribview-<version>-Linux.deb`. Apt resolves
   the runtime dependencies listed in the package metadata automatically.

### Linux (portable tarball)

Download `gribview-<version>-Linux.tar.gz` and unpack it anywhere:

```bash
tar -xvf gribview-<version>-Linux.tar.gz
./gribview-<version>-Linux/bin/gribview myfile.grib
```

### macOS

Two options are supported:

- **Homebrew:** follow `tools/homebrew_tap_install.sh` or tap
  `filippi/gribview` manually, then run
  `brew install --build-from-source filippi/gribview/gribview`.
- **Standalone app/DMG:** run `tools/package_macos.sh` locally if you need a
  drag-and-drop bundle. The script copies SDL2, GLEW, and ecCodes into the app
  so the DMG can be shared with testers.

### Windows

Download `gribview-<version>-windows.zip` from the release, extract it, and run
`gribview.exe`. The ZIP bundles the `SDL2`, `GLEW`, and `eccodes` DLLs produced
by the CI job so no additional runtime is required.

## Updating the Homebrew tap

1. Ensure `tools/release_homebrew.sh` has been run for the new version.
2. In the tap repository (`filippi/homebrew-gribview` or equivalent), copy the
   updated `homebrew/gribview.rb` into `Formula/`.
3. Commit and push the tap so `brew update` picks up the new formula.

With these steps every release publishes Homebrew-friendly source archives,
Linux `.deb` installers, cross-platform tarballs/ZIPs, and documentation for
users who prefer manual packaging.
