#
# $Id$
#
# Copyright 2011-2012, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

if SLAX_WARNINGS_HIGH
SLAX_WARNINGS = HIGH
endif
if HAVE_GCC
GCC_WARNINGS = yes
endif
include ${top_srcdir}/warnings.mk

ext_bitincdir = ${includedir}/libslax

EXTRA_DIST = bit.txt

AM_CFLAGS = \
    -DLIBSLAX_XMLSOFT_NEED_PRIVATE \
    -I${top_builddir} \
    -I${top_srcdir} \
    -I${top_srcdir}/libslax \
    ${LIBXSLT_CFLAGS} \
    ${LIBXML_CFLAG} \
    ${WARNINGS}

AM_CFLAGS += \
 -DSLAX_EXTDIR=\"${SLAX_EXTDIR}\"

LIBNAME = libext_bit
pkglib_LTLIBRARIES = libext_bit.la
LIBS = \
    ${LIBXSLT_LIBS} \
    -lexslt \
    ${LIBXML_LIBS} \
    -L${top_builddir}/libslax -lslax

LDADD = ${top_builddir}/libslax/libslax.la

if HAVE_READLINE
LIBS += -L/opt/local/lib -lreadline
endif

if HAVE_LIBEDIT
LIBS += -ledit
endif
libext_bit_la_SOURCES = \
    ext_bit.c

pkglibdir = ${SLAX_EXTDIR}

UGLY_NAME = bit.prefix:http%3A%2F%2Fxml.libslax.org%2Fbit.ext

install-exec-hook:
	@DLNAME=`sh -c '. ./libext_bit.la ; echo $$dlname'` ; \
		if [ x"$$DLNAME" = x ]; \
                    then DLNAME=${LIBNAME}.${SLAX_LIBEXT}; fi ; \
		echo Install link $$DLNAME "->" ${UGLY_NAME} "..." ; \
		mkdir -p ${DESTDIR}${SLAX_EXTDIR} ; \
		cd ${DESTDIR}${SLAX_EXTDIR} \
		&& chmod +w . \
		&& prefix=`echo ${UGLY_NAME} | awk -F: '{ print $$1 }'` \
		&& url=`echo ${UGLY_NAME} | awk -F: '{ print $$2 }'` \
		&& rm -f $$prefix $$url \
		&& ${LN_S} $$DLNAME $$url \
		&& ${LN_S} $$url $$prefix
