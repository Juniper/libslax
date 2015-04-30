#!/bin/bash

#
# usage: cygwin.sh
#
# This is a somewhat hacky script to generate the necessary cygwin
# tarball to distribute libslax.
#
# It basically will run autoreconf, configure (with appropriate arguments),
# build juise and tar it up.  It will be up to you to rename it and upload
# it to the proper location.
#
# It will temporarily create a 'build' directory under this directory
#

CWD=`pwd`
BUILDDIR="$CWD/build"
STAGEDIR="$BUILDDIR/stage"
VERSION=`grep "PACKAGE_VERSION='" ../../configure | cut -d "'" -f 2`

mkdir -p ${STAGEDIR}
cd ../..
autoreconf -f -i
cd $BUILDDIR
../../../configure
make install DESTDIR=$STAGEDIR
cd $STAGEDIR && tar -cjf ../../libslax-$VERSION.tar.bz2 *
cd $STAGEDIR
echo "libslax-$VERSION.tar.bz2 successfully created."
