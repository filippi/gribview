class Gribview < Formula
  desc "Lightweight GRIB data file visualiser/filter/time series extraction"
  homepage "https://github.com/filippi/gribview"
  url "https://github.com/filippi/gribview/releases/download/v1.2/gribview-1.2.tar.gz"
  sha256 "f5a56bb8f67a379f0323c1c52f6baa42d2d69f1a11670972de4f144bd0114a13"
  license "Apache-2.0"

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
