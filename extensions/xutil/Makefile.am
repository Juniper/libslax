#
# $Id$
#
# Copyright 2012, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

if SLAX_WARNINGS_HIGH
SLAX_WARNINGS = HIGH
endif
include ${top_srcdir}/warnings.mk

ext_curlincdir = ${includedir}/libslax

AM_CFLAGS = \
    -DLIBSLAX_XMLSOFT_NEED_PRIVATE \
    -I${top_srcdir} \
    -I${top_srcdir}/libslax \
    ${LIBSLAX_CFLAGS} \
    ${LIBXSLT_CFLAGS} \
    ${LIBXML_CFLAGS} \
    ${WARNINGS}

AM_CFLAGS += \
 -DSLAX_EXTDIR=\"${SLAX_EXTDIR}\"

LIBNAME = libext_xutil
pkglib_LTLIBRARIES = libext_xutil.la
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

libext_xutil_la_SOURCES = \
    ext_xutil.c

pkglibdir = ${SLAX_EXTDIR}
UGLY_NAME = xutil.prefix:http%3A%2F%2Fxml.libslax.org%2Fxutil.ext

install-exec-hook:
	@DLNAME=`sh -c '. ./libext_xutil.la ; echo $$dlname'`; \
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
