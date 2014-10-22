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
rm -rf debian/files debian/tmp debian/libslax0 debian/libslax0-dev debian/libslax*debhelper* debian/libslax*substvars debian/build.log debian/autoreconf.*

# clean up our symlink
rm debian
cd packaging/debian
mkdir -p output

# dpkg-buildpackage doesn't support output directory argument
mv ../../../libslax0_* output
mv ../../../libslax0-dev_* output

echo "-----------------------------------------------------------------"
echo ".deb (and related files) have been created in 'output' directory."
echo ""
