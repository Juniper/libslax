#
# $Id$
#

AM_CFLAGS = \
    -I${top_srcdir} \
    -I${top_srcdir}/libslax \
    ${LIBXML_CFLAG} \
    ${LIBXSLT_CFLAGS}

if SLAX_DEBUG
AM_CFLAGS += -g -DSLAX_DEBUG
endif

bin_PROGRAMS = slaxproc

slaxproc_SOURCES = slaxproc.c
slaxproc_LDADD = ../libslax/libslax.la

man_MANS = slaxproc.1x

EXTRA_DIST = slaxproc.1x
