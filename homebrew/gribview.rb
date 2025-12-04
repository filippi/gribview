class Gribview < Formula
  desc "Lightweight cross-platform GRIB file visualiser and filter"
  homepage "https://github.com/filippi/gribview"
  url "https://github.com/filippi/gribview/releases/download/v1.0/gribview-1.0.tar.gz"
  sha256 "4cc8e8e9b2d407efe8957de9ab705e18839d459a1596a136476094bdce7ca6cb"
  license "Apache-2.0"

  bottle do
    root_url "https://github.com/filippi/gribview/releases/download/v1.0"
    sha256 cellar: :any, arm64_sequoia: "4d466672cf099dc6a0156aa8a30ce1893083cb64e15c9df0e92fc938c87cfb00"
  end

  depends_on "cmake" => :build
  depends_on "pkg-config" => :build
  depends_on "eccodes"
  depends_on "glew"
  depends_on "sdl2"

  def install
    system "cmake", "-S", ".", "-B", "build",
           "-DCMAKE_BUILD_TYPE=Release",
           "-DCMAKE_INSTALL_PREFIX=#{prefix}"
    system "cmake", "--build", "build"
    system "cmake", "--install", "build"
  end

  test do
    assert_predicate bin/"gribview", :exist?
  end
end
