#
# Homebrew formula file for libslax
# https://github.com/mxcl/homebrew
#

require 'formula'

class Libslax < Formula
  homepage 'https://github.com/Juniper/@PACKAGE-NAME@'
  url 'https://github.com/Juniper/libslax/releases/0.16.13/libslax-0.16.13.tar.gz'
  sha1 'ad52781d7ce8f4b87fe8e7460a93d81ddbd21569'

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
