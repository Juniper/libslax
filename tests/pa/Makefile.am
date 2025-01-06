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
    -I${top_srcdir}/libslax \
    -I${top_builddir} \
    ${WARNINGS}

# Ick: maintained by hand!
TEST_CASES = \
pa01.c \
pa02.c \
pa03.c \
pa04.c \
pa05.c \
pa06.c \
pa07.c

pa01_test_SOURCES = pa01.c
pa02_test_SOURCES = pa02.c
pa03_test_SOURCES = pa03.c
pa04_test_SOURCES = pa04.c
pa05_test_SOURCES = pa05.c
pa06_test_SOURCES = pa06.c
pa07_test_SOURCES = pa07.c

# TEST_CASES := $(shell cd ${srcdir} ; echo *.c )
SAVEDDATA := $(shell cd ${srcdir}; echo saved/pa*.out saved/pa*.err)

TEST_FILES = ${TEST_CASES:.c=.test}
noinst_PROGRAMS = ${TEST_FILES}

LDADD = \
    ${top_builddir}/libpsu/libpsu.la \
    ${top_builddir}/parrotdb/libparrotdb.la

EXTRA_DIST = \
    pamain.h \
    ${TEST_CASES} \
    ${SAVEDDATA}

S2O = | ${SED} '1,/@@/d'

SH_OPTS=

all:

valgrind:
	@echo '## Running the regression tests under Valgrind'
	${MAKE} CHECKER='valgrind -q' tests

SCRIBBLE=DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib MallocPreScribble=1

scribble:
	@echo '## Running the regression tests under MallocPreScribble'
	${MAKE} CHECKER="${SCRIBBLE}" tests

#TEST_TRACE = set -x ; 

TEST_JIG = \
      ${CHECKER} ./$$base.test ${TEST_OPTS} \
      > out/$$base.$$fmt.out 2> out/$$base.$$fmt.err ; \
 ${DIFF} -Nu ${srcdir}/saved/$$base.$$fmt.out out/$$base.$$fmt.out ${S2O} ; \
 ${DIFF} -Nu ${srcdir}/saved/$$base.$$fmt.err out/$$base.$$fmt.err ${S2O}

TEST_ONE = \
  LIBXO_OPTIONS=:W$$fmt ${TEST_JIG}

TEST_TWO = \
  LIBXO_OPTIONS=warn,encoder=test ${TEST_JIG}

TEST_FORMATS = T XP JP HP X J H HIPx

RUN_TESTS = ${srcdir}/../run-tests.sh -d ${srcdir}

test tests: ${bin_PROGRAMS}
	@${MKDIR} -p out
	@sh ${SH_OPTS} ${RUN_TESTS} run ${TEST_FILES}

one:

accept:
	@${MKDIR} -p ${srcdir}/saved
	@$sh ${SH_OPTS} ${RUN_TESTS} accept ${TEST_FILES}

.c.test:
	$(AM_V_CC)$(LTCOMPILE) -MT $@ -MD -MP -MF $(DEPDIR)/$*.Tpo -o $@ $<

CLEANFILES = ${TEST_CASES:.c=.test}
CLEANDIRS = out

clean-local:
	rm -rf ${CLEANDIRS}
