#
# Copyright 2017, Juniper Networks, Inc.
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

AM_CFLAGS = \
    -I${top_builddir} \
    -I${top_srcdir} \
    ${WARNINGS}

lib_LTLIBRARIES = libparrotdb.la
libparrotdb_la_LIBADD = $(top_builddir)/libpsu/libpsu.la

parrotdbincdir = ${includedir}/parrotdb

parrotdbinc_HEADERS = \
    paarb.h \
    pabitmap.h \
    pacommon.h \
    paconfig.h \
    pafixed.h \
    paistr.h \
    palog2.h \
    pammap.h \
    papat.h

libparrotdb_la_SOURCES = \
    paarb.c \
    pacommon.c \
    paconfig.c \
    pafixed.c \
    paistr.c \
    pammap.c \
    papat.c

GEN_CODE = ${top_srcdir}/bin/gen-code.sh

gen_headers:
	sh ${GEN_CODE}

auto_gen: ${GEN_CODE} gen/tstamp

gen/tstamp:
	${MKDIR_P} gen
	sh ${GEN_CODE}
	touch gen/tstamp

${libparrotdb_la_SOURCES}: auto_gen

clean-local:
	rm -fr gen

