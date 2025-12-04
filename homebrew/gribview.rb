class Gribview < Formula
  desc "Lightweight GRIB data file visualiser/filter/time series extraction"
  homepage "https://github.com/filippi/gribview"
  url "https://github.com/filippi/gribview/releases/download/v1.1/gribview-1.1.tar.gz"
  sha256 "1ee0499d5a0daae583ac5b89492940964eed90d421544b3d4ada4a5f86b43636"
  license "Apache-2.0"

  bottle do
    root_url "https://github.com/filippi/gribview/releases/download/v1.1"
    sha256 cellar: :any, arm64_sequoia: "1ee0499d5a0daae583ac5b89492940964eed90d421544b3d4ada4a5f86b43636"
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
