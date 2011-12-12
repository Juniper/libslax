#
# $Id$
#
# Copyright 2011, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

include ${top_srcdir}/warnings.mk

ext_curlincdir = ${includedir}/libslax

EXTRA_DIST = curl.txt

AM_CFLAGS = \
    -DLIBSLAX_XMLSOFT_NEED_PRIVATE \
    -DCURL_DISABLE_TYPECHECK \
    -I${top_srcdir} \
    ${LIBSLAX_CFLAGS} \
    ${LIBXSLT_CFLAGS} \
    ${LIBXML_CFLAGS} \
    ${LIBCURL_CFLAGS} \
    ${WARNINGS}

AM_CFLAGS += \
 -DSLAX_EXTDIR=\"${SLAX_EXTDIR}\"

lib_LTLIBRARIES = libext_curl.la
LIBS = \
    ${LIBXSLT_LIBS} \
    -lexslt \
    ${LIBXML_LIBS} \
    ${LIBCURL_LIBS} \
    -L${SLAX_LIBDIR} -lslax

libext_curl_la_SOURCES = \
    ext_curl.c

noinst_HEADERS = \
    ext_curl.h

libdir = ${SLAX_EXTDIR}

UGLY_NAME = http%3A%2F%2Fxml.juniper.net%2Fextensions%2Fcurl.ext

install-exec-hook:
	@DLNAME=`sh -c '. ${libdir}/libext_curl.la ; echo $$dlname'`; \
                    echo Install link $$DLNAME "->" ${UGLY_NAME} "..." \
                    && cd ${SLAX_EXTDIR} \
	            && rm -f ${UGLY_NAME} \
		    && ${LN_S} $$DLNAME ${UGLY_NAME}
