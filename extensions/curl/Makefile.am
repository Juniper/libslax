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
include ${top_srcdir}/warnings.mk

ext_curlincdir = ${includedir}/libslax

EXTRA_DIST = curl.txt

AM_CFLAGS = \
    -DLIBSLAX_XMLSOFT_NEED_PRIVATE \
    -DCURL_DISABLE_TYPECHECK \
    -I${top_srcdir} \
    ${LIBCURL_CFLAGS} \
    ${LIBSLAX_CFLAGS} \
    ${LIBXSLT_CFLAGS} \
    ${LIBXML_CFLAGS} \
    ${LIBCURL_CFLAGS} \
    ${WARNINGS}

AM_CFLAGS += \
 -DSLAX_EXTDIR=\"${SLAX_EXTDIR}\"

pkglib_LTLIBRARIES = libext_curl.la
LIBS = \
    ${LIBXSLT_LIBS} \
    -lexslt \
    ${LIBXML_LIBS} \
    ${LIBCURL_LIBS}

LDADD = ${top_builddir}/libslax/libslax.la

if HAVE_READLINE
LIBS += -L/opt/local/lib -lreadline
endif

if HAVE_LIBEDIT
LIBS += -ledit
endif

libext_curl_la_SOURCES = \
    ext_curl.c

noinst_HEADERS = \
    ext_curl.h

pkglibdir = ${SLAX_EXTDIR}

UGLY_NAME = http%3A%2F%2Fxml.libslax.org%2Fcurl.ext

install-exec-hook:
	@DLNAME=`sh -c '. ${pkglibdir}/libext_curl.la ; echo $$dlname'`; \
		if [ x"$$DLNAME" = x ]; then echo error: emtpy dlname; fi ; \
		echo Install link $$DLNAME "->" ${UGLY_NAME} "..." ; \
		cd ${SLAX_EXTDIR} \
		&& chmod +w . \
	        && rm -f ${UGLY_NAME} \
		&& ${LN_S} $$DLNAME ${UGLY_NAME}
