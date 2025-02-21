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
base_01.c

base_01_test_SOURCES = base_01.c

# TEST_CASES := $(shell cd ${srcdir} ; echo *.c )
SAVEDDATA := $(shell cd ${srcdir}; echo saved/ba*.out saved/ba*.err)

TEST_FILES = ${TEST_CASES:.c=.test}
noinst_PROGRAMS = ${TEST_FILES}

LDADD = \
    ${top_builddir}/libpsu/libpsu.la \
    ${top_builddir}/parrotdb/libparrotdb.la

EXTRA_DIST = \
    bamain.h \
    ${TEST_CASES} \
    ${SAVEDDATA}

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
