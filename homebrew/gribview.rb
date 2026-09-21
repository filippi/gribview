class Gribview < Formula
  desc "Desktop GRIB weather-data viewer"
  homepage "https://github.com/filippi/gribview"
  url "https://github.com/filippi/gribview/releases/download/v1.4.0/gribview-1.4.0.tar.gz"
  sha256 "2ad045f3b0a103c2c854efc8111c0e79cae9eaa81deec58a7e5018897d24e033"
  license "Apache-2.0"

  bottle do
    root_url "https://github.com/filippi/gribview/releases/download/v1.4.0"
    sha256 cellar: :any, arm64_sequoia: "a5b9f6deac9452c725c4df6d2444efbb9bb04a31af38665cb4506668b067f0e2"
  end

  depends_on "cmake" => :build
  depends_on "pkgconf" => :build
  depends_on "eccodes"
  depends_on "glew"
  depends_on "sdl2-compat"
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
    assert_match "gribview #{version}", shell_output("#{bin}/gribview --version")
    assert_match "messages=2", shell_output("#{bin}/gribview --check #{pkgshare}/sample.grib")
  end
end
