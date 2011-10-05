#
# $Id$
#

if SLAX_WARNINGS
include ${top_srcdir}/warnings.mk
endif

bitincdir = ${includedir}/libslax

EXTRA_DIST = bit.txt

AM_CFLAGS = \
    -I${top_srcdir} \
    -I${top_srcdir}/libslax \
    ${LIBXSLT_CFLAGS} \
    ${LIBXML_CFLAG} \
    ${WARNINGS}

AM_CFLAGS += \
 -DSLAX_EXTDIR=\"${SLAX_EXTDIR}\"

lib_LTLIBRARIES = libbit.la
LIBS = ${LIBXSLT_LIBS} -lexslt ${LIBXML_LIBS} -L${SLAX_EXTDIR} -lslax

libbit_la_SOURCES = \
    ext_bit.c

libdir = ${SLAX_EXTDIR}

UGLY_NAME = http%3A%2F%2Fxml.juniper.net%2Fextensions%2Fbit.ext

install-exec-hook:
	@DLNAME=`sh -c '. ${libdir}/libbit.la ; echo $$dlname'` \
		&& echo Install link $$DLNAME "->" ${UGLY_NAME} "..." \
	        && cd ${SLAX_EXTDIR} \
	        && rm -f ${UGLY_NAME} \
		&& ${LN_S} $$DLNAME ${UGLY_NAME}
