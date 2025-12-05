class Gribview < Formula
  desc "Lightweight GRIB data file visualiser/filter/time series extraction"
  homepage "https://github.com/filippi/gribview"
  url "https://github.com/filippi/gribview/releases/download/v1.3/gribview-1.3.tar.gz"
  sha256 "ed87f918b9341827704431f6ce70a7854ae0a74716822f5b07fddc11f684277f"
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
