#!/usr/bin/env python3
"""Generate release formula/cask from actual source and DMG checksums."""
import argparse
import hashlib
from pathlib import Path


def prepare(version, source, output, dmg=None):
    base = f"https://github.com/filippi/gribview/releases/download/v{version}"
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    formula = f'''class Gribview < Formula
  desc "Desktop GRIB weather-data viewer"
  homepage "https://github.com/filippi/gribview"
  url "{base}/{source.name}"
  sha256 "{digest(source)}"
  license "Apache-2.0"
  depends_on "cmake" => :build
  depends_on "pkgconf" => :build
  depends_on "eccodes"
  depends_on "glew"
  depends_on "sdl2"
  depends_on "libpng"
  on_macos do
    depends_on arch: :arm64
  end

  def install
    system "cmake", "-S", ".", "-B", "build", *std_cmake_args
    system "cmake", "--build", "build"
    system "ctest", "--test-dir", "build", "--output-on-failure"
    system "cmake", "--install", "build"
  end

  test do
    assert_match "gribview #{{version}}", shell_output("#{{bin}}/gribview --version")
    assert_match "messages=2", shell_output("#{{bin}}/gribview --check #{{pkgshare}}/sample.grib")
  end
end
'''
    (output / "Formula").mkdir(parents=True, exist_ok=True)
    (output / "Formula/gribview.rb").write_text(formula)
    if dmg:
        (output / "Casks").mkdir(parents=True, exist_ok=True)
        (output / "Casks/gribview-app.rb").write_text(f'''cask "gribview-app" do
  version "{version}"
  sha256 "{digest(dmg)}"
  url "{base}/{dmg.name}"
  name "Gribview"
  desc "Desktop GRIB weather-data viewer"
  homepage "https://github.com/filippi/gribview"
  depends_on arch: :arm64
  depends_on macos: ">= :sequoia"
  app "Gribview.app"
end
''')


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--dmg", type=Path)
    args = parser.parse_args()
    prepare(args.version, args.source, args.output, args.dmg)
