#
# $Id$
#
# Copyright 2012, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

include ${top_srcdir}/warnings.mk

BISON_COMMAND = ${BISON} -kvd -b jsonparser

ext_curlincdir = ${includedir}/libslax

AM_CFLAGS = \
    -DLIBSLAX_XMLSOFT_NEED_PRIVATE \
    -I${top_srcdir} \
    -I${top_srcdir}/libslax \
    ${LIBSLAX_CFLAGS} \
    ${LIBXSLT_CFLAGS} \
    ${LIBXML_CFLAGS} \
    ${WARNINGS}

AM_CFLAGS += \
 -DSLAX_EXTDIR=\"${SLAX_EXTDIR}\"

lib_LTLIBRARIES = libext_xutil.la
LIBS = \
    ${LIBXSLT_LIBS} \
    -lexslt \
    ${LIBXML_LIBS} \
    -L${SLAX_LIBDIR} -L../../libslax -lslax

if HAVE_READLINE
LIBS += -L/opt/local/lib -lreadline
endif

if HAVE_LIBEDIT
LIBS += -ledit
endif

XUTILHEADERS = jsonparser.h

libext_xutil_la_SOURCES = \
    jsonparser.c \
    jsonlexer.c \
    ext_xutil.c

libdir = ${SLAX_EXTDIR}

jsonparser.h: jsonparser.c
${libext_xutil_la_SOURCES:.c=.o}: ${XUTILHEADERS}
${libext_xutil_la_SOURCES:.c=.lo}: ${XUTILHEADERS}

SED_ARGS = \
-e 's:int yyparse (void);::' \
-e 's/yyparse (void \*YYPARSE_PARAM)/extXutilJsonParse (slax_data_t *slax_data)/'

jsonparser.c: jsonparser.y
	${BISON_COMMAND} -o jsonparser.c ${srcdir}/jsonparser.y
	@${MV} $@ $@.old
	@${SED} ${SED_ARGS} < $@.old  > $@
	@${RM} $@.old

CLEANFILES = jsonparser.h jsonparser.c jsonparser.output

UGLY_NAME = http%3A%2F%2Fxml.libslax.org%2Fxutil.ext

install-exec-hook:
	@DLNAME=`sh -c '. libext_xutil.la ; echo $$dlname'`; \
                    echo Install link $$DLNAME "->" ${UGLY_NAME} "..." \
                    && cd ${SLAX_EXTDIR} \
                    && chmod +w . \
	            && rm -f ${UGLY_NAME} \
		    && ${LN_S} $$DLNAME ${UGLY_NAME}

jsontest: jsonparser.o
	${COMPILE} -DUNIT_TEST ${srcdir}/jsonlexer.c -o jsontest.o 
	${LINK} -o jsontest jsonparser.o jsontest.o
