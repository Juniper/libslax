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

lib_LTLIBRARIES = libpsu.la

psuincdir = ${includedir}/libpsu

psuinc_HEADERS = \
    psualloc.h \
    psucpu.h \
    psubase64.h \
    psucommon.h \
    psulog.h \
    psustring.h \
    psuthread.h \
    psutime.h

libpsu_la_SOURCES = \
    psualloc.c \
    psuasprintf.c \
    psubase64.c \
    psucpu.c \
    psulog.c \
    psumemdump.c \
    psustring.c
