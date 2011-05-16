#
# $Id$
#

TEST_CASES := $(shell cd ${srcdir} ; echo *.slax )

EXTRA_DIST = \
    ${TEST_CASES} \
    $(addprefix saved/, ${TEST_CASES:.slax=.err})

SLAXPROC=${top_builddir}/slaxproc/slaxproc
S2O = | ${SED} '1,/@@/d'
SPDEBUG=
S2E = ${CHECKER} ${SLAXPROC} ${SPDEBUG} --check

CLEANDIRS = out

all:

${SLAXPROC}:
	@(cd ${top_builddir}/slaxproc ; ${MAKE} slaxproc)

valgrind:
	@echo '## Running the regression tests under Valgrind'
	${MAKE} CHECKER='valgrind -q' tests

#TEST_TRACE = set -x ; 

TEST_ONE = \
 base=`${BASENAME} $$test .slax` ; \
 ${S2E} ${srcdir}/$$test > out/$$base.out 2> out/$$base.err ; \
 ${DIFF} -Nbu ${srcdir}/saved/$$base.out out/$$base.out ${S2O} ; \
 ${DIFF} -Nbu ${srcdir}/saved/$$base.err out/$$base.err ${S2O}

test tests:
	-@echo "... (skipping errors) ...";

errors: ${SLAXPROC}
	@${MKDIR} -p out
	-@(for test in ${TEST_CASES} ; do \
	      echo "... $$test ..."; \
	      ${TEST_ONE}; \
	done; true)

one:
	-@(test=${TEST_CASE}; data=${TEST_DATA}; ${TEST_ONE} ; true)

accept:
	-@(for test in ${TEST_CASES} ; do \
	      echo "... $$test ..."; \
	      base=`${BASENAME} $$test .slax` ; \
	      ${CP} out/$$base.out ${srcdir}/saved/$$base.out ; \
	      ${CP} out/$$base.err ${srcdir}/saved/$$base.err ; \
	done; true)
