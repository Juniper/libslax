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

AC_PREREQ([2.69])
AC_INIT([libslax],[3.1.6],[phil@juniper.net])
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

# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_ALLOCA
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([bzero memmove strchr strcspn strerror strspn])
AC_CHECK_FUNCS([sranddev srand strlcpy strlcat])
AC_CHECK_FUNCS([fdopen getrusage])
AC_CHECK_FUNCS([gettimeofday ctime])
AC_CHECK_FUNCS([getpass])
AC_CHECK_FUNCS([sysctlbyname])
AC_CHECK_FUNCS([flock])
AC_CHECK_FUNCS([asprintf])
AC_CHECK_FUNCS([memrchr])

AC_CHECK_HEADERS([dlfcn.h])
AC_CHECK_HEADERS([tzfile.h])
AC_CHECK_HEADERS([stdtime/tzfile.h])
AC_CHECK_FUNCS([dlfunc])
AC_CHECK_FUNCS([strnstr])
AC_CHECK_FUNCS([strndup])

AC_CHECK_HEADERS([inttypes.h])
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
    [SLAX_DEBUG=$enableval],
    [SLAX_DEBUG=no])
AC_MSG_RESULT([$SLAX_DEBUG])
if test "$SLAX_DEBUG" != "no"; then
    AC_DEFINE([SLAX_DEBUG], [1], [Enable debugging])
fi
AM_CONDITIONAL([SLAX_DEBUG], [test "$SLAX_DEBUG" != "no"])

AC_MSG_CHECKING([whether to build with readline])
AC_ARG_ENABLE([readline],
    [  --enable-readline    Enable support for GNU readline],
    [HAVE_READLINE=$enableval],
    [HAVE_READLINE=no])
AC_MSG_RESULT([$HAVE_READLINE])
if test "$HAVE_READLINE" != "no"; then
    AC_DEFINE([HAVE_READLINE], [1], [Support readline])
fi
AM_CONDITIONAL([HAVE_READLINE], [test "$HAVE_READLINE" != "no"])

AC_MSG_CHECKING([whether to build with libedit])
AC_ARG_ENABLE([libedit],
    [  --enable-libedit    Enable support for libedit (BSD readline)],
    [HAVE_LIBEDIT=$enableval],
    [HAVE_LIBEDIT=no])
AC_MSG_RESULT([$HAVE_LIBEDIT])
if test "$HAVE_LIBEDIT" != "no"; then
    AC_DEFINE([HAVE_LIBEDIT], [1], [Support libedit])
fi
AM_CONDITIONAL([HAVE_LIBEDIT], [test "$HAVE_LIBEDIT" != "no"])

AC_MSG_CHECKING([compiler for gcc])
HAVE_GCC=no
if test "${CC}" != ""; then
    HAVE_GCC=`${CC} --version 2>&1 | grep -i ' Free Software'`
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

dnl humanize_number(3) is a great function, but it's not standard.
dnl Note Macosx has the function in libutil.a but doesn't ship the
dnl header file, so I'll need to carry my own implementation.  See:
dnl    https://devforums.apple.com/thread/271121
AC_CHECK_HEADERS([libutil.h])
AC_CHECK_LIB([util], [humanize_number],
     [HAVE_HUMANIZE_NUMBER=$ac_cv_header_libutil_h],
     [HAVE_HUMANIZE_NUMBER=no])

AC_MSG_CHECKING([humanize_number results])
AC_MSG_RESULT(:${HAVE_HUMANIZE_NUMBER}:${ac_cv_header_libutil_h}:)

if test "$HAVE_HUMANIZE_NUMBER" = "yes"; then
    AC_DEFINE([HAVE_HUMANIZE_NUMBER], [1], [humanize_number(3)])
fi

AM_CONDITIONAL([HAVE_HUMANIZE_NUMBER], [test "$HAVE_HUMANIZE_NUMBER" = "yes"])

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
AC_ARG_WITH(sqlite3, [  --with-sqlite3[=DIR]       Include SQLITE3 ])
if test "$with_sqlite3" != "" ; then
    AX_LIB_SQLITE3([$SQLITE3_REQUIRED_VERSION])
    AM_CONDITIONAL([HAVE_SQLITE3], [test VERSION_TO_NUMBER(echo $SQLITE3_VERSION) -ge VERSION_TO_NUMBER(echo $SQLITE3_REQUIRED_VERSION)])
    if test VERSION_TO_NUMBER(echo $SQLITE3_VERSION) -ge VERSION_TO_NUMBER(echo $SQLITE3_REQUIRED_VERSION)
    then
        HAVE_SQLITE3=yes
    fi
else
        HAVE_SQLITE3=no
fi
AM_CONDITIONAL([HAVE_SQLITE3], [test "$HAVE_SQLITE3" != "no"])

#
# Check the availability of sqlcipher
#
AC_ARG_WITH(sqlcipher, [  --with-sqlcipher[=DIR] Path to SQLCipher])
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
# ---- captive libxml module selection
#
# libxml2 used to have 34 separate --with-XXX/--without-XXX flags plus
# a tangle of hand-written dependency rules between them.  That's
# replaced here with a single --with-bxml-modules=LIST option.  LIST is
# a space-separated set of module names and/or module sets:
#
#   --with-bxml-modules='default'                 (the default, if omitted)
#   --with-bxml-modules='min'                     start from (almost) nothing
#   --with-bxml-modules='all'                     everything on
#   --with-bxml-modules='legacy'                  default, plus schematron/zlib
#   --with-bxml-modules='default -history icu'    default, minus history, plus icu
#
# A bare name turns a module on; "-name" or "no-name" turns it off (the
# leading "+" on a name, e.g. "+icu", is accepted too, but is just a
# synonym for the bare name -- it's optional).  Unknown names/sets are
# a hard error.  Run "configure --enable-list-bxml-modules" to see the
# table.
#
# Each table row is name:default:implies:description.  "implies" is a
# comma-separated list of other modules that get forced on, to a fixed
# point, whenever this one ends up on -- so turning on "relaxng" also
# turns on "regexps" and "schemas" without having to spell them out.
#
BXML_MODULE_TABLE='
c14n:on:output,xpath:Canonical XML 1.0 support
catalog:on::XML Catalogs support
debug:on::debugging module
docs:off::Build documentation
history:off:readline:history support for xmllint shell
readline:off::use readline for shell history
html:on::HTML parser
http:off::ABI compatibility stubs for removed HTTP support
iconv:on::iconv support
icu:off::ICU support
iso8859x:on::ISO-8859-X support if no iconv
modules:on::dynamic modules support
output:on::serialization support
pattern:on::xmlPattern selection interface
push:on::push parser interfaces
python:on::Python bindings
reader:on:push:xmlReader parsing interface
regexps:on::regular expressions support
relaxng:on:regexps,schemas:RELAX NG support
sax1:on::older SAX1 interface
schemas:on:pattern,regexps:XML Schemas 1.0 support
schematron:off:pattern,xpath:Schematron support
threads:on::multithreading support
thread-alloc:off::per-thread malloc hooks
valid:on::DTD validation support
writer:on:output,push:xmlWriter serialization interface
xinclude:on::XInclude 1.0 support
xpath:on::XPath 1.0 support
xptr:on:xpath:XPointer support
zlib:off::use libz for compression
tls:off::thread-local storage
'

BXML_MODULE_NAMES=`echo "$BXML_MODULE_TABLE" | awk -F: 'NF { print $1 }'`

# name -> shell-safe variable fragment ("thread-alloc" -> "thread_alloc")
bxml_var () {
    echo "$1" | tr '-' '_'
}

bxml_known () {
    for bxml_n in $BXML_MODULE_NAMES; do
        test "$bxml_n" = "$1" && return 0
    done
    return 1
}

bxml_set () { # name value
    eval "bxml_mod_`bxml_var $1`=$2"
}

bxml_get () { # name
    eval "echo \$bxml_mod_`bxml_var $1`"
}

# reset every module to its table default (also (re)loads implies/)
bxml_reset_all () {
    bxml_old_IFS=$IFS
    IFS='
'
    for bxml_row in $BXML_MODULE_TABLE; do
        test -z "$bxml_row" && continue
        bxml_name=`echo "$bxml_row" | cut -d: -f1`
        bxml_default=`echo "$bxml_row" | cut -d: -f2`
        bxml_implies=`echo "$bxml_row" | cut -d: -f3`
        eval "bxml_implies_`bxml_var $bxml_name`=\"$bxml_implies\""
        bxml_set "$bxml_name" "$bxml_default"
    done
    IFS=$bxml_old_IFS
}
bxml_reset_all

bxml_apply_min () {
    for bxml_n in $BXML_MODULE_NAMES; do bxml_set "$bxml_n" off; done
}

bxml_apply_all () {
    for bxml_n in $BXML_MODULE_NAMES; do bxml_set "$bxml_n" on; done
}

bxml_apply_legacy () {
    bxml_reset_all
    bxml_set schematron on
    bxml_set zlib on
}

AC_ARG_WITH(bxml-modules,
  [  --with-bxml-modules=LIST   captive libxml modules to build.  LIST is a
                        space-separated list of module names and/or
                        module sets (default, min, all, legacy);
                        prefix a name with - or no- to disable it.
                        See --enable-list-bxml-modules for the full set.],
  [BXML_MODULES_ARG=$withval], [BXML_MODULES_ARG=default])
test "x$BXML_MODULES_ARG" = "xyes" && BXML_MODULES_ARG=default

AC_ARG_ENABLE(list-bxml-modules,
  [  --enable-list-bxml-modules   List captive libxml modules/sets and exit],
  [BXML_LIST=$enableval], [BXML_LIST=no])

if test "x$BXML_LIST" != "xno"; then
    echo "bxml module sets: default min all legacy"
    echo ""
    echo "bxml modules:"
    bxml_old_IFS=$IFS
    IFS='
'
    for bxml_row in $BXML_MODULE_TABLE; do
        test -z "$bxml_row" && continue
        bxml_name=`echo "$bxml_row" | cut -d: -f1`
        bxml_default=`echo "$bxml_row" | cut -d: -f2`
        bxml_implies=`echo "$bxml_row" | cut -d: -f3`
        bxml_desc=`echo "$bxml_row" | cut -d: -f4`
        printf '  %-14s %-4s implies: %-20s %s\n' \
            "$bxml_name" "$bxml_default" "$bxml_implies" "$bxml_desc"
    done
    IFS=$bxml_old_IFS
    exit 0
fi

for bxml_tok in $BXML_MODULES_ARG; do
    case $bxml_tok in
        default) bxml_reset_all ;;
        min)     bxml_apply_min ;;
        all)     bxml_apply_all ;;
        legacy)  bxml_apply_legacy ;;
        -*|no-*)
            bxml_name=`echo "$bxml_tok" | sed -e 's/^-//' -e 's/^no-//'`
            bxml_known "$bxml_name" || \
                AC_MSG_ERROR([unknown bxml module: $bxml_name])
            bxml_set "$bxml_name" off
            ;;
        +*)
            bxml_name=`echo "$bxml_tok" | sed -e 's/^+//'`
            bxml_known "$bxml_name" || \
                AC_MSG_ERROR([unknown bxml module: $bxml_name])
            bxml_set "$bxml_name" on
            ;;
        *)
            bxml_known "$bxml_tok" || \
                AC_MSG_ERROR([unknown bxml module or set: $bxml_tok])
            bxml_set "$bxml_tok" on
            ;;
    esac
done

# Force on, to a fixed point, every prerequisite of a module that's on.
# This is the only direction resolved automatically: turning a module
# off never reaches out and turns other modules off, so a contradictory
# request (e.g. "relaxng -regexps") just re-enables regexps and says so,
# rather than silently dropping relaxng instead.
bxml_changed=yes
bxml_pass=0
while test "$bxml_changed" = yes; do
    bxml_changed=no
    bxml_pass=`expr $bxml_pass + 1`
    test $bxml_pass -gt 10 && \
        AC_MSG_ERROR([bxml module dependency resolution did not converge])
    for bxml_name in $BXML_MODULE_NAMES; do
        test "`bxml_get $bxml_name`" = on || continue
        eval "bxml_implies=\$bxml_implies_`bxml_var $bxml_name`"
        test -z "$bxml_implies" && continue
        for bxml_dep in `echo "$bxml_implies" | tr ',' ' '`; do
            if test "`bxml_get $bxml_dep`" = off; then
                AC_MSG_NOTICE([bxml: enabling '$bxml_dep', required by '$bxml_name'])
                bxml_set "$bxml_dep" on
                bxml_changed=yes
            fi
        done
    done
done

BXML_MODULES=
for bxml_name in $BXML_MODULE_NAMES; do
    test "`bxml_get $bxml_name`" = on && BXML_MODULES="$BXML_MODULES $bxml_name"
done
BXML_MODULES=`echo $BXML_MODULES`
AC_SUBST(BXML_MODULES)
AC_MSG_RESULT([bxml modules enabled:${BXML_MODULES}])

# Turn each module's on/off state into a WITH_<NAME> shell variable
# (1 or 0) so the explicit AC_SUBST/AC_DEFINE_UNQUOTED/AM_CONDITIONAL
# calls below -- one block per module, matching the WITH_<NAME> and
# WITH_<NAME>_SOURCES names libbxml's own Makefile.am/xmlversion.h.in
# already expect -- can pick them up.
for bxml_name in $BXML_MODULE_NAMES; do
    bxml_upper=`bxml_var $bxml_name | tr a-z A-Z`
    if test "`bxml_get $bxml_name`" = on; then
        eval "WITH_$bxml_upper=1"
    else
        eval "WITH_$bxml_upper=0"
    fi
done

AC_SUBST(WITH_C14N)
AC_DEFINE_UNQUOTED([WITH_C14N], [$WITH_C14N], [Canonical XML 1.0 support])
AM_CONDITIONAL([WITH_C14N], [test "$WITH_C14N" = "1"])
AM_CONDITIONAL([WITH_C14N_SOURCES], [test "$WITH_C14N" = "1"])

AC_SUBST(WITH_CATALOG)
AC_DEFINE_UNQUOTED([WITH_CATALOG], [$WITH_CATALOG], [XML Catalogs support])
AM_CONDITIONAL([WITH_CATALOG], [test "$WITH_CATALOG" = "1"])
AM_CONDITIONAL([WITH_CATALOG_SOURCES], [test "$WITH_CATALOG" = "1"])

AC_SUBST(WITH_DEBUG)
AC_DEFINE_UNQUOTED([WITH_DEBUG], [$WITH_DEBUG], [debugging module])
AM_CONDITIONAL([WITH_DEBUG], [test "$WITH_DEBUG" = "1"])
AM_CONDITIONAL([WITH_DEBUG_SOURCES], [test "$WITH_DEBUG" = "1"])

AC_SUBST(WITH_DOCS)
AC_DEFINE_UNQUOTED([WITH_DOCS], [$WITH_DOCS], [Build documentation])
AM_CONDITIONAL([WITH_DOCS], [test "$WITH_DOCS" = "1"])

AC_SUBST(WITH_HISTORY)
AC_DEFINE_UNQUOTED([WITH_HISTORY], [$WITH_HISTORY], [history support for xmllint shell])
AM_CONDITIONAL([WITH_HISTORY], [test "$WITH_HISTORY" = "1"])

AC_SUBST(WITH_READLINE)
AC_DEFINE_UNQUOTED([WITH_READLINE], [$WITH_READLINE], [use readline for shell history])
AM_CONDITIONAL([WITH_READLINE], [test "$WITH_READLINE" = "1"])

AC_SUBST(WITH_HTML)
AC_DEFINE_UNQUOTED([WITH_HTML], [$WITH_HTML], [HTML parser])
AM_CONDITIONAL([WITH_HTML], [test "$WITH_HTML" = "1"])
AM_CONDITIONAL([WITH_HTML_SOURCES], [test "$WITH_HTML" = "1"])

AC_SUBST(WITH_HTTP)
AC_DEFINE_UNQUOTED([WITH_HTTP], [$WITH_HTTP], [ABI compatibility stubs for removed HTTP support])
AM_CONDITIONAL([WITH_HTTP], [test "$WITH_HTTP" = "1"])
AM_CONDITIONAL([WITH_HTTP_SOURCES], [test "$WITH_HTTP" = "1"])

AC_SUBST(WITH_ICONV)
AC_DEFINE_UNQUOTED([WITH_ICONV], [$WITH_ICONV], [iconv support])
AM_CONDITIONAL([WITH_ICONV], [test "$WITH_ICONV" = "1"])

AC_SUBST(WITH_ICU)
AC_DEFINE_UNQUOTED([WITH_ICU], [$WITH_ICU], [ICU support])
AM_CONDITIONAL([WITH_ICU], [test "$WITH_ICU" = "1"])

AC_SUBST(WITH_ISO8859X)
AC_DEFINE_UNQUOTED([WITH_ISO8859X], [$WITH_ISO8859X], [ISO-8859-X support if no iconv])
AM_CONDITIONAL([WITH_ISO8859X], [test "$WITH_ISO8859X" = "1"])

AC_SUBST(WITH_MODULES)
AC_DEFINE_UNQUOTED([WITH_MODULES], [$WITH_MODULES], [dynamic modules support])
AM_CONDITIONAL([WITH_MODULES], [test "$WITH_MODULES" = "1"])
AM_CONDITIONAL([WITH_MODULES_SOURCES], [test "$WITH_MODULES" = "1"])

AC_SUBST(WITH_OUTPUT)
AC_DEFINE_UNQUOTED([WITH_OUTPUT], [$WITH_OUTPUT], [serialization support])
AM_CONDITIONAL([WITH_OUTPUT], [test "$WITH_OUTPUT" = "1"])
AM_CONDITIONAL([WITH_OUTPUT_SOURCES], [test "$WITH_OUTPUT" = "1"])

AC_SUBST(WITH_PATTERN)
AC_DEFINE_UNQUOTED([WITH_PATTERN], [$WITH_PATTERN], [xmlPattern selection interface])
AM_CONDITIONAL([WITH_PATTERN], [test "$WITH_PATTERN" = "1"])
AM_CONDITIONAL([WITH_PATTERN_SOURCES], [test "$WITH_PATTERN" = "1"])

AC_SUBST(WITH_PUSH)
AC_DEFINE_UNQUOTED([WITH_PUSH], [$WITH_PUSH], [push parser interfaces])
AM_CONDITIONAL([WITH_PUSH], [test "$WITH_PUSH" = "1"])

AC_SUBST(WITH_PYTHON)
AC_DEFINE_UNQUOTED([WITH_PYTHON], [$WITH_PYTHON], [Python bindings])
AM_CONDITIONAL([WITH_PYTHON], [test "$WITH_PYTHON" = "1"])

AC_SUBST(WITH_READER)
AC_DEFINE_UNQUOTED([WITH_READER], [$WITH_READER], [xmlReader parsing interface])
AM_CONDITIONAL([WITH_READER], [test "$WITH_READER" = "1"])
AM_CONDITIONAL([WITH_READER_SOURCES], [test "$WITH_READER" = "1"])

AC_SUBST(WITH_REGEXPS)
AC_DEFINE_UNQUOTED([WITH_REGEXPS], [$WITH_REGEXPS], [regular expressions support])
AM_CONDITIONAL([WITH_REGEXPS], [test "$WITH_REGEXPS" = "1"])
AM_CONDITIONAL([WITH_REGEXPS_SOURCES], [test "$WITH_REGEXPS" = "1"])

AC_SUBST(WITH_RELAXNG)
AC_DEFINE_UNQUOTED([WITH_RELAXNG], [$WITH_RELAXNG], [RELAX NG support])
AM_CONDITIONAL([WITH_RELAXNG], [test "$WITH_RELAXNG" = "1"])
AM_CONDITIONAL([WITH_RELAXNG_SOURCES], [test "$WITH_RELAXNG" = "1"])

AC_SUBST(WITH_SAX1)
AC_DEFINE_UNQUOTED([WITH_SAX1], [$WITH_SAX1], [older SAX1 interface])
AM_CONDITIONAL([WITH_SAX1], [test "$WITH_SAX1" = "1"])
AM_CONDITIONAL([WITH_SAX1_SOURCES], [test "$WITH_SAX1" = "1"])

AC_SUBST(WITH_SCHEMAS)
AC_DEFINE_UNQUOTED([WITH_SCHEMAS], [$WITH_SCHEMAS], [XML Schemas 1.0 support])
AM_CONDITIONAL([WITH_SCHEMAS], [test "$WITH_SCHEMAS" = "1"])
AM_CONDITIONAL([WITH_SCHEMAS_SOURCES], [test "$WITH_SCHEMAS" = "1"])

AC_SUBST(WITH_SCHEMATRON)
AC_DEFINE_UNQUOTED([WITH_SCHEMATRON], [$WITH_SCHEMATRON], [Schematron support])
AM_CONDITIONAL([WITH_SCHEMATRON], [test "$WITH_SCHEMATRON" = "1"])
AM_CONDITIONAL([WITH_SCHEMATRON_SOURCES], [test "$WITH_SCHEMATRON" = "1"])

AC_SUBST(WITH_THREADS)
AC_DEFINE_UNQUOTED([WITH_THREADS], [$WITH_THREADS], [multithreading support])
AM_CONDITIONAL([WITH_THREADS], [test "$WITH_THREADS" = "1"])

AC_SUBST(WITH_THREAD_ALLOC)
AC_DEFINE_UNQUOTED([WITH_THREAD_ALLOC], [$WITH_THREAD_ALLOC], [per-thread malloc hooks])
AM_CONDITIONAL([WITH_THREAD_ALLOC], [test "$WITH_THREAD_ALLOC" = "1"])

AC_SUBST(WITH_VALID)
AC_DEFINE_UNQUOTED([WITH_VALID], [$WITH_VALID], [DTD validation support])
AM_CONDITIONAL([WITH_VALID], [test "$WITH_VALID" = "1"])
AM_CONDITIONAL([WITH_VALID_SOURCES], [test "$WITH_VALID" = "1"])

AC_SUBST(WITH_WRITER)
AC_DEFINE_UNQUOTED([WITH_WRITER], [$WITH_WRITER], [xmlWriter serialization interface])
AM_CONDITIONAL([WITH_WRITER], [test "$WITH_WRITER" = "1"])
AM_CONDITIONAL([WITH_WRITER_SOURCES], [test "$WITH_WRITER" = "1"])

AC_SUBST(WITH_XINCLUDE)
AC_DEFINE_UNQUOTED([WITH_XINCLUDE], [$WITH_XINCLUDE], [XInclude 1.0 support])
AM_CONDITIONAL([WITH_XINCLUDE], [test "$WITH_XINCLUDE" = "1"])
AM_CONDITIONAL([WITH_XINCLUDE_SOURCES], [test "$WITH_XINCLUDE" = "1"])

AC_SUBST(WITH_XPATH)
AC_DEFINE_UNQUOTED([WITH_XPATH], [$WITH_XPATH], [XPath 1.0 support])
AM_CONDITIONAL([WITH_XPATH], [test "$WITH_XPATH" = "1"])
AM_CONDITIONAL([WITH_XPATH_SOURCES], [test "$WITH_XPATH" = "1"])

AC_SUBST(WITH_XPTR)
AC_DEFINE_UNQUOTED([WITH_XPTR], [$WITH_XPTR], [XPointer support])
AM_CONDITIONAL([WITH_XPTR], [test "$WITH_XPTR" = "1"])
AM_CONDITIONAL([WITH_XPTR_SOURCES], [test "$WITH_XPTR" = "1"])

AC_SUBST(WITH_ZLIB)
AC_DEFINE_UNQUOTED([WITH_ZLIB], [$WITH_ZLIB], [use libz for compression])
AM_CONDITIONAL([WITH_ZLIB], [test "$WITH_ZLIB" = "1"])

AC_SUBST(WITH_TLS)
AC_DEFINE_UNQUOTED([WITH_TLS], [$WITH_TLS], [thread-local storage])
AM_CONDITIONAL([WITH_TLS], [test "$WITH_TLS" = "1"])

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

if ${XML_CONFIG} --libs > /dev/null 2>&1
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

if ${XSLT_CONFIG} --libs > /dev/null 2>&1
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

# Look for "struct clockinfo" for sysctl printing
AC_MSG_CHECKING([for struct clockinfo])
AC_COMPILE_IFELSE(
    [AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/time.h>
]],
                     [[struct clockinfo ci; ci.hz = 0;]])],
    [
        AC_MSG_RESULT([yes])
        AC_DEFINE_UNQUOTED([HAVE_CLOCKINFO], 1,
	                   [Define to 1 if you have the `clockinfo' struct.])
    ],
    [
        AC_MSG_RESULT([no])
    ]
)

# Look for "struct clockinfo" tickadj for sysctl printing
AC_MSG_CHECKING([for struct clockinfo.tickadj])
AC_COMPILE_IFELSE(
    [AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/time.h>
]],
                     [[struct clockinfo ci; ci.tickadj = 0;]])],
    [
        AC_MSG_RESULT([yes])
        AC_DEFINE_UNQUOTED([HAVE_CLOCKINFO_TICKADJ], 1,
	                   [Define to 1 if you have the `clockinfo.tickadj' fiel.])
    ],
    [
        AC_MSG_RESULT([no])
    ]
)

# Look for "struct loadavg" for sysctl printing
AC_MSG_CHECKING([for struct loadavg])
AC_COMPILE_IFELSE(
    [AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
]],
                     [[struct loadavg la; la.fscale = 0;]])],
    [
        AC_MSG_RESULT([yes])
        AC_DEFINE_UNQUOTED([HAVE_LOADAVG], 1,
	                   [Define to 1 if you have the `loadavg' struct.])
    ],
    [
        AC_MSG_RESULT([no])
    ]
)

# Look for "struct xsw_usage" for sysctl printing
AC_MSG_CHECKING([for struct xsw_usage])
AC_COMPILE_IFELSE(
    [AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/sysctl.h>
]],
                     [[struct xsw_usage xsu; xsu.xsu_total = 0;]])],
    [
        AC_MSG_RESULT([yes])
        AC_DEFINE_UNQUOTED([HAVE_XSW_USAGE], 1,
	                   [Define to 1 if you have the `xsw_usage' struct.])
    ],
    [
        AC_MSG_RESULT([no])
    ]
)

# Look for "dev_t" for sysctl printing
AC_MSG_CHECKING([for struct dev_t])
AC_COMPILE_IFELSE(
    [AC_LANG_PROGRAM([[
#include <sys/types.h>
]],
                     [[dev_t dev; dev = 0;]])],
    [
        AC_MSG_RESULT([yes])
        AC_DEFINE_UNQUOTED([HAVE_DEV_T], 1,
	                   [Define to 1 if you have the `dev_t' type.])
    ],
    [
        AC_MSG_RESULT([no])
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
  tests/base/Makefile
  tests/bugs/Makefile
  tests/core/Makefile
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
  humanize:         ${HAVE_HUMANIZE_NUMBER:-no}
])
