#
# Homebrew formula file for libslax
# https://github.com/mxcl/homebrew
#

require 'formula'

class Libslax < Formula
  homepage 'https://github.com/Juniper/@PACKAGE-NAME@'
  url 'https://github.com/Juniper/libslax/releases/0.17.0/libslax-0.17.0.tar.gz'
  sha1 '1fded594af301994bce0d532f30cb4190845a11f'

  depends_on 'libtool' => :build

  # Need newer versions of these libraries
  if MacOS.version <= :lion
    depends_on 'libxml2'
    depends_on 'libxslt'
    depends_on 'curl'
  end

  def install
    system "./configure", "--disable-dependency-tracking",
                          "--prefix=#{prefix}"
    system "make install"
  end
end
