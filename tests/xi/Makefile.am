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
xi01.c

XXX= \
xi02.c \
xi03.c

xi01_test_SOURCES = xi01.c
#xi02_test_SOURCES = xi02.c
#xi03_test_SOURCES = xi03.c

# TEST_CASES := $(shell cd ${srcdir} ; echo *.c )
SAVEDDATA := $(shell cd ${srcdir} ; echo saved/xi*.out saved/xi*.err)

TEST_FILES = ${TEST_CASES:.c=.test}
noinst_PROGRAMS = ${TEST_FILES}

LDADD = \
    ${top_builddir}/libpsu/libpsu.la \
    ${top_builddir}/libxi/libxi.la

EXTRA_DIST = \
    ${TEST_CASES} \
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
