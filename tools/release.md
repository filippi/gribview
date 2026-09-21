# Release guide

The maintained release process, local build commands, tests and required
credentials are in [docs/packaging.md](../docs/packaging.md).

Use an immutable tag matching CMake's version. CI creates a draft release
after installed-artifact checks pass. Manual workflow runs create artifacts
only. Do not run the old local `generate_release.bash` helper; it predates
the current release workflow and is not tracked by this repository.
