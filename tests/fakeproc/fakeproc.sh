#!/bin/sh
#
# $Id$
#

# fakeproc::
# This is a "plug compatible" version of "xsltproc" suitable for
# use as a "CHECKER" under the stock libxslt tests.

SLAXPROC=../../../slaxproc/slaxproc

script=$1
shift
input=$1
shift
base=`basename $script .xsl`

${SLAXPROC} --xslt-to-slax $base.xsl $base.slax
${SLAXPROC} --slax-to-xslt $base.slax $base.xsl2
diff -bu $base.xsl $base.xsl2

${SLAXPROC} $base.slax $input

exit 0
