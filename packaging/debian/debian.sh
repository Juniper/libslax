#!/bin/bash

#
# usage: debian.sh
#
# This is a somewhat hacky script to use debian tools to build a valid .deb
# package file for libslax.
#
# See the README file in this directory on steps how to use this script.
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
