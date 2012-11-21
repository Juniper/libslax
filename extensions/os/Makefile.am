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

ext_osincdir = ${includedir}/libslax

AM_CFLAGS = \
    -DLIBSLAX_XMLSOFT_NEED_PRIVATE \
    -I${top_srcdir} \
    -I${top_srcdir}/libslax \
    ${LIBXSLT_CFLAGS} \
    ${LIBXML_CFLAG} \
    ${WARNINGS}

AM_CFLAGS += \
 -DSLAX_EXTDIR=\"${SLAX_EXTDIR}\"

lib_LTLIBRARIES = libext_os.la
LIBS = \
    ${LIBXSLT_LIBS} -lexslt ${LIBXML_LIBS} \
    -L../../libslax -L${SLAX_LIBDIR} -lslax

if HAVE_READLINE
LIBS += -L/opt/local/lib -lreadline
endif

if HAVE_LIBEDIT
LIBS += -ledit
endif
libext_os_la_SOURCES = \
    ext_os.c

libdir = ${SLAX_EXTDIR}

UGLY_NAME = http%3A%2F%2Fxml.libslax.org%2Fos.ext

install-exec-hook:
	@DLNAME=`sh -c '. ${libdir}/libext_os.la ; echo $$dlname'` \
		&& echo Install link $$DLNAME "->" ${UGLY_NAME} "..." \
		&& cd ${SLAX_EXTDIR} \
		&& chmod +w . \
		&& rm -f ${UGLY_NAME} \
		&& ${LN_S} $$DLNAME ${UGLY_NAME}
