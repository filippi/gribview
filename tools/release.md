# gribview release guide

Follow these steps to cut a new gribview release (source, packages, bottle, docs).

## 0) Prereqs
- Working tree clean and on main branch.
- `git` and GitHub access.
- Homebrew tap exists at `filippi/homebrew-gribview`.

## 1) Bump version and package source
1. Update `CMakeLists.txt` version (already 1.2 if you followed previous steps).
2. (Optional) Regenerate icons with `tools/make_icon.sh` if needed.
3. Create the source archive and SHA:
   ```bash
   ./tools/package_homebrew.sh
   ```
   Note the generated `dist/gribview-<version>.tar.gz` and SHA256.

## 2) Commit changes
```bash
git add CMakeLists.txt homebrew/gribview.rb docs/index.html README.md .github/workflows/build.yml
git add tools/release.md  # this file
git commit -m "Release v<version>"
git push
```

## 3) Tag and push
```bash
git tag v<version>
git push origin v<version>
```

## 4) Trigger CI / packaging
- The GitHub Actions workflow runs on the tag (or use “Run workflow” via workflow_dispatch).
- It builds:
  - macOS DMG (existing script)
  - Linux `.deb` and portable tar (`gribview-<version>-Linux-portable.tar.gz`)
  - Windows zip with DLLs (`gribview-<version>-Windows-AMD64.zip`)
  - Source tarball (already built)
- Artifacts are uploaded as `packages-*` per runner.

## 5) Collect artifacts
From the workflow run artifacts:
- macOS: `Gribview.dmg`
- Linux: `gribview-<version>-Linux-x86_64.deb`, `gribview-<version>-Linux-portable.tar.gz`
- Windows: `gribview-<version>-Windows-AMD64.zip`
- Source: `gribview-<version>.tar.gz`
- Homebrew bottle: build separately from tap workflow (see step 7).

## 6) Publish GitHub release
- Create/edit the `v<version>` release in `filippi/gribview`.
- Attach all artifacts above.

## 7) Homebrew bottle
1. Run the bottle workflow in the tap repo or locally:
   ```bash
   brew uninstall --force gribview || true
   brew install --build-bottle filippi/gribview/gribview
   brew bottle --json filippi/gribview/gribview
   ```
   Or trigger the tap’s `bottle.yml` workflow to generate the JSON and tarball.
2. Upload the `gribview--<version>.arm64_sequoia.bottle.tar.gz` to the `v<version>` release.
3. Merge the bottle block into the tap formula:
   ```bash
   brew bottle --merge --write --no-commit path/to/gribview--<version>.arm64_sequoia.bottle.json
   git add Formula/gribview.rb
   git commit -m "gribview: add <version> arm64_sequoia bottle"
   git push
   ```

## 8) Verify
```bash
brew untap filippi/gribview || true
brew tap filippi/gribview
brew install --force-bottle filippi/gribview/gribview
gribview --help
```

## 9) Docs
- Ensure `docs/index.html` points to the new versioned artifact URLs.
- Push docs if hosted (e.g., GitHub Pages). 
