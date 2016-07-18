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
pa_01.c \
pa_02.c \
pa_03.c \
pa_04.c \
pa_05.c \
pa_06.c \
pa_07.c

pa_01_test_SOURCES = pa_01.c
pa_02_test_SOURCES = pa_02.c
pa_03_test_SOURCES = pa_03.c
pa_04_test_SOURCES = pa_04.c
pa_05_test_SOURCES = pa_05.c
pa_06_test_SOURCES = pa_06.c
pa_07_test_SOURCES = pa_07.c

# TEST_CASES := $(shell cd ${srcdir} ; echo *.c )

TEST_FILES = ${TEST_CASES:.c=.test}
noinst_PROGRAMS = ${TEST_FILES}

LDADD = \
    ${top_builddir}/libslax/libslax.la

EXTRA_DIST = \
    ${TEST_CASES} \
    ${addprefix saved/, ${TEST_CASES:.c=.T.err}} \
    ${addprefix saved/, ${TEST_CASES:.c=.T.out}} \
    ${addprefix saved/, ${TEST_CASES:.c=.XP.err}} \
    ${addprefix saved/, ${TEST_CASES:.c=.XP.out}} \
    ${addprefix saved/, ${TEST_CASES:.c=.JP.err}} \
    ${addprefix saved/, ${TEST_CASES:.c=.JP.out}} \
    ${addprefix saved/, ${TEST_CASES:.c=.HP.err}} \
    ${addprefix saved/, ${TEST_CASES:.c=.HP.out}} \
    ${addprefix saved/, ${TEST_CASES:.c=.X.err}} \
    ${addprefix saved/, ${TEST_CASES:.c=.X.out}} \
    ${addprefix saved/, ${TEST_CASES:.c=.J.err}} \
    ${addprefix saved/, ${TEST_CASES:.c=.J.out}} \
    ${addprefix saved/, ${TEST_CASES:.c=.H.err}} \
    ${addprefix saved/, ${TEST_CASES:.c=.H.out}} \
    ${addprefix saved/, ${TEST_CASES:.c=.HIPx.err}} \
    ${addprefix saved/, ${TEST_CASES:.c=.HIPx.out}}

S2O = | ${SED} '1,/@@/d'

all:

valgrind:
	@echo '## Running the regression tests under Valgrind'
	${MAKE} CHECKER='valgrind -q' tests

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

RUN_TESTS = ${srcdir}/../run-tests.sh ${srcdir}

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
