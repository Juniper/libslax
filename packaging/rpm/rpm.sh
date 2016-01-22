#!/bin/bash

#
# usage: rpm.sh
#
# This is a somewhat hacky script to use rpmbuild to build a valid .rpm
# package file for libslax.
#
# Note this will make rpmbuild directories under ~/rpmbuild
#
# See the README.md file in this directory on steps how to use this script.
#

PKG_DIR=`pwd`

mkdir -p ~/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}

cd ../..
autoreconf -f -i

cd $PKG_DIR
mkdir -p build
cd build

../../../configure
make
make dist
mv libslax*tar.gz ~/rpmbuild/SOURCES
rpmbuild -ba packaging/rpm/libslax.spec

cd $PKG_DIR
rm -rf build

echo "------------------------------------------------------------"
echo ".rpm has been created in '~/rpmbuild/RPMS/<arch>' directory."
echo "-"
