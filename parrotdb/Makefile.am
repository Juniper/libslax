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

parrotdbincdir = ${includedir}/libparrotdb

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
