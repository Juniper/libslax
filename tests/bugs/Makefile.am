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

SLAXPROC=${top_builddir}/slaxproc/slaxproc
S2O = | ${SED} '1,/@@/d'
SPDEBUG=
S2X = ${CHECKER} ${SLAXPROC} ${SPDEBUG} --slax-to-xslt
X2S = ${CHECKER} ${SLAXPROC} ${SPDEBUG} --xslt-to-slax
SRUN = ${CHECKER} ${SLAXPROC} ${SPDEBUG} --run
SDUMP = ${CHECKER} ${SLAXPROC} ${SPDEBUG} --format --dump-tree

all:

${SLAXPROC}:
	@(cd ${top_builddir}/slaxproc ; ${MAKE} slaxproc)

valgrind:
	@echo '## Running the regression tests under Valgrind'
	${MAKE} CHECKER='valgrind -q' tests

test tests: ${SLAXPROC} main-tests other-tests

main-tests:
	@${MKDIR} -p out
	-@(for test in ${TEST_CASES} ; do \
	  echo "... $$test ..."; \
	  name=`${BASENAME} $$test .slax` ; \
	  ${S2X} ${srcdir}/$$test out/$$name.xsl ; \
	  ${DIFF} -Nbu ${srcdir}/$$name.xsl out/$$name.xsl ${S2O} ; \
	  ${X2S} out/$$name.xsl out/$$name.slax2 ; \
	  ${DIFF} -Nbu ${srcdir}/$$name.slax2 out/$$name.slax2 ${S2O} ; \
	done)

other-tests: ${SLAXPROC}
	@${MKDIR} -p out
	-@(for test in ${OTHER_TEST_CASES} ; do \
	  echo "... $$test ..."; \
	  name=`${BASENAME} $$test .slax` ; \
	  ${SDUMP} ${srcdir}/$$test > out/$$name.out 2>&1 ; \
	  ${DIFF} -Nbu ${srcdir}/$$name.out out/$$name.out ${S2O} ; \
	done)

accept:
	-@(for test in ${TEST_CASES} ; do \
	  name=`${BASENAME} $$test .slax` ; \
	  ${CP} out/$$name.xsl ${srcdir}/$$name.xsl ; \
	  ${CP} out/$$name.slax2 ${srcdir}/$$name.slax2 ; \
	done)
	-@(for test in ${OTHER_TEST_CASES} ; do \
	  name=`${BASENAME} $$test .slax` ; \
	  ${CP} out/$$name.out ${srcdir}/$$name.out ; \
	done)

clean-local:
	${RM} -fr out
