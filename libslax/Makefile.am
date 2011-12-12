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

slaxincdir = ${includedir}/libslax

AM_CFLAGS = -I${top_srcdir} ${LIBXSLT_CFLAGS} ${LIBXML_CFLAG} ${WARNINGS}

AM_CFLAGS += \
    -DLIBSLAX_XMLSOFT_NEED_PRIVATE \
    -DSLAX_EXTDIR=\"${SLAX_EXTDIR}\"

lib_LTLIBRARIES = libslax.la
libslax_la_LIBADD = ${LIBXSLT_LIBS} -lexslt ${LIBXML_LIBS}

slaxinc_HEADERS = \
     slax.h \
     slaxconfig.h \
     slaxdata.h \
     slaxdyn.h \
     xmlsoft.h

noinst_HEADERS = \
    slaxinternals.h \
    slaxio.h \
    slaxlexer.h \
    slaxloader.h \
    slaxnames.h \
    slaxprofiler.h \
    slaxstring.h \
    slaxtree.h \
    slaxutil.h \
    xmlsoft.h

SLAXHEADERS = ${noinst_HEADERS} slaxparser.h

BISON_COMMAND = ${BISON} -kvd -b slaxparser

libslax_la_SOURCES = \
    slaxdebugger.c \
    slaxdyn.c \
    slaxext.c \
    slaxio.c \
    slaxlexer.c \
    slaxloader.c \
    slaxmvar.c \
    slaxparser.c \
    slaxprofiler.c \
    slaxstring.c \
    slaxtree.c \
    slaxwriter.c

slaxparser.h: slaxparser.c
${libslax_la_SOURCES:.c=.o}: ${SLAXHEADERS}
${libslax_la_SOURCES:.c=.lo}: ${SLAXHEADERS}

SED_ARGS = \
-e 's:int yyparse (void);::' \
-e 's/yyparse (void \*YYPARSE_PARAM)/slaxParse (slax_data_t *slax_data)/'

slaxparser.c: slaxparser-out.y
	${BISON_COMMAND} -o slaxparser.c slaxparser-out.y
	@${MV} $@ $@.old
	@${SED} ${SED_ARGS} < $@.old  > $@
	@${RM} $@.old

CLEANFILES = slaxparser.h slaxparser.c slaxparser.output

#
# See the comment at the top of slaxparser-xp.y for details
#
SLAXPIECES = \
    ${srcdir}/slaxparser.y \
    ${srcdir}/slaxparser-xp.y \
    slaxparser-xpl.y \
    ${srcdir}/slaxparser-tail.y

slaxparser-out.y: ${SLAXPIECES}
	cat ${SLAXPIECES} > $@

if HAVE_READLINE
LIBS += -lreadline
endif

if HAVE_LIBEDIT
LIBS += -ledit
endif

EXTRA_DIST = \
    slaxparser.y \
    slaxparser-xp.y \
    slaxparser-tail.y

CLEANFILES += slaxparser-out.y slaxparser-xpl.y

slaxparser-xpl.y: slaxparser-xp.y
	sed 's/xp_/xpl_/g' < ${srcdir}/slaxparser-xp.y > slaxparser-xpl.y

clean-slaxparser:
	${RM} -f slaxparser-xpl.y slaxparser-out.y
