#
# $Id$
#

include ${top_srcdir}/warnings.mk

AM_CFLAGS = -I${top_srcdir} ${LIBXML_CFLAG} ${LIBXSLT_CFLAGS} ${WARNINGS}

if SLAX_DEBUG
AM_CFLAGS += -g -DSLAX_DEBUG
endif

lib_LTLIBRARIES = libslax.la

include_HEADERS = \
    slax.h

noinst_HEADERS = \
    slaxinternals.h

BISON_COMMAND = ${BISON} -kvd -b slaxparser

libslax_la_SOURCES = \
    slaxdebugger.c \
    slaxext.c \
    slaxloader.c \
    slaxparser.c \
    slaxstring.c \
    slaxwriter.c

slaxparser.h: slaxparser.c
slaxext.lo slaxext.o: slaxparser.h
slaxloader.lo slaxloader.o: slaxparser.h
slaxstring.lo slaxstring.o: slaxparser.h
slaxwriter.lo slaxwriter.o: slaxparser.h

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

EXTRA_DIST = ${SLAXPIECES}

CLEANFILES += slaxparser-out.y slaxparser-xpl.y

slaxparser-xpl.y: slaxparser-xp.y
	sed 's/xp_/xpl_/g' < ${srcdir}/slaxparser-xp.y > slaxparser-xpl.y

clean-slaxparser:
	${RM} -f slaxparser-xpl.y slaxparser-out.y
