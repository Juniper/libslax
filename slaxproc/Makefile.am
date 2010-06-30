#
# $Id$
#


include ${top_srcdir}/warnings.mk

AM_CFLAGS = \
    -I${top_srcdir} \
    -I${top_srcdir}/libslax \
    ${LIBXML_CFLAGS} \
    ${LIBXSLT_CFLAGS} \
    ${WARNINGS}

if SLAX_DEBUG
AM_CFLAGS += -g -DSLAX_DEBUG
endif

LIBS = \
    ${LIBXSLT_LIBS} \
    -lexslt \
    ${LIBXML_LIBS}

bin_PROGRAMS = slaxproc

slaxproc_SOURCES = slaxproc.c
slaxproc_LDADD = ../libslax/libslax.la
slaxproc_LDFLAGS = -static

man_MANS = slaxproc.1x

EXTRA_DIST = slaxproc.1x
