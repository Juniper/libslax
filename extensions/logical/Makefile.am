#
# $Id$
#

include ${top_srcdir}/warnings.mk

logicalincdir = ${includedir}/libslax

AM_CFLAGS = \
    -I${top_srcdir} \
    -I${top_srcdir}/libslax \
    ${LIBXSLT_CFLAGS} \
    ${LIBXML_CFLAG} \
    ${WARNINGS}

AM_CFLAGS += \
 -DSLAX_EXTDIR=\"${SLAX_EXTDIR}\"

lib_LTLIBRARIES = liblogical.la
liblogical_la_LIBADD = ${LIBXSLT_LIBS} -lexslt ${LIBXML_LIBS}

liblogical_la_SOURCES = \
    ext_logical.c
