## Process this file with automake to produce Makefile.in

CHECKER=${srcdir}/../fakeproc
SLAXPROC=${top_builddir}/slaxproc/slaxproc

${SLAXPROC}:
	@(cd ${top_builddir}/slaxproc ; ${MAKE} slaxproc)


TEST_CASES = \
    pr432942.slax

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
	-@(for i in ${srcdir}/*.slax ; do \
	  name=`${BASENAME} $$i .slax` ; \
	  echo "... $$name ..."; \
	  ${SLAXPROC} --slax-to-xslt $$i out/$$name.xsl ; \
	  ${DIFF} -Nbu ${srcdir}/$$name.xsl out/$$name.xsl ; \
	  ${SLAXPROC} --xslt-to-slax out/$$name.xsl out/$$name.slax2 ; \
	  ${DIFF} -bu $$i out/$$name.slax2 | \
		${SED} '1,/@@/d' > out/$$name.comp2 ; \
	  ${DIFF} -Nbu ${srcdir}/$$name.comp out/$$name.comp2 ; \
	done)

accept:
	-@(for i in ${srcdir}/*.slax ; do \
	  name=`${BASENAME} $$i .slax` ; \
	  ${CP} out/$$name.xsl ${srcdir}/$$name.xsl ; \
	  ${CP} out/$$name.comp2 ${srcdir}/$$name.comp ; \
	done)

clean-local:
	${RM} -fr out
