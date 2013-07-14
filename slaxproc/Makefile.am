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

AM_CFLAGS = \
    -DLIBSLAX_XMLSOFT_NEED_PRIVATE \
    -I${top_srcdir} \
    -I${top_srcdir}/libslax \
    ${LIBXML_CFLAGS} \
    ${LIBXSLT_CFLAGS} \
    ${WARNINGS}

LIBS = \
    ${LIBXSLT_LIBS} \
    -lexslt \
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

bin_PROGRAMS = slaxproc

slaxproc_SOURCES = slaxproc.c
#slaxproc_LDADD = ../libslax/libslax.la
#slaxproc_LDFLAGS = -static

LDADD = \
    ${top_builddir}/libslax/libslax.la

man_MANS = slaxproc.1x slaxdebugger.1x

EXTRA_DIST = slaxproc.1x slaxdebugger.1x
