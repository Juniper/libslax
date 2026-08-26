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
    -I${top_builddir}/libxo \
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
pin_08.c

pin_01_test_SOURCES = pin_01.c
pin_02_test_SOURCES = pin_02.c
pin_03_test_SOURCES = pin_03.c
pin_04_test_SOURCES = pin_04.c
pin_05_test_SOURCES = pin_05.c
pin_06_test_SOURCES = pin_06.c
pin_07_test_SOURCES = pin_07.c
pin_08_test_SOURCES = pin_08.c

# pin04 needs parrotdb; pin01 only needs libpin
pin_04_test_LDADD = \
    ${top_builddir}/libpsu/libpsu.la \
    ${top_builddir}/parrotdb/libparrotdb.la

# pin05 uses pin_filter_create which pulls in xo_filter symbols at runtime
pin_05_test_LDADD = \
    ${top_builddir}/libpsu/libpsu.la \
    ${top_builddir}/libpin/libpin.la \
    ${libdir}/libxo/libxo_filter.la \
    ${libdir}/libxo.la

# pin06 also calls xmlReadFile directly, so it needs libxml2
pin_06_test_LDADD = \
    ${top_builddir}/libpsu/libpsu.la \
    ${top_builddir}/libpin/libpin.la \
    ${libdir}/libxo/libxo_filter.la \
    ${libdir}/libxo.la \
    ${LIBXML_LIBS}

# pin07 likewise calls xmlReadFile directly
pin_07_test_LDADD = \
    ${top_builddir}/libpsu/libpsu.la \
    ${top_builddir}/libpin/libpin.la \
    ${libdir}/libxo/libxo_filter.la \
    ${libdir}/libxo.la \
    ${LIBXML_LIBS}

# pin08 likewise calls xmlReadFile directly
pin_08_test_LDADD = \
    ${top_builddir}/libpsu/libpsu.la \
    ${top_builddir}/libpin/libpin.la \
    ${libdir}/libxo/libxo_filter.la \
    ${libdir}/libxo.la \
    ${LIBXML_LIBS}

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
	@sh ${RUN_TESTS} run ${TEST_FILES}

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
