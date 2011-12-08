#
# $Id$
#

if SLAX_WARNINGS
include ${top_srcdir}/warnings.mk
endif

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
LIBS += -lreadline
endif

if HAVE_LIBEDIT
LIBS += -ledit
endif

bin_PROGRAMS = slaxproc

slaxproc_SOURCES = slaxproc.c
slaxproc_LDADD = ../libslax/libslax.la
slaxproc_LDFLAGS = -static

man_MANS = slaxproc.1x slaxdebugger.1x

EXTRA_DIST = slaxproc.1x slaxdebugger.1x
