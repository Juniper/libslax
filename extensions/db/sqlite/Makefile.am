#
# $Id$
#
# Copyright 2014, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

#
# If you've come here to look to the source that annoying error:
#
#    ld: warning: directory not found for option '-L../../libslax'
#
# then give it up.  This is an actifact of glibtool, which records
# each libraries link arguments to pass them to libraries that link
# against those libraries.  Since we link against ../libext_db.a,
# we get the "dependency_libs" from that file, which includes the
# "-L../../libslax" that bites us.  Nothing to do except wait for
# some glorious future where autotools and glibtool are something
# we laugh about in the "remember when" category where MS-DOS, TSRs,
# and other foolish live.
#

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
    ${WARNINGS}

if HAVE_SQLCIPHER
    AM_CFLAGS += $(SQLIPHER_CFLAGS)
else
    AM_CFLAGS += ${SQLITE3_CFLAGS}
endif

AM_CFLAGS += \
 -DSLAX_EXTDIR=\"${SLAX_EXTDIR}\"

LIBNAME = db_driver_sqlite
pkglib_LTLIBRARIES = libext_db_sqlite.la
LIBS = \
    ${LIBXSLT_LIBS} \
    -lexslt \
    ${LIBXML_LIBS} \
    -L${top_builddir}/libslax -lslax \
    -L${top_builddir}/extensions/db

if HAVE_SQLCIPHER
    LIBS += ${SQLCIPHER_LIBS} -lsqlcipher
else
    LIBS += ${SQLITE3_LIBS} -lsqlite3
endif

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
		if [ "$(build_os)" = "cygwin" ]; \
		    then DLNAME="../bin/$$DLNAME"; fi ; \
		echo Install link $$DLNAME "->" ${UGLY_NAME} "..." ; \
		mkdir -p ${DESTDIR}${SLAX_EXTDIR}/db ; \
		cd ${DESTDIR}${SLAX_EXTDIR}/db \
		&& chmod +w . \
        && rm -f ${LIBNAME}.so \
        && ${LN_S} $$DLNAME ${LIBNAME}.so
