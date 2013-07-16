#
# Homebrew formula file for libslax
# https://github.com/mxcl/homebrew
#

require 'formula'

class Libslax < Formula
  homepage 'https://github.com/Juniper/@PACKAGE-NAME@'
  url 'https://github.com/Juniper/libslax/releases/0.16.15/libslax-0.16.15.tar.gz'
  sha1 'ee849585d7af7fd6515862f8f3bf30338ad073ab'

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
