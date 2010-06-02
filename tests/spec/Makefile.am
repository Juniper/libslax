#
# $Id$
#

SLAXPROC=${top_builddir}/slaxproc/slaxproc

${SLAXPROC}:
	@(cd ${top_builddir}/slaxproc ; ${MAKE} slaxproc)

TEST_CASES = \
test-1-01.slax \
test-1-02.slax \
test-1-03.slax \
test-1-04.slax \
test-1-05.slax \
test-1-06.slax \
test-1-07.slax \
test-1-08.slax \
test-1-09.slax \
test-1-10.slax \
test-1-11.slax \
test-1-12.slax \
test-1-13.slax \
test-1-14.slax \
test-1-15.slax \
test-1-16.slax \
test-1-17.slax \
test-1-18.slax \
test-1-19.slax \
test-2-01.slax \
test-2-02.slax \
test-2-03.slax \
test-2-04.slax

EXTRA_DIST = \
    ${TEST_CASES} \
    ${TEST_CASES:.slax=.xsl} \
    ${TEST_CASES:.slax=.slax2}

S2O = | ${SED} '1,/@@/d'

all:

valgrind:
	@echo '## Running the regression tests under Valgrind'
	${MAKE} CHECKER='valgrind -q' tests

test tests: ${SLAXPROC}
	@${MKDIR} -p out
	-@(for test in ${TEST_CASES} ; do \
	  echo "... $$test ..."; \
	  base=`${BASENAME} $$test .slax` ; \
	  ${SLAXPROC} --slax-to-xslt ${srcdir}/$$test out/$$base.xsl ; \
	  ${DIFF} -Nbu ${srcdir}/$$base.xsl out/$$base.xsl ${S2O} ; \
	  ${SLAXPROC} --xslt-to-slax out/$$base.xsl out/$$base.slax2 ; \
	  ${DIFF} -Nbu ${srcdir}/$$base.slax2 out/$$base.slax2 ${S2O} ; \
	  if [ -f ${srcdir}/$$base.xml ]; then \
	    ${SLAXPROC} ${srcdir}/$$test ${srcdir}/$$base.xml \
	            > out/$$base.out 2> out/$$base.err ; \
	    ${DIFF} -Nu ${srcdir}/$$base.out out/$$base.out ${S2O} ; \
	  fi ; \
	done)

accept:
	-@(for test in ${TEST_CASES} ; do \
	  base=`${BASENAME} $$test .slax` ; \
	  ${CP} out/$$base.xsl ${srcdir}/$$base.xsl ; \
	  ${CP} out/$$base.slax2 ${srcdir}/$$base.slax2 ; \
	done)

clean-local:
	${RM} -fr out
