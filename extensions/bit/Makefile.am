#
# $Id$
#

include ${top_srcdir}/warnings.mk

bitincdir = ${includedir}/libslax

AM_CFLAGS = \
    -I${top_srcdir} \
    -I${top_srcdir}/libslax \
    ${LIBXSLT_CFLAGS} \
    ${LIBXML_CFLAG} \
    ${WARNINGS}

AM_CFLAGS += \
 -DSLAX_EXTDIR=\"${SLAX_EXTDIR}\"

lib_LTLIBRARIES = libbit.la
libbit_la_LIBADD = ${LIBXSLT_LIBS} -lexslt ${LIBXML_LIBS}

libbit_la_SOURCES = \
    ext_bit.c

libdir = ${SLAX_EXTDIR}

UGLY_NAME = http-mumble.ext

install-exec-hook:
	@DLNAME=`sh -c '. libbit.la ; echo $$dlname'` \
		&& echo Initializing $$DLNAME "->" ${UGLY_NAME} "..." \
	        && cd ${SLAX_EXTDIR} \
		&& ln -s -f $$DLNAME ${UGLY_NAME}
