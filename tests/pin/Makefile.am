#
# $Id$
#
# Copyright 2014-2016, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

if SLAX_WARNINGS_HIGH
SLAX_WARNINGS = HIGH
endif
if HAVE_GCC
GCC_WARNINGS = yes
endif
include ${top_srcdir}/warnings.mk

AM_CFLAGS = \
    -I${top_srcdir} \
    -I${top_builddir} \
    -I${top_builddir}/parrotdb \
    -I${top_builddir}/libpin \
    ${LIBXO_CFLAGS} \
    ${LIBXML_CFLAGS} \
    ${WARNINGS}

# Ick: maintained by hand!
TEST_CASES = \
pin_01.c \
pin_02.c \
pin_03.c \
pin_04.c \
pin_05.c \
pin_06.c \
pin_07.c \
pin_08.c \
pin_09.c

pin_01_test_SOURCES = pin_01.c
pin_02_test_SOURCES = pin_02.c
pin_03_test_SOURCES = pin_03.c
pin_04_test_SOURCES = pin_04.c
pin_05_test_SOURCES = pin_05.c
pin_06_test_SOURCES = pin_06.c
pin_07_test_SOURCES = pin_07.c
pin_08_test_SOURCES = pin_08.c
pin_09_test_SOURCES = pin_09.c

# pin04 needs parrotdb; pin01 only needs libpin
pin_04_test_LDADD = \
    ${top_builddir}/libpsu/libpsu.la \
    ${top_builddir}/parrotdb/libparrotdb.la

# pin05 uses pin_filter_create which pulls in xo_filter symbols at runtime
pin_05_test_LDADD = \
    ${top_builddir}/libpsu/libpsu.la \
    ${top_builddir}/libpin/libpin.la \
    ${LIBXO_LIBS}

# pin06 also calls xmlReadFile directly, so it needs libxml2
pin_06_test_LDADD = \
    ${top_builddir}/libpsu/libpsu.la \
    ${top_builddir}/libpin/libpin.la \
    ${LIBXO_LIBS} \
    ${LIBXML_LIBS}

# pin07 likewise calls xmlReadFile directly
pin_07_test_LDADD = \
    ${top_builddir}/libpsu/libpsu.la \
    ${top_builddir}/libpin/libpin.la \
    ${LIBXO_LIBS} \
    ${LIBXML_LIBS}

# pin08 likewise calls xmlReadFile directly
pin_08_test_LDADD = \
    ${top_builddir}/libpsu/libpsu.la \
    ${top_builddir}/libpin/libpin.la \
    ${LIBXO_LIBS} \
    ${LIBXML_LIBS}

# pin09 calls slaxEnable(), the one real one-time application-startup
# hook, which registers libslax's pin_dyn_ops_t provider with libpin
# (slaxDynSetPinOps()); unlike pin01-pin08, it needs libslax itself and
# libslax's own libxslt/libexslt dependencies
pin_09_test_LDADD = \
    ${top_builddir}/libpsu/libpsu.la \
    ${top_builddir}/libpin/libpin.la \
    ${top_builddir}/libslax/libslax.la \
    ${LIBXO_LIBS} \
    ${LIBXML_LIBS} \
    ${LIBXSLT_LIBS} -lexslt

# TEST_CASES := $(shell cd ${srcdir} ; echo *.c )
SAVEDDATA := $(shell cd ${srcdir} ; echo saved/pin_*.out saved/pin_*.err)

TEST_FILES = ${TEST_CASES:.c=.test}
noinst_PROGRAMS = ${TEST_FILES}

LDADD = \
    ${top_builddir}/libpsu/libpsu.la \
    ${top_builddir}/libpin/libpin.la

EXTRA_DIST = \
    ${TEST_CASES} \
    script.xml \
    pin_03.01.in \
    pin_04.01.in \
    pin_04.02.in \
    pin_05.01.in \
    pin_05.02.in \
    pin_05.03.in \
    pin_06.01.in \
    pin_06.01.xml \
    pin_06.02.in \
    pin_06.02.xml \
    pin_07.01.in \
    pin_07.01.xml \
    pin_08.01.in \
    pin_08.01.xml \
    pin_08.02.in \
    pin_08.02.xml \
    pin_08.03.in \
    pin_08.03.xml \
    pin_08.04.in \
    pin_08.04.xml \
    pin_08.05.in \
    pin_08.05.xml \
    pin_08.06.in \
    pin_08.06.xml \
    pin_08.07.in \
    pin_08.07.xml \
    pin_08.08.in \
    pin_08.08.xml \
    pin_08.09.in \
    pin_08.09.xml \
    pin_08.10.in \
    pin_08.10.xml \
    pin_08.11.in \
    pin_08.11.xml \
    pin_08.12.in \
    pin_08.12.xml \
    pin_08.13.in \
    pin_08.13.xml \
    pin_08.14.in \
    pin_08.14.xml \
    pin_08.15.in \
    pin_08.15.xml \
    pin_08.16.in \
    pin_08.16.xml \
    pin_08.17.in \
    pin_08.17.xml \
    pin_08.18.in \
    pin_08.18.xml \
    pin_08.19.in \
    pin_08.19.xml \
    pin_08.20.in \
    pin_08.20.xml \
    pin_08.21.in \
    pin_08.21.xml \
    pin_08.22.in \
    pin_08.22.xml \
    pin_08.23.in \
    pin_08.23.xml \
    pin_08.24.in \
    pin_08.24.xml \
    pin_08.25.in \
    pin_08.25.xml \
    pin_08.26.in \
    pin_08.26.xml \
    pin_08.27.in \
    pin_08.27.xml \
    pin_08.28.in \
    pin_08.28.xml \
    pin_08.29.in \
    pin_08.29.xml \
    pin_08.30.in \
    pin_08.30.xml \
    pin_08.31.in \
    pin_08.31.xml \
    pin_08.32.in \
    pin_08.32.xml \
    pin_08.33.in \
    pin_08.33.xml \
    pin_08.34.in \
    pin_08.34.xml \
    pin_08.35.in \
    pin_08.35.xml \
    pin_08.36.in \
    pin_08.36.xml \
    pin_08.37.in \
    pin_08.37.xml \
    pin_08.38.in \
    pin_08.38.xml \
    pin_08.39.in \
    pin_08.39.xml \
    pin_08.40.in \
    pin_08.40.xml \
    pin_08.41.in \
    pin_08.41.xml \
    pin_08.42.in \
    pin_08.42.xml \
    pin_08.43.in \
    pin_08.43.xml \
    pin_08.44.in \
    pin_08.44.xml \
    pin_08.45.in \
    pin_08.45.xml \
    pin_08.46.in \
    pin_08.46.xml \
    pin_08.47.in \
    pin_08.47.xml \
    pin_08.47_inc.xsl \
    pin_08.48.in \
    pin_08.48.xml \
    pin_08.48_imp.xsl \
    pin_08.49.in \
    pin_08.49.xml \
    pin_08.50.in \
    pin_08.50.xml \
    pin_08.51.in \
    pin_08.51.xml \
    pin_08.52.in \
    pin_08.53.in \
    pin_08.53.xml \
    pin_08.54.in \
    pin_08.54.xml \
    pin_08.55.in \
    pin_08.55.xml \
    pin_08.56.in \
    pin_08.56.xml \
    pin_08.56b.xml \
    pin_08.57.in \
    pin_08.57.xml \
    pin_08.58.in \
    pin_08.58.xml \
    pin_08.59.in \
    pin_08.59.xml \
    pin_08.60.in \
    pin_08.60.xml \
    pin_08.61.in \
    pin_08.61.xml \
    pin_08.62.in \
    pin_08.62.xml \
    pin_08.63.in \
    pin_08.63.xml \
    pin_08.64.in \
    pin_08.64.xml \
    pin_08.65.in \
    pin_08.65.xml \
    pin_08.66.in \
    pin_08.66.xml \
    pin_08.67.in \
    pin_08.67.xml \
    pin_08.68.in \
    pin_08.68.xml \
    pin_08.71.in \
    pin_08.71.xml \
    pin_08.72.in \
    pin_08.73.in \
    pin_08.73.xml \
    pin_08.74.pending \
    pin_08.74.xml \
    pin_08.75.pending \
    pin_08.76.pending \
    pin_08.77.pending \
    pin_08.78.pending \
    pin_08.79.in \
    pin_08.79.xml \
    pin_08.80.in \
    pin_08.80.xml \
    pin_08.81.in \
    pin_08.81.xml \
    pin_08.82.in \
    pin_08.83.in \
    pin_08.83.xml \
    pin_08.84.in \
    pin_08.85.in \
    pin_08.85.xml \
    pin_08.86.in \
    pin_08.86.xml \
    pin_08.87.in \
    pin_08.87.xml \
    pin_08.88.in \
    pin_08.88.xml \
    pin_08.89.in \
    pin_08.89.xml \
    pin_08.90.in \
    pin_08.90.xml \
    pin_08.91.in \
    pin_08.91.xml \
    pin_08.92.in \
    pin_08.92.xml \
    pin_08.93.in \
    pin_08.93.xml \
    pin_08.94.in \
    pin_08.94.xml \
    pin_08.95.in \
    pin_08.95.xml \
    pin_08.96.in \
    pin_08.96.xml \
    pin_08.97.in \
    pin_08.97.xml \
    pin_08.98.in \
    pin_08.98.xml \
    pin_08.99.in \
    pin_08.99.xml \
    pin_08.100.in \
    pin_08.100.xml \
    pin_08.101.in \
    pin_08.101.xml \
    pin_08.102.in \
    pin_08.102.xml \
    pin_08.103.in \
    pin_08.103.xml \
    pin_08.104.in \
    pin_08.104.xml \
    pin_08.105.in \
    pin_08.105.xml \
    pin_08.106.in \
    pin_08.106.xml \
    pin_08.107.in \
    pin_08.107.xml \
    pin_08.108.in \
    pin_08.108.xml \
    pin_09.01.in \
    pin_09.01.xml \
    pin_09.02.in \
    pin_09.02.xml \
    pin_09.03.in \
    pin_09.03.xml \
    pin_09.04.in \
    pin_09.04.xml \
    ${SAVEDDATA}

S2O = | ${SED} '1,/@@/d'

all:

valgrind:
	@echo '## Running the regression tests under Valgrind'
	${MAKE} CHECKER='valgrind -q' tests

SCRIBBLE=DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib MallocPreScribble=1

scribble:
	@echo '## Running the regression tests under MallocPreScribble'
	${MAKE} CHECKER="${SCRIBBLE}" tests

#TEST_TRACE = set -x ; 

RUN_TESTS = ${srcdir}/../run-tests.sh -d ${srcdir}

test tests: ${bin_PROGRAMS}
	@${MKDIR} -p out
	@sh ${RUN_TESTS} $(if $(CHECKER),-C "$(CHECKER)") run ${TEST_FILES}

one:

accept:
	@${MKDIR} -p ${srcdir}/saved
	@sh ${RUN_TESTS} accept ${TEST_FILES}

.c.test:
	$(AM_V_CC)$(LTCOMPILE) -MT $@ -MD -MP -MF $(DEPDIR)/$*.Tpo -o $@ $<

CLEANFILES = ${TEST_CASES:.c=.test}
CLEANDIRS = out

clean-local:
	rm -rf ${CLEANDIRS}

SLAXPROC=${top_builddir}/slaxproc/slaxproc
BASEDIR=${top_srcdir}/tests/core

SKIP_FILES = \
test-authors-02.slax \
test-count-02.slax \
test-count-05.slax \
test-empty-11.slax \
test-empty-12.slax \
test-empty-13.slax \
test-empty-15.slax \
test-empty-16.slax \
test-empty-29.slax \
test-empty-31.slax

slax-convert:
	-echo "... converting slax files to xsl ..."
	-mkdir -p out/core
	-(cd ${BASEDIR}; ls *.slax) > out/core/all.files
	-comm -13 ${srcdir}/skip-slax-files.txt out/core/all.files > out/core/work.files
	-for i in `cat out/core/work.files` ; do \
		base=`basename $$i .slax` ; \
		${SLAXPROC} -x ${BASEDIR}/$$i > out/core/$$base.xsl ; \
		done

slax-test: slax-convert
	BASEDIR=${BASEDIR} sh ${RUN_TESTS} \
		run-slax out/core/*.xsl
