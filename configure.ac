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
AC_INIT([libslax], [0.6.1], [phil@juniper.net])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
LT_INIT([disable-shared])
AC_DISABLE_SHARED

#
# libxslt 1.1.26 has a fix for AVT terminating with close braces
#
LIBXML_REQUIRED_VERSION=2.7.7
LIBXSLT_REQUIRED_VERSION=1.1.26

AC_PROG_CC
AC_PROG_INSTALL
AC_CONFIG_MACRO_DIR([m4])

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

AC_CHECK_HEADERS([sys/time.h])
AC_CHECK_HEADERS([ctype.h errno.h stdio.h stdlib.h])
AC_CHECK_HEADERS([string.h sys/param.h unistd.h ])
AC_CHECK_HEADERS([sys/sysctl.h])

AC_CHECK_LIB([xml2], [xmlNewParserCtxt])
AC_CHECK_LIB([xslt], [xsltInit])

dnl
dnl Some packages need to be checked against version numbers so we
dnl define a function here for later use
dnl
AC_DEFUN([VERSION_TO_NUMBER],
[`$1 | sed -e 's/libxml //' | awk 'BEGIN { FS = "."; } { printf "%d", ([$]1 * 1000 + [$]2) * 1000 + [$]3;}'`])

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

AC_MSG_CHECKING([whether to build with printflike])
AC_ARG_ENABLE([printflike],
    [  --enable-printflike    Enable use of GCC __printflike attribute],
    [HAVE_PRINTFLIKE=yes;
           AC_DEFINE([HAVE_PRINTFLIKE], [1], [Support printflike])],
    [HAVE_PRINTFLIKE=no])
AC_MSG_RESULT([$HAVE_PRINTFLIKE])
AM_CONDITIONAL([HAVE_PRINTFLIKE], [test "$HAVE_PRINTFLIKE" != ""])


# Allow the reuse of the libxslt tests, if they have the source code
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

#
# ---- end of noise
#

case $host_os in
     darwin*)
        LIBTOOL=glibtool
	;;
esac

SLAX_LIBS=-lslax
SLAX_SRCDIR='${srcdir}'
SLAX_LIBDIR='${libdir}'
SLAX_INCLUDEDIR='${includedir}'

AC_SUBST(SLAX_SRCDIR)
AC_SUBST(SLAX_LIBDIR)
AC_SUBST(SLAX_INCLUDEDIR)
AC_SUBST(SLAX_CFLAGS)
AC_SUBST(SLAX_LIBTOOLLIBS)
AC_SUBST(SLAX_LIBS)
AC_SUBST(WIN32_EXTRA_LIBADD)

dnl for the spec file
RELDATE=`date +'%Y-%m-%d%n'`
AC_SUBST(RELDATE)

AC_MSG_RESULT(Using configure dir $ac_abs_confdir)

if test -d $ac_abs_confdir/.svn ; then
  extra=`svn info $ac_abs_confdir | grep Revision | sed 's+Revision: ++'`
  echo extra=$extra
  if test "$extra" != ""
  then
      LIBSLAX_VERSION_EXTRA="-SVN$extra"
  fi
fi

LIBSLAX_VERSION=$PACKAGE_VERSION
LIBSLAX_VERSION_NUMBER=VERSION_TO_NUMBER(echo $PACKAGE_VERSION)
AC_SUBST(LIBSLAX_VERSION)
AC_SUBST(LIBSLAX_VERSION_NUMBER)
AC_SUBST(LIBSLAX_VERSION_EXTRA)

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
  Makefile
  slax-config
  libslax/Makefile
  libslax/slaxconfig.h
  slaxproc/Makefile
  tests/Makefile
  tests/core/Makefile
  tests/bugs/Makefile
  tests/errors/Makefile
  tests/libxslt/Makefile
  doc/Makefile
])
AC_OUTPUT
