class Qdl < Formula
  desc "Flash images to Qualcomm EDL devices"
  homepage "https://github.com/linux-msm/qdl"
  url "https://github.com/linux-msm/qdl/archive/refs/tags/v2.7.1.tar.gz"
  sha256 "93cd1475d4cdfed31bca2aed9279c3e4cf829e5d65921762734dc0c6d26f0221"
  license "BSD-3-Clause"

  head "https://github.com/linux-msm/qdl.git"

  depends_on "help2man" => :build
  depends_on "meson" => :build
  depends_on "ninja" => :build
  depends_on "pkgconf" => :build
  depends_on "libusb"
  depends_on "libxml2"
  depends_on "libzip"

  def install
    system "meson", "setup", "build",
           "-DVERSION=#{version}",
           *std_meson_args
    system "meson", "compile", "-C", "build"
    system "meson", "compile", "manpages", "-C", "build"
    system "meson", "install", "-C", "build"
  end

  test do
    system bin/"qdl", "--version"
  end
end
