#
# $Id$
#
# See ./INSTALL for more info
#

#
# Release numbering: even numbered dot releases are official ones, and
# odd numbers are development ones.  The svn version of this file will
# only (ONLY!) ever (EVER!) contain odd numbers, so I'll always know if
# a particular user has the dist or svn release.
#

AC_PREREQ(2.2)
AC_INIT([libslax], [0.20.2], [phil@juniper.net])
AM_INIT_AUTOMAKE([-Wall -Werror foreign -Wno-portability])

# Support silent build rules.  Requires at least automake-1.11.
# Disable with "configure --disable-silent-rules" or "make V=1"
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

#
# libxslt 1.1.26 has a fix for AVT terminating with close braces
#
LIBXML_REQUIRED_VERSION=2.7.7
LIBXSLT_REQUIRED_VERSION=1.1.26
LIBCURL_REQUIRED_VERSION=7.21.3
SQLITE3_REQUIRED_VERSION=3.7.13

AC_PROG_CC
AM_PROG_AR
AC_PROG_INSTALL
AC_CONFIG_MACRO_DIR([m4])
AC_PROG_LN_S

# Must be after AC_PROG_AR
LT_INIT([dlopen shared])

AC_PATH_PROG(BASENAME, basename, /usr/bin/basename)
AC_PATH_PROG(BISON, bison, /usr/bin/bison)
AC_PATH_PROG(CAT, cat, /bin/cat)
AC_PATH_PROG(CHMOD, chmod, /bin/chmod)
AC_PATH_PROG(CP, cp, /bin/cp)
AC_PATH_PROG(DIFF, diff, /usr/bin/diff)
AC_PATH_PROG(MKDIR, mkdir, /bin/mkdir)
AC_PATH_PROG(MV, mv, /bin/mv)
AC_PATH_PROG(RM, rm, /bin/rm)
AC_PATH_PROG(SED, sed, /bin/sed)

AC_STDC_HEADERS

# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_ALLOCA
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([bzero memmove strchr strcspn strerror strspn])
AC_CHECK_FUNCS([sranddev srand strlcpy])
AC_CHECK_FUNCS([fdopen getrusage])
AC_CHECK_FUNCS([gettimeofday ctime])
AC_CHECK_FUNCS([getpass])
AC_CHECK_FUNCS([sysctlbyname])
AC_CHECK_FUNCS([flock])
AC_CHECK_FUNCS([asprintf])

AC_CHECK_HEADERS([dlfcn.h])
AC_CHECK_HEADERS([tzfile.h])
AC_CHECK_HEADERS([stdtime/tzfile.h])
AC_CHECK_FUNCS([dlfunc])

AC_CHECK_HEADERS([sys/time.h])
AC_CHECK_HEADERS([ctype.h errno.h stdio.h stdlib.h])
AC_CHECK_HEADERS([string.h sys/param.h unistd.h ])
AC_CHECK_HEADERS([sys/sysctl.h])

AC_CHECK_LIB([crypto], [MD5_Init])
AM_CONDITIONAL([HAVE_LIBCRYPTO], [test "$HAVE_LIBCRYPTO" != "no"])

AC_CHECK_LIB([m], [lrint])
AM_CONDITIONAL([HAVE_LIBM], [test "$HAVE_LIBM" != "no"])

AC_CHECK_LIB([xml2], [xmlNewParserCtxt])
AC_CHECK_LIB([xslt], [xsltInit])

dnl
dnl Some packages need to be checked against version numbers so we
dnl define a function here for later use
dnl
AC_DEFUN([VERSION_TO_NUMBER],
[`$1 | sed -e 's/lib.* //' | awk 'BEGIN { FS = "."; } { printf "%d", ([$]1 * 1000 + [$]2) * 1000 + [$]3;}'`])

AC_MSG_CHECKING([whether to build with warnings])
AC_ARG_ENABLE([warnings],
    [  --enable-warnings    Turn on compiler warnings],
    [SLAX_WARNINGS=$enableval],
    [SLAX_WARNINGS=no])
AC_MSG_RESULT([$SLAX_WARNINGS])
AM_CONDITIONAL([SLAX_WARNINGS_HIGH], [test "$SLAX_WARNINGS" != "no"])

AC_MSG_CHECKING([whether to build with debugging])
AC_ARG_ENABLE([debug],
    [  --enable-debug    Turn on debugging],
    [SLAX_DEBUG=yes; AC_DEFINE([SLAX_DEBUG], [1], [Enable debugging])],
    [SLAX_DEBUG=no])
AC_MSG_RESULT([$SLAX_DEBUG])
AM_CONDITIONAL([SLAX_DEBUG], [test "$SLAX_DEBUG" != "no"])

AC_MSG_CHECKING([whether to build with readline])
AC_ARG_ENABLE([readline],
    [  --enable-readline    Enable support for GNU readline],
    [HAVE_READLINE=yes; AC_DEFINE([HAVE_READLINE], [1], [Support readline])],
    [HAVE_READLINE=no])
AC_MSG_RESULT([$HAVE_READLINE])
AM_CONDITIONAL([HAVE_READLINE], [test "$HAVE_READLINE" != "no"])

AC_MSG_CHECKING([whether to build with libedit])
AC_ARG_ENABLE([libedit],
    [  --enable-libedit    Enable support for libedit (BSD readline)],
    [HAVE_LIBEDIT=yes; AC_DEFINE([HAVE_LIBEDIT], [1], [Support libedit])],
    [HAVE_LIBEDIT=no])
AC_MSG_RESULT([$HAVE_LIBEDIT])
AM_CONDITIONAL([HAVE_LIBEDIT], [test "$HAVE_LIBEDIT" != "no"])

AC_MSG_CHECKING([compiler for gcc])
HAVE_GCC=no
if test "${CC}" != ""; then
    HAVE_GCC=`${CC} --version 2>&1 | grep -i ' GCC'`
    if test "${HAVE_GCC}" != ""; then
        HAVE_GCC=yes
    else
        HAVE_GCC=no
    fi
fi
AC_MSG_RESULT([$HAVE_GCC])
AM_CONDITIONAL([HAVE_GCC], [test "$HAVE_GCC" = "yes"])

AC_MSG_CHECKING([whether to build with printflike])
AC_ARG_ENABLE([printflike],
    [  --enable-printflike    Enable use of GCC __printflike attribute],
    [HAVE_PRINTFLIKE=yes;
           AC_DEFINE([HAVE_PRINTFLIKE], [1], [Support printflike])],
    [HAVE_PRINTFLIKE=no])
AC_MSG_RESULT([$HAVE_PRINTFLIKE])
AM_CONDITIONAL([HAVE_PRINTFLIKE], [test "$HAVE_PRINTFLIKE" != ""])


#
# Allow the reuse of the libxslt tests, if they have the source code
# These are striclty optional and are really only needed for regression
# testing during development.  Due to numerous issues with libxml and
# libxslt, there are some platform-specific differences (like rounding
# errors in libxml2) that may report test failures.
#
AC_ARG_WITH(libxslt_tests, [  --with-libxslt-tests[=DIR]    Include the libxslt tests])
if test "$with_libxslt_tests" != "" ; then
   if test -d "$with_libxslt_tests" ; then
     with_libxslt_tests=`cd $with_libxslt_tests; pwd`
   else
      AC_MSG_ERROR([bad value ${with_libxslt_tests} for --with-libxslt-tests])
   fi

#   if ! test -f $with_libxslt_tests/Makefile ; then
#      AC_MSG_ERROR([Run 'configure' in ${with_libxslt_tests}]) 
#   fi

   WITH_LIBXSLT_TESTS=$with_libxslt_tests
   AC_MSG_RESULT(Using libxslt test in $WITH_LIBXSLT_TESTS)
fi
AM_CONDITIONAL([USE_LIBXSLT_TESTS], [test "$WITH_LIBXSLT_TESTS" != ""])
AC_SUBST(WITH_LIBXSLT_TESTS)

#
# Check the availability of sqlite3
#
AX_LIB_SQLITE3([$SQLITE3_REQUIRED_VERSION])
AM_CONDITIONAL([HAVE_SQLITE3], [test VERSION_TO_NUMBER(echo $SQLITE3_VERSION) -ge VERSION_TO_NUMBER(echo $SQLITE3_REQUIRED_VERSION)])
if test VERSION_TO_NUMBER(echo $SQLITE3_VERSION) -ge VERSION_TO_NUMBER(echo $SQLITE3_REQUIRED_VERSION)
then
    HAVE_SQLITE3=yes
fi

#
# Check the availability of sqlcipher
#
AC_ARG_WITH(sqlcipher, [ --with-sqlcipher[=DIR] Path to SQLCipher])
if test "$with_sqlcipher" != ""; then
    if test -f "$with_sqlcipher/sqlite3.h" ; then
        USE_SQLCIPHER=$with_sqlcipher
        AC_DEFINE([HAVE_SQLCIPHER], [1], "Have SQLCipher")
        HAVE_SQLCIPHER=yes
        SQLCIPHER_CFLAGS="-I$with_sqlcipher"
    fi
fi
AM_CONDITIONAL([HAVE_SQLCIPHER], [test "$USE_SQLCIPHER" != ""])

#
# ---- start of noise
#

#
# ---- handle libxml2
#

LIBXML_CONFIG_PREFIX=""
LIBXML_SRC=""

AC_ARG_WITH(libxml-prefix,
        [  --with-libxml-prefix=[PFX]           Specify location of libxml config],
        LIBXML_CONFIG_PREFIX=$withval
)
        
AC_ARG_WITH(libxml-include-prefix,
        [  --with-libxml-include-prefix=[PFX]   Specify location of libxml headers],
        LIBXML_CFLAGS="-I$withval"
)

AC_ARG_WITH(libxml-libs-prefix,
        [  --with-libxml-libs-prefix=[PFX]      Specify location of libxml libs],
        LIBXML_LIBS="-L$withval"
)

AC_ARG_WITH(libxml-src,
        [  --with-libxml-src=[DIR]              For libxml thats not installed yet (sets all three above)],
        LIBXML_SRC="$withval"
)
AC_SUBST(LIBXML_SRC)

dnl
dnl where is xml2-config
dnl

AC_SUBST(LIBXML_REQUIRED_VERSION)
AC_MSG_CHECKING(for libxml libraries >= $LIBXML_REQUIRED_VERSION) 
if test "x$LIBXML_CONFIG_PREFIX" != "x"
then
        XML_CONFIG=${LIBXML_CONFIG_PREFIX}/bin/xml2-config
else
        XML_CONFIG=xml2-config
fi

dnl
dnl make sure xml2-config is executable,
dnl test version and init our variables
dnl

if ${XML_CONFIG} --libs print > /dev/null 2>&1
then
	XMLVERS=`$XML_CONFIG --version`
	if test VERSION_TO_NUMBER(echo $XMLVERS) -ge VERSION_TO_NUMBER(echo $LIBXML_REQUIRED_VERSION)
	then
		LIBXML_LIBS="$LIBXML_LIBS `$XML_CONFIG --libs`"
		LIBXML_CFLAGS="$LIBXML_CFLAGS `$XML_CONFIG --cflags`"
		AC_MSG_RESULT($XMLVERS found)
	else
		AC_MSG_ERROR(Version $XMLVERS found. You need at least libxml2 $LIBXML_REQUIRED_VERSION for this version of libxslt)
	fi
else
	AC_MSG_ERROR([Could not find libxml2 anywhere, check ftp://xmlsoft.org/.])
fi

AC_SUBST(XML_CONFIG)
AC_SUBST(LIBXML_LIBS)
AC_SUBST(LIBXML_CFLAGS)



#
# ---- handle libxslt
#

LIBXSLT_CONFIG_PREFIX=""
LIBXSLT_SRC=""

AC_ARG_WITH(libxslt-prefix,
        [  --with-libxslt-prefix=[PFX]           Specify location of libxslt config],
        LIBXSLT_CONFIG_PREFIX=$withval
)
        
AC_ARG_WITH(libxslt-include-prefix,
        [  --with-libxslt-include-prefix=[PFX]   Specify location of libxslt headers],
        LIBXSLT_CFLAGS="-I$withval"
)

AC_ARG_WITH(libxslt-libs-prefix,
        [  --with-libxslt-libs-prefix=[PFX]      Specify location of libxslt libs],
        LIBXSLT_LIBS="-L$withval"
)

AC_ARG_WITH(libxslt-src,
        [  --with-libxslt-src=[DIR]              For libxslt thats not installed yet (sets all three above)],
        LIBXSLT_SRC="$withval"
)
AC_SUBST(LIBXSLT_SRC)

dnl
dnl where is xslt-config
dnl

AC_SUBST(LIBXSLT_REQUIRED_VERSION)
AC_MSG_CHECKING(for libxslt libraries >= $LIBXSLT_REQUIRED_VERSION) 
if test "x$LIBXSLT_CONFIG_PREFIX" != "x"
then
        XSLT_CONFIG=${LIBXSLT_CONFIG_PREFIX}/bin/xslt-config
else
        XSLT_CONFIG=xslt-config
fi

dnl
dnl make sure xslt-config is executable,
dnl test version and init our variables
dnl

if ${XSLT_CONFIG} --libs print > /dev/null 2>&1
then
	XSLTVERS=`$XSLT_CONFIG --version`
	if test VERSION_TO_NUMBER(echo $XSLTVERS) -ge VERSION_TO_NUMBER(echo $LIBXSLT_REQUIRED_VERSION)
	then
		LIBXSLT_LIBS="$LIBXSLT_LIBS `$XSLT_CONFIG --libs`"
		LIBXSLT_CFLAGS="$LIBXSLT_CFLAGS `$XSLT_CONFIG --cflags`"
		AC_MSG_RESULT($XSLTVERS found)
	else
		AC_MSG_ERROR(Version $XSLTVERS found. You need at least libxslt $LIBXSLT_REQUIRED_VERSION for this version of libxslt)
	fi
else
	AC_MSG_ERROR([Could not find libxslt anywhere, check ftp://xmlsoft.org/.])
fi

AC_SUBST(XSLT_CONFIG)
AC_SUBST(LIBXSLT_CFLAGS)
AC_SUBST(LIBXSLT_LIBS)

LIBCURL_CONFIG_PREFIX=""

AC_ARG_WITH(libcurl-prefix,
        [  --with-libcurl-prefix=[PFX]           Specify location of libcurl config],
        LIBCURL_CONFIG_PREFIX=$withval
)

AC_SUBST(LIBCURL_REQUIRED_VERSION)
AC_MSG_CHECKING(for libcurl libraries >= $LIBCURL_REQUIRED_VERSION) 
if test "x$LIBCURL_CONFIG_PREFIX" != "x"
then
        CURL_CONFIG=${LIBCURL_CONFIG_PREFIX}/bin/curl-config
else
        CURL_CONFIG=curl-config
fi

dnl
dnl make sure curl-config is executable,
dnl test version and init our variables
dnl

if ${CURL_CONFIG} --libs > /dev/null 2>&1
then
        CURLVERS=`$CURL_CONFIG --version`
        if test VERSION_TO_NUMBER(echo $CURLVERS) -ge VERSION_TO_NUMBER(echo $LIBCURL_REQUIRED_VERSION)
        then
                LIBCURL_LIBS="$LIBCURL_LIBS `$CURL_CONFIG --libs`"
                LIBCURL_CFLAGS="$LIBCURL_CFLAGS `$CURL_CONFIG --cflags`"
                AC_MSG_RESULT($CURLVERS found)
        else
                AC_MSG_ERROR(Version $CURLVERS found. You need at least libcurl $LIBCURL_REQUIRED_VERSION for this version of libslax)
        fi
else
        AC_MSG_ERROR([Could not find libcurl anywhere, check http://curl.haxx.se])
fi

AC_SUBST(CURL_CONFIG)
AC_SUBST(LIBCURL_CFLAGS)
AC_SUBST(LIBCURL_LIBS)

#
# Find the version of BISON
#
AC_MSG_CHECKING([bison version])
test -z "${BISON}" && BISON=bison
BISON_VERSION=[`${BISON} --version | grep 'GNU Bison' | cut -d ' ' -f 4 \
    | sed -e 's/[-\.]/ /g' | tr -d a-z \
    | sed -e 's/\([0-9]*\) \([0-9]*\).*/\1.\2/'`]
BISON_VERSION_NUMBER=VERSION_TO_NUMBER(echo $BISON_VERSION)
AC_SUBST(BISON_VERSION)
AC_SUBST(BISON_VERSION_NUMBER)
AM_CONDITIONAL([HAVE_BISON30], [test "$BISON_VERSION_NUMBER" -ge 3000000])
if test "$BISON_VERSION_NUMBER" -ge 3000000 ; then
    AC_DEFINE([HAVE_BISON30], [1], [Bison version >= 3.0])
fi
AC_MSG_RESULT($BISON_VERSION)

#
# ---- end of noise
#

# Cygwin lacks the modern st_mtimespec field in struct stat
AC_MSG_CHECKING([for stat.st_mtimespec])
AC_COMPILE_IFELSE(
    [AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/stat.h>
]],
                     [[struct stat st; st.st_mtimespec.tv_sec = 0;]])],
    [
        AC_MSG_RESULT([yes])
        AC_DEFINE_UNQUOTED([HAVE_MTIMESPEC], 1,
	                   [Define to 1 if you have the `st_mtimespec' field.])
    ],
    [
        AC_MSG_RESULT([no])
        AC_DEFINE_UNQUOTED([HAVE_MTIMESPEC], 0,
                           [Define to 1 if you have the `st_mtimespec' field.])
    ]
)

case $host_os in
     darwin-13*)
#        LIBTOOL=libtool
	SLAX_LIBEXT=dylib
	;;
     darwin*)
        LIBTOOL=glibtool
	SLAX_LIBEXT=dylib
	;;
     Linux*|linux*)
        CFLAGS="-D_GNU_SOURCE $CFLAGS"
	LDFLAGS=-ldl
	SLAX_LIBEXT=so
	;;
     cygwin*|CYGWIN*)
	LDFLAGS=-no-undefined
	SLAX_LIBEXT=ddl
	;;
esac

case $prefix in
     NONE)
	prefix=/usr/local
	;;
esac

AC_ARG_WITH(extensions-dir,
        [  --with-extensions-dir=[DIR]           Specify location of extension libraries],
        [SLAX_EXTDIR=$withval],
        [SLAX_EXTDIR=$libdir/slax/extensions]
)

AC_ARG_WITH(share-dir,
        [  --with-share-dir=[DIR]           Specify location of shared files],
        [SLAX_SHAREDIR=$withval],
        [SLAX_SHAREDIR=$datarootdir/slax]
)

AC_ARG_WITH(oxtradoc-dir,
        [  --with-oxtradoc-dir=[DIR]        Specify location of oxtradoc files],
        [SLAX_OXTRADOCDIR=$withval],
        [SLAX_OXTRADOCDIR=$datarootdir/oxtradoc]
)

AC_ARG_WITH(bin-dir,
        [  --with-bin-dir=[DIR]        Specify location of executable files],
        [SLAX_BINDIR=$withval],
        [SLAX_BINDIR=$prefix/bin]
)

SLAX_LIBS="-lslax -lpsu"
SLAX_SRCDIR=${srcdir}
SLAX_LIBDIR=${libdir}
SLAX_INCLUDEDIR=${includedir}

AC_SUBST(SLAX_SRCDIR)
AC_SUBST(SLAX_LIBDIR)
AC_SUBST(SLAX_LIBEXT)
AC_SUBST(SLAX_EXTDIR)
AC_SUBST(SLAX_SHAREDIR)
AC_SUBST(SLAX_OXTRADOCDIR)
AC_SUBST(SLAX_BINDIR)
AC_SUBST(SLAX_INCLUDEDIR)
AC_SUBST(SLAX_CFLAGS)
AC_SUBST(SLAX_LIBTOOLLIBS)
AC_SUBST(SLAX_LIBS)
AC_SUBST(WIN32_EXTRA_LIBADD)

dnl for the spec file
RELDATE=`date +'%Y-%m-%d%n'`
AC_SUBST(RELDATE)

AC_MSG_RESULT(Using configure dir $ac_abs_confdir)

if test -d $ac_abs_confdir/.git ; then
  extra=`git branch | awk '/\*/ { print $2 }'`
  if test "$extra" != "" -a "$extra" != "master"
  then
      LIBSLAX_VERSION_EXTRA="-git-$extra"
  fi
fi

LIBSLAX_VERSION=$PACKAGE_VERSION
LIBSLAX_VERSION_NUMBER=VERSION_TO_NUMBER(echo $PACKAGE_VERSION)
AC_SUBST(LIBSLAX_VERSION)
AC_SUBST(LIBSLAX_VERSION_NUMBER)
AC_SUBST(LIBSLAX_VERSION_EXTRA)

AC_CONFIG_HEADERS([libslax/slaxconfig.h])
AC_CONFIG_FILES([
  Makefile
  slax-config
  libpsu/Makefile
  parrotdb/Makefile
  libxi/Makefile
  libslax/Makefile
  libslax/slaxversion.h
  extensions/Makefile
  extensions/bit/Makefile
  extensions/curl/Makefile
  extensions/exslt/Makefile
  extensions/os/Makefile
  extensions/db/Makefile
  extensions/db/sqlite/Makefile
  extensions/xutil/Makefile
  slaxproc/Makefile
  tests/Makefile
  tests/art/Makefile
  tests/core/Makefile
  tests/bugs/Makefile
  tests/errors/Makefile
  tests/libxslt/Makefile
  tests/pa/Makefile
  tests/xi/Makefile
  bin/Makefile
  doc/Makefile
  doc/oxtradoc/oxtradoc
  packaging/libslax.pc
  packaging/rpm/libslax.spec
  packaging/rpm/libslax.space.spec
  packaging/libslax.rb.base
  packaging/freebsd/port-Makefile
])
AC_OUTPUT

AC_MSG_NOTICE([summary of build options:

  libslax version:  ${VERSION} ${LIBSLAX_VERSION_EXTRA}
  host type:        ${host} / ${host_os}
  install prefix:   ${prefix}
  srcdir:           ${SLAX_SRCDIR}
  libdir:           ${SLAX_LIBDIR}
  bindir:           ${SLAX_BINDIR}
  includedir:       ${SLAX_INCLUDEDIR}
  extensions dir:   ${SLAX_EXTDIR}

  compiler:         ${CC} (${HAVE_GCC:-no})
  compiler flags:   ${CFLAGS}
  library types:    Shared=${enable_shared}, Static=${enable_static}
  libxml version:   ${XMLVERS}
  libxslt cflags:   ${LIBXSLT_CFLAGS}
  libxml libs:      ${LIBXML_LIBS}
  libxslt version:  ${XSLTVERS}
  libxslt cflags:   ${LIBXSLT_CFLAGS}
  libxslt libs:     ${LIBXSLT_LIBS}
  libcurl version:  ${CURLVERS}
  libcurl cflags:   ${LIBCURL_CFLAGS}
  libcurl libs:     ${LIBCURL_LIBS}
  bison version:    ${BISON_VERSION}
  sqlite3 version:  ${SQLITE3_VERSION}

  warnings:         ${SLAX_WARNINGS:-no}
  debug:            ${SLAX_DEBUG:-no}
  readline:         ${HAVE_READLINE:-no}
  libedit:          ${HAVE_LIBEDIT:-no}
  printf-like:      ${HAVE_PRINTFLIKE:-no}
  libxslt tests:    ${WITH_LIBXSLT_TESTS:-no}
  sqlite3:          ${HAVE_SQLITE3:-no}
  sqlcipher:        ${HAVE_SQLCIPHER:-no}
])
