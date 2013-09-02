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

BISON_COMMAND = ${BISON} -kv -b slaxparser
ECHO_BISON_0 = @echo "  BISON " $@;
ECHO_BISON_ = ${ECHO_BISON_${AM_DEFAULT_VERBOSITY}}
ECHO_BUILD_0 = @echo "  BUILD " $@;
ECHO_BUILD_ = ${ECHO_BUILD_${AM_DEFAULT_VERBOSITY}}

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
-e 's/yyparse (void \*YYPARSE_PARAM)/slaxParse (slax_data_t *slax_data)/' \
-e 's:yyvs = yyvsa;:yyvs = yyvsa; bzero(yyvsa, sizeof(yyvsa));:'

slaxparser.c: slaxparser-out.y
	${ECHO_BISON_${V}}${BISON_COMMAND} --defines=slaxparser.h.new \
			-o slaxparser.c slaxparser-out.y
	@${MV} slaxparser.c slaxparser.c.old
	@${SED} ${SED_ARGS} < slaxparser.c.old  > slaxparser.c
	@${RM} slaxparser.c.old

slaxparser.h: slaxparser.c
	@${MV} slaxparser.h.new slaxparser.h

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
	${ECHO_BUILD_${V}}cat ${SLAXPIECES} > $@

if HAVE_READLINE
LIBS += -L/opt/local/lib -lreadline
endif

if HAVE_LIBEDIT
LIBS += -ledit
endif

EXTRA_DIST = \
    slaxparser.y \
    slaxparser-xp.y \
    slaxparser-tail.y

man_MANS = libslax.3x
EXTRA_DIST += libslax.3x

CLEANFILES += slaxparser-out.y slaxparser-xpl.y

slaxparser-xpl.y: slaxparser-xp.y
	${ECHO_BUILD_${V}}sed 's/xp_/xpl_/g' < ${srcdir}/slaxparser-xp.y > slaxparser-xpl.y

clean-slaxparser:
	${RM} -f slaxparser-xpl.y slaxparser-out.y
