#
# Copyright 2011-2025, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

if SLAX_WARNINGS_HIGH
SLAX_WARNINGS = HIGH
endif
include ${top_srcdir}/warnings.mk
include ${top_srcdir}/lto.mk

AM_CFLAGS = \
    -DLIBSLAX_XMLSOFT_NEED_PRIVATE \
    -I${top_builddir} \
    -I${top_srcdir} \
    -I${top_srcdir}/libslax \
    ${LIBXML_CFLAGS} \
    ${LIBXSLT_CFLAGS} \
    ${WARNINGS} \
    $(LTO_CFLAGS)

LIBS = \
    ${LIBXSLT_LIBS} \
    ${LIBXML_LIBS}

if SLAX_DEBUG
AM_CFLAGS += -g -DSLAX_DEBUG
endif

if HAVE_READLINE
LIBS += -L/opt/local/lib -lreadline
endif

if HAVE_LIBEDIT
LIBS += -ledit -lcurses
endif

if HAVE_LIBM
LIBS += -lm
endif

LIBS += -lutil

bin_PROGRAMS = slaxproc

slaxproc_SOURCES = slaxproc.c
slaxproc_LDFLAGS = $(LTO_LDFLAGS) $(LTO_STATIC_LDFLAGS)

LDADD = \
    ${top_builddir}/libslax/libslax.la \
    ${top_builddir}/libpsu/libpsu.la

man_MANS = slaxproc.1x slaxdebugger.1x

EXTRA_DIST = slaxproc.1x slaxdebugger.1x
