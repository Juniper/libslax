#
# $Id$
#
# Copyright 2011, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

TEST_CASES = \
    pr422249.slax \
    pr432942.slax \
    pr497958.slax

OTHER_TEST_CASES = \
    pr1486566.slax

EXTRA_DIST = \
    ${TEST_CASES} \
    ${TEST_CASES:.slax=.xsl} \
    ${TEST_CASES:.slax=.slax2} \
    ${OTHER_TEST_CASES} \
    ${OTHER_TEST_CASES:.slax=.out}

WIDTH = 80
WIDTH_ARG = --width ${WIDTH}
TEST_VERSION = 1.3
VERSION_ARG = --write-version ${TEST_VERSION}
VERSION_ARG12 = --write-version 1.2
F13 = ${CHECKER} ${SLAXPROC} ${SPDEBUG} --format ${VERSION_ARG} ${WIDTH_ARG}
F12 = ${CHECKER} ${SLAXPROC} ${SPDEBUG} --format ${VERSION_ARG12}

SLAXPROC=${top_builddir}/slaxproc/slaxproc
RUN_TESTS = sh ${srcdir}/../run-tests.sh -d ${srcdir} -s ${SLAXPROC}
S2O = | ${SED} '1,/@@/d'
SPDEBUG=

all:

${SLAXPROC}:
	@(cd ${top_builddir}/slaxproc ; ${MAKE} slaxproc)

valgrind:
	@echo '## Running the regression tests under Valgrind'
	${MAKE} CHECKER='valgrind -q' tests

SCRIBBLE=DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib MallocPreScribble=1

scribble:
	@echo '## Running the regression tests under MallocPreScribble'
	${MAKE} CHECKER="${SCRIBBLE}" tests

test tests: ${SLAXPROC}
	@${MKDIR} -p out
	@${RUN_TESTS} ${RUN_OPTS} run-bugs ${TEST_CASES}
	@${RUN_TESTS} ${RUN_OPTS} run-bugs-other ${OTHER_TEST_CASES}

accept:
	@${RUN_TESTS} accept-bugs ${TEST_CASES}
	@${RUN_TESTS} accept-bugs-other ${OTHER_TEST_CASES}

clean-local:
	${RM} -fr out
