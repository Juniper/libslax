#
# $Id$
#
# Copyright 2014, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

if SLAX_WARNINGS_HIGH
SLAX_WARNINGS = HIGH
endif
include ${top_srcdir}/warnings.mk

ext_sqliteincdir = ${includedir}/libslax

AM_CFLAGS = \
    -DLIBSLAX_XMLSOFT_NEED_PRIVATE \
    -I${top_builddir} \
    -I${top_srcdir} \
    ${LIBSLAX_CFLAGS} \
    ${LIBXSLT_CFLAGS} \
    ${LIBXML_CFLAGS} \
    ${SQLITE3_CFLAGS} \
    ${WARNINGS}

AM_CFLAGS += \
 -DSLAX_EXTDIR=\"${SLAX_EXTDIR}\"

LIBNAME = db_driver_sqlite
pkglib_LTLIBRARIES = libext_db_sqlite.la
LIBS = \
    ${LIBXSLT_LIBS} \
    -lexslt \
    ${LIBXML_LIBS} \
    ${SQLITE3_LIBS} \
    -L${top_builddir}/libslax -lslax -lsqlite3 \
    -L${top_builddir}/extensions/db

LDADD = ${top_builddir}/libslax/libslax.la

AM_LDFLAGS = \
    -Wl, --whole-archive, \
    ../libext_db.la \
    --no-whole-archive

if HAVE_READLINE
LIBS += -L/opt/local/lib -lreadline
endif

if HAVE_LIBEDIT
LIBS += -ledit
endif

libext_db_sqlite_la_SOURCES = \
    db_driver_sqlite.c

pkglibdir = ${SLAX_EXTDIR}/db

install-exec-hook:
	@DLNAME=`sh -c '. ./libext_db_sqlite.la ; echo $$dlname'`; \
		if [ x"$$DLNAME" = x ]; \
                    then DLNAME=${LIBNAME}.${SLAX_LIBEXT}; fi ; \
		echo Install link $$DLNAME "->" ${UGLY_NAME} "..." ; \
		mkdir -p ${DESTDIR}${SLAX_EXTDIR}/db ; \
		cd ${DESTDIR}${SLAX_EXTDIR}/db \
		&& chmod +w . \
        && rm -f ${LIBNAME}.so \
        && ${LN_S} $$DLNAME ${LIBNAME}.so
