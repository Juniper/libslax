#
# $Id$
#
# Copyright 2011, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

if SLAX_WARNINGS
include ${top_srcdir}/warnings.mk
endif

ext_bitincdir = ${includedir}/libslax

EXTRA_DIST = bit.txt

AM_CFLAGS = \
    -DLIBSLAX_XMLSOFT_NEED_PRIVATE \
    -I${top_srcdir} \
    -I${top_srcdir}/libslax \
    ${LIBXSLT_CFLAGS} \
    ${LIBXML_CFLAG} \
    ${WARNINGS}

AM_CFLAGS += \
 -DSLAX_EXTDIR=\"${SLAX_EXTDIR}\"

lib_LTLIBRARIES = libext_bit.la
LIBS = \
    ${LIBXSLT_LIBS} -lexslt ${LIBXML_LIBS} \
    -L${SLAX_EXTDIR} -L../../libslax -lslax

if HAVE_READLINE
LIBS += -L/opt/local/lib -lreadline
endif

if HAVE_LIBEDIT
LIBS += -ledit
endif
libext_bit_la_SOURCES = \
    ext_bit.c

libdir = ${SLAX_EXTDIR}

UGLY_NAME = http%3A%2F%2Fxml.libslax.org%2Fbit.ext

install-exec-hook:
	@DLNAME=`sh -c '. libext_bit.la ; echo $$dlname'` \
		&& echo Install link $$DLNAME "->" ${UGLY_NAME} "..." \
		&& cd ${SLAX_EXTDIR} \
		&& chmod +w . \
		&& rm -f ${UGLY_NAME} \
		&& ${LN_S} $$DLNAME ${UGLY_NAME}
