#!/bin/bash

#
# usage: debian.sh
#
# This is a somewhat hacky script to use debian tools to build a valid .deb
# package file for libslax.
#
# You will need to have the following packages (and their prereqs) installed to
# run this:
#
# build-essential autoconf automake autotools-dev dh-make debhelper devscripts
# fakeroot xutils lintian pbuilder dpkg-dev libtool libxml2-dev libxslt1-dev
# libcurl4-gnutls-dev bison
#
# Please make sure the host system you are compiling on is the base line
# version of debian/ubuntu you want to support.  If you do not, you will likely
# run into glibc errors when attempting to install the built .deb files.
#
# MAKE SURE YOU HAVE UPDATED `changelog` FILE TO INCLUDE THE MOST RECENT
# VERSION AND CHANGES!
#

cd ../..
autoreconf -f -i

# dpkg-buildpackage requires the debian directory here
ln -s packaging/debian debian
dpkg-buildpackage -us -uc -rfakeroot > debian/build.log

# remove all the files dpkg-buildpackage leaves around
rm -rf debian/files debian/tmp debian/libslax debian/libslax-dev debian/libslax*debhelper* debian/libslax*substvars debian/build.log

# clean up our symlink
rm debian
cd packaging/debian
mkdir -p output

# dpkg-buildpackage doesn't support output directory argument
mv ../../../libslax_* output
mv ../../../libslax-dev_* output

echo "-----------------------------------------------------------------"
echo ".deb (and related files) have been created in 'output' directory."
echo ""
