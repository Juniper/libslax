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
AM_INIT_AUTOMAKE([-Wall -Werror foreign -Wno-portability foreign subdir-objects no-dist-gzip dist-xz])

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
AC_PROG_MKDIR_P
PKG_PROG_PKG_CONFIG

# Must be after AC_PROG_AR
LT_INIT([dlopen shared])
LT_LIB_M

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

dnl checks for bxml
AC_CHECK_DECLS([getentropy], [], [], [#include <sys/random.h>])
AC_CHECK_DECLS([glob], [], [], [#include <glob.h>])
AC_CHECK_DECLS([mmap], [], [], [#include <sys/mman.h>])

AM_CONDITIONAL(WITH_GLOB, test "$ac_cv_have_decl_glob" = "yes")

dnl old checks for libslax (XXX remove)
dnl AC_CHECK_LIB([bxml], [xmlNewParserCtxt])
dnl AC_CHECK_LIB([xslt], [xsltInit])

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
AC_SUBST(HAVE_GCC)
AM_CONDITIONAL([HAVE_GCC], [test "$HAVE_GCC" = "yes"])

AC_MSG_CHECKING([compiler for clang])
HAVE_CLANG=no
if test "${CC}" != ""; then
    HAVE_CLANG=`${CC} cc --help 2>&1 | head -1 | grep -i clang`
    if test "${HAVE_CLANG}" != ""; then
        HAVE_CLANG=yes
    else
        HAVE_CLANG=no
    fi
fi
AC_MSG_RESULT([$HAVE_CLANG])
AM_CONDITIONAL([HAVE_CLANG], [test "$HAVE_CLANG" = "yes"])

# ENABLE_LTO only sets the default baked into lto.mk via "ENABLE_LTO
# ?= @ENABLE_LTO@"; it's a plain make variable (not an AM_CONDITIONAL),
# so it can still be overridden per invocation without reconfiguring,
# e.g. "make ENABLE_LTO=yes clean all".
AC_MSG_CHECKING([whether to build with link-time optimization (LTO)])
AC_ARG_ENABLE([lto],
    [  --enable-lto    Build with link-time optimization (-flto)],
    [ENABLE_LTO=$enableval],
    [ENABLE_LTO=no])
AC_MSG_RESULT([$ENABLE_LTO])
AC_SUBST(ENABLE_LTO)

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
# errors in bxml) that may report test failures.
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
# --- Logic for bxml components
#

# bxml is versioned identically to libslax itself ($VERSION, from the
# AC_INIT above) rather than to the upstream bxml release it was
# vendored from.
LIBXML_MAJOR_VERSION=`echo $VERSION | cut -d . -f 1`
LIBXML_MINOR_VERSION=`echo $VERSION | cut -d . -f 2`
LIBXML_MICRO_VERSION=`echo $VERSION | cut -d . -f 3`
LIBXML_VERSION=$VERSION
LIBXML_VERSION_NUMBER=`expr $LIBXML_MAJOR_VERSION \* 10000 + $LIBXML_MINOR_VERSION \* 100 + $LIBXML_MICRO_VERSION`
LIBXML_VERSION_EXTRA=
# libtool -version-info CURRENT:REVISION:AGE; compatible with every
# minor release of this major version (i.e. AGE == MINOR_VERSION).
LIBXML_VERSION_INFO=`expr $LIBXML_MAJOR_VERSION + $LIBXML_MINOR_VERSION`:$LIBXML_MICRO_VERSION:$LIBXML_MINOR_VERSION
AC_SUBST(LIBXML_MAJOR_VERSION)
AC_SUBST(LIBXML_MINOR_VERSION)
AC_SUBST(LIBXML_MICRO_VERSION)
AC_SUBST(LIBXML_VERSION)
AC_SUBST(LIBXML_VERSION_NUMBER)
AC_SUBST(LIBXML_VERSION_EXTRA)
AC_SUBST(LIBXML_VERSION_INFO)

case "$host" in
    *-*-cygwin* | *-*-msys* | *-*-mingw*)
        MODULE_EXTENSION=".dll"
        ;;
    *-*-hpux*)
        MODULE_EXTENSION=".sl"
        ;;
    *)
        MODULE_EXTENSION=".so"
        ;;
esac
AC_SUBST(MODULE_EXTENSION)

XML_LIBDIR='-L${libdir}'
XML_INCLUDEDIR='-I${includedir}/libxml'
XML_CFLAGS=""
XML_STATIC_CFLAGS=""
XML_PC_CFLAGS_PRIVATE=""

XML_PRIVATE_LIBS=
XML_PRIVATE_CFLAGS=
XML_PC_LIBS=
XML_PC_REQUIRES=

AX_GCC_FUNC_ATTRIBUTE([destructor])

dnl
dnl Legacy defaults
dnl
if test "$with_legacy" = "yes"; then
    if test "$with_schematron" = ""; then
        with_schematron=yes
    fi
    if test "$with_zlib" = ""; then
        with_zlib=yes
    fi
fi

dnl
dnl hard dependencies on options
dnl
if test "$with_c14n" = "yes"; then
    if test "$with_output" = "no"; then
        echo WARNING: --with-c14n overrides --without-output
    fi
    with_output=yes
    if test "$with_xpath" = "no"; then
        echo WARNING: --with-c14n overrides --without-xpath
    fi
    with_xpath=yes
fi
if test "$with_relaxng" = "yes"; then
    if test "$with_regexps" = "no"; then
        echo WARNING: --with-relaxng overrides --without-regexps
    fi
    with_regexps=yes
    if test "$with_schemas" = "no"; then
        echo WARNING: --with-relaxng overrides --without-schemas
    fi
    with_schemas=yes
fi
if test "$with_schemas" = "yes"; then
    if test "$with_pattern" = "no"; then
        echo WARNING: --with-schemas overrides --without-pattern
    fi
    with_pattern=yes
    if test "$with_regexps" = "no"; then
        echo WARNING: --with-schemas overrides --without-regexps
    fi
    with_regexps=yes
fi
if test "$with_schematron" = "yes"; then
    if test "$with_pattern" = "no"; then
        echo WARNING: --with-schematron overrides --without-pattern
    fi
    with_pattern=yes
    if test "$with_xpath" = "no"; then
        echo WARNING: --with-schematron overrides --without-xpath
    fi
    with_xpath=yes
fi
if test "$with_reader" = "yes"; then
    if test "$with_push" = "no"; then
        echo WARNING: --with-reader overrides --without-push
    fi
    with_push=yes
fi
if test "$with_writer" = "yes"; then
    if test "$with_output" = "no"; then
        echo WARNING: --with-writer overrides --without-output
    fi
    with_output=yes
    if test "$with_push" = "no"; then
        echo WARNING: --with-writer overrides --without-push
    fi
    with_push=yes
fi
if test "$with_xptr" = "yes"; then
    if test "$with_xpath" = "no"; then
        echo WARNING: --with-xptr overrides --without-xpath
    fi
    with_xpath=yes
fi
if test "$with_history" = "yes"; then
    if test "$with_readline" = "no"; then
        echo WARNING: --with-history overrides --without-readline
        with_readline=yes
    fi
    if test "$with_readline" = ""; then
        with_readline=yes
    fi
fi

if test "$with_http" != "yes" ; then
    WITH_HTTP=0
else
    echo Enabling ABI compatibility stubs for removed HTTP support
    WITH_HTTP=1
fi

if test "$with_reader" = "no" ; then
    echo Disabling the xmlReader parsing interface
    WITH_READER=0
else
    WITH_READER=1
fi

if test "$with_writer" = "no" ; then
    echo Disabling the xmlWriter saving interface
    WITH_WRITER=0
else
    WITH_WRITER=1
fi

if test "$with_pattern" = "no" ; then
    echo Disabling the xmlPattern parsing interface
    WITH_PATTERN=0
else
    WITH_PATTERN=1
fi

if test "$with_sax1" = "no" ; then
    echo Disabling the older SAX1 interface
    WITH_SAX1=0
else
    WITH_SAX1=1
fi

if test "$with_push" = "no" ; then
    echo Disabling the PUSH parser interfaces
    WITH_PUSH=0
else
    WITH_PUSH=1
fi

if test "$with_html" = "no" ; then
    echo Disabling HTML support
    WITH_HTML=0
else
    WITH_HTML=1
fi

if test "$with_valid" = "no" ; then
    echo Disabling DTD validation support
    WITH_VALID=0
else
    WITH_VALID=1
fi

if test "$with_catalog" = "no" ; then
    echo Disabling Catalog support
    WITH_CATALOG=0
else
    WITH_CATALOG=1
fi

if test "$with_xptr" = "no" ; then
    echo Disabling XPointer support
    WITH_XPTR=0
else
    WITH_XPTR=1
fi

if test "$with_c14n" = "no" ; then
    echo Disabling C14N support
    WITH_C14N=0
else
    WITH_C14N=1
fi

if test "$with_xinclude" = "no" ; then
    echo Disabling XInclude support
    WITH_XINCLUDE=0
else
    WITH_XINCLUDE=1
fi

if test "$with_schematron" = "yes" ; then
    echo "Enabling Schematron support"
    WITH_SCHEMATRON=1
else
    WITH_SCHEMATRON=0
fi

if test "$with_xpath" = "no" ; then
    echo Disabling XPATH support
    WITH_XPATH=0
else
    WITH_XPATH=1
fi

if test "$with_output" = "no" ; then
    echo Disabling serialization/saving support
    WITH_OUTPUT=0
else
    WITH_OUTPUT=1
fi

if test "$WITH_ICONV" != "1" && test "$with_iso8859x" = "no" ; then
    echo Disabling ISO8859X support
    WITH_ISO8859X=0
else
    WITH_ISO8859X=1
fi

if test "$with_relaxng" = "no" ; then
    echo "Disabling Relax-NG support"
    WITH_RELAXNG=0
else
    WITH_RELAXNG=1
fi

if test "$with_schemas" = "no" ; then
    echo "Disabling Schemas support"
    WITH_SCHEMAS=0
else
    WITH_SCHEMAS=1
fi

if test "$with_regexps" = "no" ; then
    echo Disabling Regexps support
    WITH_REGEXPS=0
else
    WITH_REGEXPS=1
fi

if test "$with_debug" = "no" ; then
    echo Disabling DEBUG support
    WITH_DEBUG=0
else
    WITH_DEBUG=1
fi

#
# ---- captive libxml component selection
#
# libxml2 used to have 34 separate --with-XXX/--without-XXX flags plus
# a tangle of hand-written dependency rules between them.  That's
# replaced here with a single --with-bxml-components=LIST option.  LIST is
# a space-separated set of component names and/or component sets:
#
#   --with-bxml-components='default'                 (the default, if omitted)
#   --with-bxml-components='min'                     start from (almost) nothing
#   --with-bxml-components='all'                     everything on
#   --with-bxml-components='legacy'                  default, plus schematron/zlib
#   --with-bxml-components='default -history icu'    default, minus history, plus icu
#
# A bare name turns a component on; "-name" or "no-name" turns it off (the
# leading "+" on a name, e.g. "+icu", is accepted too, but is just a
# synonym for the bare name -- it's optional).  Unknown names/sets are
# a hard error.  Run "configure --enable-list-bxml-components" to see the
# table.
#
# Each table row is name:default:implies:description.  "implies" is a
# comma-separated list of other components that get forced on, to a fixed
# point, whenever this one ends up on -- so turning on "relaxng" also
# turns on "regexps" and "schemas" without having to spell them out.
#
BXML_COMPONENT_TABLE='
c14n:on:output,xpath:Canonical XML 1.0 support
catalog:on::XML Catalogs support
debug:on::debugging component
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
python:off::Python bindings
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

BXML_COMPONENT_NAMES=`echo "$BXML_COMPONENT_TABLE" | awk -F: 'NF { print $1 }'`

# name -> shell-safe variable fragment ("thread-alloc" -> "thread_alloc")
bxml_var () {
    echo "$1" | tr '-' '_'
}

bxml_known () {
    for bxml_n in $BXML_COMPONENT_NAMES; do
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

# reset every component to its table default (also (re)loads implies/)
bxml_reset_all () {
    bxml_old_IFS=$IFS
    IFS='
'
    for bxml_row in $BXML_COMPONENT_TABLE; do
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
    for bxml_n in $BXML_COMPONENT_NAMES; do bxml_set "$bxml_n" off; done
}

bxml_apply_all () {
    for bxml_n in $BXML_COMPONENT_NAMES; do bxml_set "$bxml_n" on; done
}

bxml_apply_legacy () {
    bxml_reset_all
    bxml_set schematron on
    bxml_set zlib on
}

AC_ARG_WITH(bxml-components,
  [  --with-bxml-components=LIST   captive libxml components to build.  LIST is a
                        space-separated list of component names and/or
                        component sets (default, min, all, legacy);
                        prefix a name with - or no- to disable it.
                        See --enable-list-bxml-components for the full set.],
  [BXML_COMPONENTS_ARG=$withval], [BXML_COMPONENTS_ARG=default])
test "x$BXML_COMPONENTS_ARG" = "xyes" && BXML_COMPONENTS_ARG=default

AC_ARG_ENABLE(list-bxml-components,
  [  --enable-list-bxml-components   List captive libxml components/sets and exit],
  [BXML_LIST=$enableval], [BXML_LIST=no])

if test "x$BXML_LIST" != "xno"; then
    echo "bxml component sets: default min all legacy"
    echo ""
    echo "bxml components:"
    bxml_old_IFS=$IFS
    IFS='
'
    for bxml_row in $BXML_COMPONENT_TABLE; do
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

for bxml_tok in $BXML_COMPONENTS_ARG; do
    case $bxml_tok in
        default) bxml_reset_all ;;
        min)     bxml_apply_min ;;
        all)     bxml_apply_all ;;
        legacy)  bxml_apply_legacy ;;
        -*|no-*)
            bxml_name=`echo "$bxml_tok" | sed -e 's/^-//' -e 's/^no-//'`
            bxml_known "$bxml_name" || \
                AC_MSG_ERROR([unknown bxml component: $bxml_name])
            bxml_set "$bxml_name" off
            ;;
        +*)
            bxml_name=`echo "$bxml_tok" | sed -e 's/^+//'`
            bxml_known "$bxml_name" || \
                AC_MSG_ERROR([unknown bxml component: $bxml_name])
            bxml_set "$bxml_name" on
            ;;
        *)
            bxml_known "$bxml_tok" || \
                AC_MSG_ERROR([unknown bxml component or set: $bxml_tok])
            bxml_set "$bxml_tok" on
            ;;
    esac
done

# Force on, to a fixed point, every prerequisite of a component that's on.
# This is the only direction resolved automatically: turning a component
# off never reaches out and turns other components off, so a contradictory
# request (e.g. "relaxng -regexps") just re-enables regexps and says so,
# rather than silently dropping relaxng instead.
bxml_changed=yes
bxml_pass=0
while test "$bxml_changed" = yes; do
    bxml_changed=no
    bxml_pass=`expr $bxml_pass + 1`
    test $bxml_pass -gt 10 && \
        AC_MSG_ERROR([bxml component dependency resolution did not converge])
    for bxml_name in $BXML_COMPONENT_NAMES; do
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

BXML_COMPONENTS=
for bxml_name in $BXML_COMPONENT_NAMES; do
    test "`bxml_get $bxml_name`" = on && BXML_COMPONENTS="$BXML_COMPONENTS $bxml_name"
done
BXML_COMPONENTS=`echo $BXML_COMPONENTS`
AC_SUBST(BXML_COMPONENTS)
AC_MSG_RESULT([bxml components enabled:${BXML_COMPONENTS}])

# Turn each component's on/off state into a WITH_<NAME> shell variable
# (1 or 0) so the explicit AC_SUBST/AC_DEFINE_UNQUOTED/AM_CONDITIONAL
# calls below -- one block per component, matching the WITH_<NAME> and
# WITH_<NAME>_SOURCES names libbxml's own Makefile.am/xmlversion.h.in
# already expect -- can pick them up.
for bxml_name in $BXML_COMPONENT_NAMES; do
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
AC_DEFINE_UNQUOTED([WITH_DEBUG], [$WITH_DEBUG], [debugging component])
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
dnl No AC_DEFINE here: WITH_MODULES as a compiled C macro belongs to
dnl bxslt's own plugin-loading feature (set via its private xsltconfig.h,
dnl gated by WITH_XSLT_MODULES). bxml's C sources never check WITH_MODULES
dnl directly (only the @WITH_MODULES@ substitution in xmlversion.h.in and
dnl the Makefile conditionals below), so defining it here would leak bxml's
dnl own always-real component state into the shared config header and
dnl falsely satisfy bxslt's #ifdef WITH_MODULES.
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

dnl
dnl bxml's iconv support (WITH_ICONV=1) needs an actual -liconv link
dnl check, not just the boolean component flag -- on macOS/BSD, iconv()
dnl lives in libiconv, not libc.
dnl
if test "$WITH_ICONV" = "1"; then
    _libs=$LIBS
    AC_MSG_CHECKING([for iconv_open])
    AC_LINK_IFELSE([
        AC_LANG_PROGRAM([#include <iconv.h>], [iconv_open(0,0);])
    ], [
        AC_MSG_RESULT([none required])
    ], [
        LIBS="$LIBS -liconv"
        AC_LINK_IFELSE([
            AC_LANG_PROGRAM([#include <iconv.h>], [iconv_open(0,0);])
        ], [
            ICONV_LIBS="-liconv"
            AC_MSG_RESULT([-liconv])
        ], [
            AC_MSG_ERROR([libiconv not found])
        ])
    ])
    LIBS=$_libs
    XML_PRIVATE_LIBS="${XML_PRIVATE_LIBS} ${ICONV_LIBS}"
    XML_PC_LIBS="${XML_PC_LIBS} ${ICONV_LIBS}"
fi

dnl
dnl bxml's modules component (WITH_MODULES=1) needs dlopen/shl_load
dnl detection -- xmlmodule.c's platform backend is chosen at compile
dnl time by HAVE_DLOPEN/HAVE_SHLLOAD.
dnl
if test "$WITH_MODULES" = "1"; then
    _libs=$LIBS
    AC_SEARCH_LIBS([dlopen], [dl], [
        if test "$ac_cv_search_dlopen" != "none required"; then
            MODULE_LIBS=$ac_cv_search_dlopen
        fi
        AC_DEFINE([HAVE_DLOPEN], [1], [Have dlopen based dso])
    ], [
        AC_SEARCH_LIBS([shl_load], [dld], [
            if test "$ac_cv_search_shl_load" != "none required"; then
                MODULE_LIBS=$ac_cv_search_shl_load
            fi
            AC_DEFINE([HAVE_SHLLOAD], [1], [Have shl_load based dso])
        ], [
            AC_MSG_ERROR([dlopen or equivalent not found])
        ])
    ])
    LIBS=$_libs
    XML_PRIVATE_LIBS="${XML_PRIVATE_LIBS} ${MODULE_LIBS}"
    XML_PC_LIBS="${XML_PC_LIBS} ${MODULE_LIBS}"
fi

dnl
dnl State of bxml options
dnl

dnl
dnl Check for Python
dnl

AS_IF([test "x$with_python" = "xyes"], [
    AM_PATH_PYTHON
    PKG_CHECK_MODULES([PYTHON], [python-${PYTHON_VERSION}])
])

dnl
dnl Check for Doxygen
dnl

if test "$with_python" = "yes" || test "$with_docs" = "yes"; then
    AC_PATH_PROG(DOXYGEN, doxygen, [no])
    if test "$DOXYGEN" = "no"; then
        AC_MSG_ERROR([cannot find doxygen])
    fi
    WITH_DOXYGEN=1
fi
AM_CONDITIONAL([WITH_DOXYGEN], test "$WITH_DOXYGEN" = "1")


dnl
dnl Extra Python flags
dnl
PYTHON_LDFLAGS=
if test "${PYTHON}" != ""; then
    case "$host" in
        *-*-cygwin* | *-*-mingw* | *-*-msys* )
            PYTHON_LDFLAGS="-no-undefined"
            ;;
    esac
    ext_suffix=`$PYTHON -c 'import sysconfig; print(sysconfig.get_config_var("EXT_SUFFIX") or sysconfig.get_config_var("SO"))'`
    PYTHON_LDFLAGS="$PYTHON_LDFLAGS -shrext $ext_suffix"
fi
AC_SUBST(PYTHON_LDFLAGS)


dnl
dnl Checks for ICU library.
dnl
WITH_ICU=0

if test "$with_icu" != "no" && test "$with_icu" != "" ; then
    echo Enabling ICU support

    # Try pkg-config first so that static linking works.
    # If this succeeeds, we ignore the WITH_ICU directory.
    PKG_CHECK_MODULES([ICU], [icu-uc], [
        WITH_ICU=1; XML_PC_REQUIRES="${XML_PC_REQUIRES} icu-uc"
        m4_ifdef([PKG_CHECK_VAR],
            [PKG_CHECK_VAR([ICU_DEFS], [icu-uc], [DEFS])])
        if test "x$ICU_DEFS" != "x"; then
            ICU_CFLAGS="$ICU_CFLAGS $ICU_DEFS"
        fi],[:])

    if test "$WITH_ICU" = "0"; then
        ICU_CONFIG=icu-config
        if ${ICU_CONFIG} --cflags >/dev/null 2>&1
        then
            WITH_ICU=1
            ICU_CFLAGS=`${ICU_CONFIG} --cflags`
            ICU_LIBS=`${ICU_CONFIG} --ldflags`
            XML_PC_LIBS="${XML_PC_LIBS} ${ICU_LIBS}"
        else
            _cppflags="${CPPFLAGS}"
            _libs="${LIBS}"
            if test "$with_icu" != "yes" ; then
                ICU_DIR=$with_icu
                CPPFLAGS="${CPPFLAGS} -I$ICU_DIR/include"
                LIBS="${LIBS} -L$ICU_DIR/lib"
            fi

            AC_CHECK_HEADER(unicode/ucnv.h, [
                AC_CHECK_LIB([icucore], [ucnv_open], [
                    WITH_ICU=1
                    ICU_LIBS=-licucore
                    if test "$ICU_DIR" != ""; then
                        ICU_CFLAGS="-I$ICU_DIR/include"
                        ICU_LIBS="-L$ICU_DIR/lib $ICU_LIBS"
                    fi])])
                    XML_PC_LIBS="${XML_PC_LIBS} ${ICU_LIBS}"
            CPPFLAGS=$_cppflags
            LIBS=$_libs
        fi
    fi

    if test "$WITH_ICU" = "0"; then
        AC_MSG_ERROR([ICU not found])
    fi

    XML_PRIVATE_CFLAGS="${XML_PRIVATE_CFLAGS} ${ICU_CFLAGS}"
    XML_PRIVATE_LIBS="${XML_PRIVATE_LIBS} ${ICU_LIBS}"
fi
AC_SUBST(WITH_ICU)

dnl
dnl Crypto libraries
dnl
case "$host" in
    *-*-mingw*)
        CRYPTO_LIBS="-lbcrypt"
        XML_PRIVATE_LIBS="${XML_PRIVATE_LIBS} ${CRYPTO_LIBS}"
        XML_PC_LIBS="${XML_PC_LIBS} ${CRYPTO_LIBS}"
        ;;
esac

XML_LIBS="-lbxml"
XML_PC_LIBS="${XML_PC_LIBS} ${LIBM}"
XML_PRIVATE_LIBS="${XML_PRIVATE_LIBS} ${LIBM}"

dnl When static-only:
dnl * Duplicate xml-config static --libs into --dynamic.
dnl * Fold pkg-config private fields into main fields.
if test "x$enable_shared" = "xno"; then
  XML_PRIVATE_LIBS_NO_SHARED="${XML_PRIVATE_LIBS}"
  XML_PC_PRIVATE=
  XML_PC_LIBS_PRIVATE=
  BUILD_SHARED_LIBS=0
else
  XML_PRIVATE_LIBS_NO_SHARED=
  XML_PC_PRIVATE=".private"
  XML_PC_LIBS_PRIVATE="
Libs.private:"
  BUILD_SHARED_LIBS=1
fi
AC_SUBST(XML_PRIVATE_LIBS_NO_SHARED)
AC_SUBST(XML_PC_PRIVATE)
AC_SUBST(XML_PC_LIBS_PRIVATE)
AC_SUBST(XML_PC_CFLAGS_PRIVATE)
AC_SUBST(BUILD_SHARED_LIBS)
AM_SUBST_NOTMAKE(XML_PRIVATE_LIBS_NO_SHARED)
AM_SUBST_NOTMAKE(XML_PC_PRIVATE)
AM_SUBST_NOTMAKE(XML_PC_LIBS_PRIVATE)
AM_SUBST_NOTMAKE(XML_PC_CFLAGS_PRIVATE)
AM_SUBST_NOTMAKE(BUILD_SHARED_LIBS)

AC_SUBST(XML_PC_LIBS)
AC_SUBST(XML_PC_REQUIRES)
AM_SUBST_NOTMAKE(XML_PC_LIBS)
AM_SUBST_NOTMAKE(XML_PC_REQUIRES)

AC_SUBST(AM_CFLAGS)
AC_SUBST(AM_LDFLAGS)
AC_SUBST(XML_CFLAGS)
AC_SUBST(XML_STATIC_CFLAGS)

AC_SUBST(XML_LIBDIR)
AC_SUBST(XML_LIBS)
AC_SUBST(XML_PRIVATE_LIBS)
AC_SUBST(XML_PRIVATE_CFLAGS)
AC_SUBST(XML_INCLUDEDIR)

# for bxml-config.cmake.in
AX_RECURSIVE_EVAL(["$bindir"], [INSTALL_BINDIR])
AX_RECURSIVE_EVAL(["$includedir"], [INSTALL_INCLUDEDIR])
AX_RECURSIVE_EVAL(["$libdir"], [INSTALL_LIBDIR])
AC_SUBST(INSTALL_BINDIR)
AC_SUBST(INSTALL_INCLUDEDIR)
AC_SUBST(INSTALL_LIBDIR)
AM_SUBST_NOTMAKE(INSTALL_BINDIR)
AM_SUBST_NOTMAKE(INSTALL_INCLUDEDIR)
AM_SUBST_NOTMAKE(INSTALL_LIBDIR)

AX_RECURSIVE_EVAL(["$sysconfdir"], [XML_SYSCONFDIR])
AC_DEFINE_UNQUOTED([XML_SYSCONFDIR], ["$XML_SYSCONFDIR"],
                   [System configuration directory (/etc)])

#
# --- Logic for bxslt (captive libxslt/libexslt)
#

# bxslt is versioned identically to libslax itself ($VERSION, from the
# AC_INIT above) rather than to the upstream libxslt/libexslt releases
# it was vendored from.
LIBXSLT_MAJOR_VERSION=`echo $VERSION | cut -d . -f 1`
LIBXSLT_MINOR_VERSION=`echo $VERSION | cut -d . -f 2`
LIBXSLT_MICRO_VERSION=`echo $VERSION | cut -d . -f 3`
LIBXSLT_VERSION=$VERSION
LIBXSLT_VERSION_NUMBER=`expr $LIBXSLT_MAJOR_VERSION \* 10000 + $LIBXSLT_MINOR_VERSION \* 100 + $LIBXSLT_MICRO_VERSION`
LIBXSLT_VERSION_EXTRA=
LIBXSLT_MAJOR_MINOR_VERSION=$LIBXSLT_MAJOR_VERSION.$LIBXSLT_MINOR_VERSION
# libtool -version-info CURRENT:REVISION:AGE; compatible with every
# minor release of this major version (i.e. AGE == MINOR_VERSION).
LIBXSLT_VERSION_INFO=`expr $LIBXSLT_MAJOR_VERSION + $LIBXSLT_MINOR_VERSION`:$LIBXSLT_MICRO_VERSION:$LIBXSLT_MINOR_VERSION
AC_SUBST(LIBXSLT_MAJOR_VERSION)
AC_SUBST(LIBXSLT_MINOR_VERSION)
AC_SUBST(LIBXSLT_MICRO_VERSION)
AC_SUBST(LIBXSLT_VERSION)
AC_SUBST(LIBXSLT_VERSION_NUMBER)
AC_SUBST(LIBXSLT_VERSION_EXTRA)
AC_SUBST(LIBXSLT_MAJOR_MINOR_VERSION)
AC_SUBST(LIBXSLT_VERSION_INFO)

LIBEXSLT_MAJOR_VERSION=`echo $VERSION | cut -d . -f 1`
LIBEXSLT_MINOR_VERSION=`echo $VERSION | cut -d . -f 2`
LIBEXSLT_MICRO_VERSION=`echo $VERSION | cut -d . -f 3`
LIBEXSLT_VERSION=$VERSION
LIBEXSLT_VERSION_NUMBER=`expr $LIBEXSLT_MAJOR_VERSION \* 10000 + $LIBEXSLT_MINOR_VERSION \* 100 + $LIBEXSLT_MICRO_VERSION`
LIBEXSLT_VERSION_EXTRA=
LIBEXSLT_VERSION_INFO=`expr $LIBEXSLT_MAJOR_VERSION + $LIBEXSLT_MINOR_VERSION`:$LIBEXSLT_MICRO_VERSION:$LIBEXSLT_MINOR_VERSION
AC_SUBST(LIBEXSLT_MAJOR_VERSION)
AC_SUBST(LIBEXSLT_MINOR_VERSION)
AC_SUBST(LIBEXSLT_MICRO_VERSION)
AC_SUBST(LIBEXSLT_VERSION)
AC_SUBST(LIBEXSLT_VERSION_NUMBER)
AC_SUBST(LIBEXSLT_VERSION_EXTRA)
AC_SUBST(LIBEXSLT_VERSION_INFO)

dnl
dnl if the system support linker version scripts for symbol versioning
dnl then add it (bxslt uses this for libxslt.syms)
dnl
AC_MSG_CHECKING([how to pass version script to the linker ($LD)])
VERSION_SCRIPT_FLAGS=none
if $LD --help 2>&1 | grep "version-script" >/dev/null 2>/dev/null; then
    dnl lld 16 defaults to --no-undefined-version but the version script
    dnl can contain symbols disabled by configuration options.
    VERSION_SCRIPT_FLAGS=''
    AX_APPEND_LINK_FLAGS([-Wl,--undefined-version], [VERSION_SCRIPT_FLAGS])
    AX_APPEND_FLAG([-Wl,--version-script=], [VERSION_SCRIPT_FLAGS])
elif $LD --help 2>&1 | grep "M mapfile" >/dev/null 2>/dev/null; then
    VERSION_SCRIPT_FLAGS="-Wl,-M -Wl,"
fi
AC_MSG_RESULT([$VERSION_SCRIPT_FLAGS])
AC_SUBST(VERSION_SCRIPT_FLAGS)
AM_CONDITIONAL([USE_VERSION_SCRIPT], [test "$VERSION_SCRIPT_FLAGS" != none])

dnl
dnl bxslt needs real pthread detection (independent of bxml's own
dnl WITH_THREADS component flag above, which is a boolean intent only)
dnl to populate THREAD_LIBS for its testThreads program and for
dnl tests/bxml's runtest.
dnl
case $host in
  *-mingw*) ;;
  *)
    THREAD_LIBS=""
    AC_CHECK_HEADER(pthread.h,
        AC_CHECK_LIB(pthread, pthread_join,[
           AC_DEFINE([HAVE_LIBPTHREAD], [1], [Define if pthread library is there (-lpthread)])
           AC_DEFINE([HAVE_PTHREAD_H], [1], [Define if <pthread.h> is there])
           THREAD_LIBS="-lpthread"]))
    ;;
esac
AC_SUBST(THREAD_LIBS)

dnl
dnl Detect supported locale
dnl
AC_CHECK_HEADERS([locale.h xlocale.h])
AC_CHECK_FUNCS([strxfrm_l])

dnl
dnl Math detection
dnl
AC_CHECK_HEADERS(sys/timeb.h sys/stat.h sys/select.h)
AC_CHECK_FUNCS(stat _stat)
AC_CHECK_FUNCS(gettimeofday)
AC_CHECK_FUNCS(localtime_r gmtime_r ftime)

dnl Checking the standard string functions availability
AC_CHECK_FUNCS(snprintf vsnprintf,, NEED_TRIO=1)

dnl Checking for POSIX timers
AC_CHECK_FUNCS(clock_gettime, [], [
  AC_CHECK_LIB(rt, clock_gettime, [
    AC_DEFINE(HAVE_CLOCK_GETTIME, 1)
    EXTRA_LIBS="$EXTRA_LIBS -lrt"
  ])
])
AC_SUBST(EXTRA_LIBS)

dnl
dnl Check for trio string functions
dnl
if test "${NEED_TRIO}" = "1" ; then
    echo Reusing trio library for string functions
    WITH_TRIO=1
else
    WITH_TRIO=0
fi
AC_SUBST(WITH_TRIO)

dnl
dnl Perl is just needed for generating some data for XSLTmark
dnl
AC_CHECK_PROG(PERL, perl, perl, false)
AM_CONDITIONAL(WITH_PERL, test "$PERL" != "false")

AC_PATH_PROG(XMLLINT, xmllint, /usr/bin/xmllint)
AC_PATH_PROG(XSLTPROC, xsltproc, /usr/bin/xsltproc)

#
# ---- captive libxslt component selection
#
# Mirrors bxml's --with-bxml-components mechanism above: a single
# --with-bxslt-components=LIST option replaces the separate
# --with-crypto/--with-debugger/--with-profiler/--with-debug/--with-plugins/
# --with-python switches libxslt used to have.
#
# Two of these are renamed from their upstream libxslt names to avoid
# colliding with bxml's own same-named components (both vendored libraries
# now share one configure.ac):
#   plugins/modules -> xslt-modules (upstream WITH_MODULES; bxml already
#                       owns WITH_MODULES for its own dynamic-modules support)
#   python           -> xslt-python (upstream WITH_PYTHON; bxml already
#                       owns WITH_PYTHON for its own Python bindings)
# bxslt/libxslt/xsltconfig.h.in and the bxslt Makefile.am files that used
# the bare names have been updated to match.
#
BXSLT_COMPONENT_TABLE='
crypto:off::Add crypto support to exslt
debugger:off::Add debugging (breakpoint/inspection) support
profiler:on::Add profiling support
xslt-debug:off::Enable XSLT verbose debug reporting (xsltproc -v)
xslt-modules:off::Add plugin extension support
xslt-python:on::Build Python bindings
'

BXSLT_COMPONENT_NAMES=`echo "$BXSLT_COMPONENT_TABLE" | awk -F: 'NF { print $1 }'`

bxslt_var () {
    echo "$1" | tr '-' '_'
}

bxslt_known () {
    for bxslt_n in $BXSLT_COMPONENT_NAMES; do
        test "$bxslt_n" = "$1" && return 0
    done
    return 1
}

bxslt_set () { # name value
    eval "bxslt_mod_`bxslt_var $1`=$2"
}

bxslt_get () { # name
    eval "echo \$bxslt_mod_`bxslt_var $1`"
}

bxslt_reset_all () {
    bxslt_old_IFS=$IFS
    IFS='
'
    for bxslt_row in $BXSLT_COMPONENT_TABLE; do
        test -z "$bxslt_row" && continue
        bxslt_name=`echo "$bxslt_row" | cut -d: -f1`
        bxslt_default=`echo "$bxslt_row" | cut -d: -f2`
        bxslt_implies=`echo "$bxslt_row" | cut -d: -f3`
        eval "bxslt_implies_`bxslt_var $bxslt_name`=\"$bxslt_implies\""
        bxslt_set "$bxslt_name" "$bxslt_default"
    done
    IFS=$bxslt_old_IFS
}
bxslt_reset_all

bxslt_apply_min () {
    for bxslt_n in $BXSLT_COMPONENT_NAMES; do bxslt_set "$bxslt_n" off; done
}

bxslt_apply_all () {
    for bxslt_n in $BXSLT_COMPONENT_NAMES; do bxslt_set "$bxslt_n" on; done
}

AC_ARG_WITH(bxslt-components,
  [  --with-bxslt-components=LIST  captive libxslt/libexslt components to build.
                        LIST is a space-separated list of component names
                        and/or component sets (default, min, all); prefix
                        a name with - or no- to disable it.
                        See --enable-list-bxslt-components for the full set.],
  [BXSLT_COMPONENTS_ARG=$withval], [BXSLT_COMPONENTS_ARG=default])
test "x$BXSLT_COMPONENTS_ARG" = "xyes" && BXSLT_COMPONENTS_ARG=default

AC_ARG_ENABLE(list-bxslt-components,
  [  --enable-list-bxslt-components  List captive libxslt components/sets and exit],
  [BXSLT_LIST=$enableval], [BXSLT_LIST=no])

if test "x$BXSLT_LIST" != "xno"; then
    echo "bxslt component sets: default min all"
    echo ""
    echo "bxslt components:"
    bxslt_old_IFS=$IFS
    IFS='
'
    for bxslt_row in $BXSLT_COMPONENT_TABLE; do
        test -z "$bxslt_row" && continue
        bxslt_name=`echo "$bxslt_row" | cut -d: -f1`
        bxslt_default=`echo "$bxslt_row" | cut -d: -f2`
        bxslt_desc=`echo "$bxslt_row" | cut -d: -f4`
        printf '  %-14s %-4s %s\n' "$bxslt_name" "$bxslt_default" "$bxslt_desc"
    done
    IFS=$bxslt_old_IFS
    exit 0
fi

for bxslt_tok in $BXSLT_COMPONENTS_ARG; do
    case $bxslt_tok in
        default) bxslt_reset_all ;;
        min)     bxslt_apply_min ;;
        all)     bxslt_apply_all ;;
        -*|no-*)
            bxslt_name=`echo "$bxslt_tok" | sed -e 's/^-//' -e 's/^no-//'`
            bxslt_known "$bxslt_name" || \
                AC_MSG_ERROR([unknown bxslt component: $bxslt_name])
            bxslt_set "$bxslt_name" off
            ;;
        +*)
            bxslt_name=`echo "$bxslt_tok" | sed -e 's/^+//'`
            bxslt_known "$bxslt_name" || \
                AC_MSG_ERROR([unknown bxslt component: $bxslt_name])
            bxslt_set "$bxslt_name" on
            ;;
        *)
            bxslt_known "$bxslt_tok" || \
                AC_MSG_ERROR([unknown bxslt component or set: $bxslt_tok])
            bxslt_set "$bxslt_tok" on
            ;;
    esac
done

BXSLT_COMPONENTS=
for bxslt_name in $BXSLT_COMPONENT_NAMES; do
    test "`bxslt_get $bxslt_name`" = on && BXSLT_COMPONENTS="$BXSLT_COMPONENTS $bxslt_name"
done
BXSLT_COMPONENTS=`echo $BXSLT_COMPONENTS`
AC_SUBST(BXSLT_COMPONENTS)
AC_MSG_RESULT([bxslt components enabled:${BXSLT_COMPONENTS}])

for bxslt_name in $BXSLT_COMPONENT_NAMES; do
    bxslt_upper=`bxslt_var $bxslt_name | tr a-z A-Z`
    if test "`bxslt_get $bxslt_name`" = on; then
        eval "WITH_$bxslt_upper=1"
    else
        eval "WITH_$bxslt_upper=0"
    fi
done

AC_SUBST(WITH_CRYPTO)
AC_DEFINE_UNQUOTED([WITH_CRYPTO], [$WITH_CRYPTO], [crypto support for exslt])
AM_CONDITIONAL([WITH_CRYPTO], [test "$WITH_CRYPTO" = "1"])

AC_SUBST(WITH_DEBUGGER)
AC_DEFINE_UNQUOTED([WITH_DEBUGGER], [$WITH_DEBUGGER], [XSLT debugger support])
AM_CONDITIONAL([WITH_DEBUGGER], [test "$WITH_DEBUGGER" = "1"])

AC_SUBST(WITH_PROFILER)
AC_DEFINE_UNQUOTED([WITH_PROFILER], [$WITH_PROFILER], [XSLT profiler support])
AM_CONDITIONAL([WITH_PROFILER], [test "$WITH_PROFILER" = "1"])

AC_SUBST(WITH_XSLT_DEBUG)
AC_DEFINE_UNQUOTED([WITH_XSLT_DEBUG], [$WITH_XSLT_DEBUG], [XSLT verbose debug reporting])
AM_CONDITIONAL([WITH_XSLT_DEBUG], [test "$WITH_XSLT_DEBUG" = "1"])

AC_SUBST(WITH_XSLT_MODULES)
AC_DEFINE_UNQUOTED([WITH_XSLT_MODULES], [$WITH_XSLT_MODULES], [XSLT plugin extension support])
AM_CONDITIONAL([WITH_XSLT_MODULES], [test "$WITH_XSLT_MODULES" = "1"])

AC_SUBST(WITH_XSLT_PYTHON)
AC_DEFINE_UNQUOTED([WITH_XSLT_PYTHON], [$WITH_XSLT_PYTHON], [build libxslt Python bindings])
AM_CONDITIONAL([WITH_XSLT_PYTHON], [test "$WITH_XSLT_PYTHON" = "1"])

dnl
dnl bxslt's crypto component (WITH_CRYPTO=1) needs an actual libgcrypt --
dnl if it's not found, fall the component back off rather than hard-error,
dnl matching upstream libxslt's own tolerant behavior for this one.
dnl
CRYPTO_TESTDIR=
if test "$WITH_CRYPTO" = "1"; then
    case $host in
      *-mingw*)
        dnl native crypto-API for mingw* hosts
        CRYPTO_TESTDIR=crypto
        ;;
      *)
        AC_PATH_TOOL(LIBGCRYPT_CONFIG, libgcrypt-config, no)
        if test "$LIBGCRYPT_CONFIG" != "no" ; then
            LIBGCRYPT_VERSION=`$LIBGCRYPT_CONFIG --version`
            if test VERSION_TO_NUMBER(echo $LIBGCRYPT_VERSION) -lt VERSION_TO_NUMBER(echo "1.1.42")
            then
                AC_MSG_WARN([gcrypt library version < 1.1.42 - Crypto extensions will not be available.])
                WITH_CRYPTO=0
            else
                LIBGCRYPT_CFLAGS=`$LIBGCRYPT_CONFIG --cflags`
                LIBGCRYPT_LIBS=`$LIBGCRYPT_CONFIG --libs`
                AC_DEFINE(HAVE_GCRYPT, 1, [Define if gcrypt library is available.])
                CRYPTO_TESTDIR=crypto
            fi
        else
            PKG_CHECK_MODULES([LIBGCRYPT], [libgcrypt >= 1.1.42], [
                AC_DEFINE(HAVE_GCRYPT, 1, [Define if gcrypt library is available.])
                CRYPTO_TESTDIR=crypto
            ], [
                AC_MSG_WARN([Crypto extensions will not be available. Install libgcrypt >= 1.1.42 and reconfigure to make available.])
                WITH_CRYPTO=0
            ])
        fi
        ;;
    esac
fi
AC_SUBST(CRYPTO_TESTDIR)
AC_SUBST(LIBGCRYPT_CFLAGS)
AC_SUBST(LIBGCRYPT_LIBS)

dnl
dnl bxslt's xslt-modules component (WITH_XSLT_MODULES=1) needs dlopen
dnl detection, same as bxml's own modules component above -- named
dnl separately (XSLT_MODULE_LIBS) so the two don't stomp on each other.
dnl
LIBXSLT_DEFAULT_PLUGINS_PATH=
if test "$WITH_XSLT_MODULES" = "1"; then
    case "$host" in
        *-*-cygwin* | *-*-mingw* | *-*-msys* )
            AC_DEFINE([MODULE_EXTENSION], [".dll"], [XSLT plugin file extension])
            ;;
        *)
            AC_DEFINE([MODULE_EXTENSION], [".so"], [XSLT plugin file extension])
            ;;
    esac
    case "$host" in
        *-*-mingw*) ;;
        *)
            _libs=$LIBS
            AC_SEARCH_LIBS([dlopen], [dl], [
                if test "$ac_cv_search_dlopen" != "none required"; then
                    XSLT_MODULE_LIBS=$ac_cv_search_dlopen
                fi
            ], [
                AC_MSG_ERROR([dlopen or equivalent not found])
            ])
            LIBS=$_libs
            ;;
    esac
    expanded_libdir=$(
        test "x$prefix" = xNONE && prefix="$ac_default_prefix"
        test "x$exec_prefix" = xNONE && exec_prefix="$prefix"
        while test "$libdir_old" != "$libdir"; do
          libdir_old="$libdir"
          eval libdir="$libdir"
        done
        echo "$libdir"
    )
    LIBXSLT_DEFAULT_PLUGINS_PATH="$expanded_libdir/libxslt-plugins"
fi
AC_SUBST(LIBXSLT_DEFAULT_PLUGINS_PATH)

dnl
dnl bxslt's xslt-python component (WITH_XSLT_PYTHON=1) needs the Python
dnl dev environment, same as bxml's own python component.
dnl
PYTHON_LDFLAGS=
if test "$WITH_XSLT_PYTHON" = "1"; then
    AM_PATH_PYTHON
    PKG_CHECK_MODULES([PYTHON], [python-${PYTHON_VERSION}])
    case "$host" in
        *-*-cygwin* | *-*-mingw* | *-*-msys* )
            PYTHON_LDFLAGS="-no-undefined -shrext .pyd"
            ;;
    esac
fi
AC_SUBST(PYTHON_LDFLAGS)

XSLT_LIBDIR='-L${libdir}'
XSLT_INCLUDEDIR='-I${includedir}'
XSLT_LIBS="-lbxslt"
XSLT_PRIVATE_LIBS="$XSLT_MODULE_LIBS $LIBM"
AC_SUBST(XSLT_LIBDIR)
AC_SUBST(XSLT_INCLUDEDIR)
AC_SUBST(XSLT_LIBS)
AC_SUBST(XSLT_PRIVATE_LIBS)

LIBXSLT_CFLAGS=
LIBEXSLT_CFLAGS=
case ${host} in
    *-*-mingw* | *-*-cygwin* | *-*-msvc* )
        if test "x$enable_shared" = "xno"; then
           LIBXSLT_CFLAGS="$LIBXSLT_CFLAGS -DLIBXSLT_STATIC"
           LIBEXSLT_CFLAGS="$LIBEXSLT_CFLAGS -DLIBEXSLT_STATIC"
        fi
        ;;
esac
AC_SUBST(LIBXSLT_CFLAGS)
AC_SUBST(LIBEXSLT_CFLAGS)

EXSLT_LIBDIR='-L${libdir}'
EXSLT_INCLUDEDIR='-I${includedir}'
EXSLT_PRIVATE_LIBS="$XSLT_PRIVATE_LIBS $LIBGCRYPT_LIBS"
AC_SUBST(EXSLT_LIBDIR)
AC_SUBST(EXSLT_INCLUDEDIR)
AC_SUBST(EXSLT_PRIVATE_LIBS)

dnl for the bxslt spec file
BXSLT_RELDATE=`date +'%a %b %e %Y'`
AC_SUBST(BXSLT_RELDATE)

dnl
dnl ---- start of noise
dnl

#
# ---- captive bxml (libxml2)
#

dnl bxml is always our own captive source tree (bxml/), never an
dnl installed/external libxml2 — so there's nothing to search for.
dnl Point straight at its build-tree headers and library.
AC_SUBST(LIBXML_REQUIRED_VERSION)
LIBXML_CFLAGS='-I$(top_builddir)/libbxml/include -I$(top_srcdir)/libbxml/include'
LIBXML_LIBS='$(top_builddir)/libbxml/libbxml.la'
AC_SUBST(LIBXML_CFLAGS)
AC_SUBST(LIBXML_LIBS)

#
# ---- captive bxslt (libxslt/libexslt)
#

dnl Same story for bxslt: always our own captive source tree (bxslt/),
dnl so link/include paths point straight into the build tree instead of
dnl being discovered via an installed xslt-config. LIBXSLT_LIBS carries
dnl both libbxslt.la and libexslt.la, so callers should not also add a
dnl bare -lexslt.
AC_SUBST(LIBXSLT_REQUIRED_VERSION)
LIBXSLT_CFLAGS="$LIBXSLT_CFLAGS "'-I$(top_builddir)/libbxslt -I$(top_srcdir)/libbxslt -I$(top_builddir)/libbxml/include -I$(top_srcdir)/libbxml/include'
LIBXSLT_LIBS='$(top_builddir)/libbxslt/libxslt/libbxslt.la $(top_builddir)/libbxslt/libexslt/libexslt.la $(top_builddir)/libbxml/libbxml.la'
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
  if test "$extra" != "" -a "$extra" != "main"
  then
      LIBSLAX_VERSION_EXTRA="-git-$extra"
  fi
fi

LIBSLAX_VERSION=$PACKAGE_VERSION
LIBSLAX_VERSION_NUMBER=VERSION_TO_NUMBER(echo $PACKAGE_VERSION)
AC_SUBST(LIBSLAX_VERSION)
AC_SUBST(LIBSLAX_VERSION_NUMBER)
AC_SUBST(LIBSLAX_VERSION_EXTRA)

dnl A single AC_CONFIG_HEADERS call, one shared template
dnl (libslax/slaxconfig.h.in): autoheader only ever generates a template for
dnl the first HEADER name it sees, so libbxml/libbxslt reuse libslax's template
dnl explicitly via the "output:input" form rather than each getting their
dnl own (and their own autoheader-regeneration headaches).
AC_CONFIG_HEADERS([
  libslax/slaxconfig.h
  bxml/libbxmlconfig.h:libslax/slaxconfig.h.in
  bxslt/libbxsltconfig.h:libslax/slaxconfig.h.in
])
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
  tests/bxml/Makefile
  tests/core/Makefile
  tests/errors/Makefile
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

# keep on one line for cygwin c.f. #130896
AC_CONFIG_FILES([
    libbxml/Makefile
    libbxml/include/Makefile
    libbxml/include/libxml/Makefile
    libbxml/include/private/Makefile
    libbxml/doc/Makefile
    libbxml/example/Makefile
    libbxml/fuzz/Makefile
    libbxml/python/Makefile
    libbxml/python/tests/Makefile
    libbxml/include/libxml/xmlversion.h
    libbxml/libxml-2.0.pc
])
AC_CONFIG_FILES([libbxml/python/setup.py], [chmod +x libbxml/python/setup.py])
AC_CONFIG_FILES([libbxml/bxml-config], [chmod +x libbxml/bxml-config])

AC_CONFIG_FILES([
    libbxslt/Makefile
    libbxslt/libxslt.pc
    libbxslt/libexslt.pc
    libbxslt/libxslt/Makefile
    libbxslt/libxslt/xsltconfig.h
    libbxslt/libexslt/Makefile
    libbxslt/libexslt/exsltconfig.h
    libbxslt/xsltproc/Makefile
    libbxslt/python/Makefile
    libbxslt/python/tests/Makefile
    libbxslt/doc/Makefile
    libbxslt/doc/devhelp/Makefile
    libbxslt/doc/EXSLT/devhelp/Makefile
    tests/bxslt/Makefile
    tests/bxslt/xmlspec/Makefile
    tests/bxslt/multiple/Makefile
    tests/bxslt/xinclude/Makefile
    tests/bxslt/XSLTMark/Makefile
    tests/bxslt/docbook/Makefile
    tests/bxslt/fuzz/Makefile
])
AC_CONFIG_FILES([libbxslt/python/setup.py], [chmod +x libbxslt/python/setup.py])
AC_CONFIG_FILES([libbxslt/bxslt-config], [chmod +x libbxslt/bxslt-config])

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

  compiler:         ${CC} (gcc: ${HAVE_GCC:-no}) (clang: ${HAVE_CLANG:-no})
  compiler flags:   ${CFLAGS}
  library types:    Shared=${enable_shared}, Static=${enable_static}
  libbxml version:   ${LIBXML_VERSION} (captive)
  libbxslt cflags:   ${LIBXSLT_CFLAGS}
  libbxml libs:      ${LIBXML_LIBS}
  libbxslt version:  ${LIBXSLT_VERSION} (captive)
  libbxslt cflags:   ${LIBXSLT_CFLAGS}
  libbxslt libs:     ${LIBXSLT_LIBS}
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
  clang LTO:        ${ENABLE_LTO:-no}
])
