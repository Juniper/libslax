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
test-2-03.slax

EXTRA_DIST = \
    ${TEST_CASES} \
    ${TEST_CASES:.slax=.xsl} \
    ${TEST_CASES:.slax=.comp}

all:

valgrind:
	@echo '## Running the regression tests under Valgrind'
	${MAKE} CHECKER='valgrind -q' tests

test tests: ${SLAXPROC}
	@${MKDIR} -p out
	-@(for test in ${srcdir}/*.slax ; do \
	  name=`${BASENAME} $$test .slax` ; \
	  echo "... $$name ..."; \
	  ${SLAXPROC} --slax-to-xslt $$test out/$$name.xsl ; \
	  ${DIFF} -Nbu ${srcdir}/$$name.xsl out/$$name.xsl ; \
	  ${SLAXPROC} --xslt-to-slax out/$$name.xsl out/$$name.slax2 ; \
	  ${DIFF} -bu $$test out/$$name.slax2 \
		| ${SED} '1,/@@/d' > out/$$name.comp2 ; \
	  ${DIFF} -Nbu ${srcdir}/$$name.comp out/$$name.comp2 ; \
	done)

accept:
	-@(for test in ${srcdir}/*.slax ; do \
	  name=`${BASENAME} $$test .slax` ; \
	  ${CP} out/$$name.xsl ${srcdir}/$$name.xsl ; \
	  ${CP} out/$$name.comp2 ${srcdir}/$$name.comp ; \
	done)

clean-local:
	${RM} -fr out
