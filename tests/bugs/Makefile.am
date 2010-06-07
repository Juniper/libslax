
TEST_CASES = \
    pr432942.slax

EXTRA_DIST = \
    ${TEST_CASES} \
    ${TEST_CASES:.slax=.xsl} \
    ${TEST_CASES:.slax=.slax2}

SLAXPROC=${top_builddir}/slaxproc/slaxproc
S2O = | ${SED} '1,/@@/d'
SPDEBUG=
S2X = ${CHECKER} ${SLAXPROC} ${SPDEBUG} --slax-to-xslt
X2S = ${CHECKER} ${SLAXPROC} ${SPDEBUG} --xslt-to-slax
SRUN = ${CHECKER} ${SLAXPROC} ${SPDEBUG} --run

all:

${SLAXPROC}:
	@(cd ${top_builddir}/slaxproc ; ${MAKE} slaxproc)

valgrind:
	@echo '## Running the regression tests under Valgrind'
	${MAKE} CHECKER='valgrind -q' tests

test tests: ${SLAXPROC}
	@${MKDIR} -p out
	-@(for test in ${TEST_CASES} ; do \
	  echo "... $$test ..."; \
	  name=`${BASENAME} $$test .slax` ; \
	  ${S2X} ${srcdir}/$$test out/$$name.xsl ; \
	  ${DIFF} -Nbu ${srcdir}/$$name.xsl out/$$name.xsl ${S2O} ; \
	  ${X2S} out/$$name.xsl out/$$name.slax2 ; \
	  ${DIFF} -Nbu ${srcdir}/$$name.slax2 out/$$name.slax2 ${S2O} ; \
	done)

accept:
	-@(for test in ${TEST_CASES} ; do \
	  name=`${BASENAME} $$test .slax` ; \
	  ${CP} out/$$name.xsl ${srcdir}/$$name.xsl ; \
	  ${CP} out/$$name.slax2 ${srcdir}/$$name.slax2 ; \
	done)

clean-local:
	${RM} -fr out
