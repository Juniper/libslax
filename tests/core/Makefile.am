#
# $Id$
#

TEST_XML = `cd ${srcdir} ; echo *.xml`
TEST_CASES = `cd ${srcdir} ; echo *.slax`

EXTRA_DIST = \
    ${TEST_CASES} \
    ${TEST_CASES:.slax=.xsl} \
    ${TEST_CASES:.slax=.slax2}

SLAXPROC=${top_builddir}/slaxproc/slaxproc
S2O = | ${SED} '1,/@@/d'
SPDEBUG=
S2X = ${CHECKER} ${SLAXPROC} ${SPDEBUG} --slax-to-xslt
X2S = ${CHECKER} ${SLAXPROC} ${SPDEBUG} --xslt-to-slax
SRUN = ${CHECKER} ${SLAXPROC} ${SPDEBUG} --run --indent

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
 ${S2X} ${srcdir}/$$test out/$$base.xsl ; \
 ${DIFF} -Nbu ${srcdir}/saved/$$base.xsl out/$$base.xsl ${S2O} ; \
 ${X2S} out/$$base.xsl out/$$base.slax2 ; \
 ${DIFF} -Nbu ${srcdir}/saved/$$base.slax2 out/$$base.slax2 ${S2O} ; \
 ${SRUN} ${srcdir}/$$test ${srcdir}/$$data \
   > out/$$base.out 2> out/$$base.err ; \
 ${DIFF} -Nu ${srcdir}/saved/$$base.out out/$$base.out ${S2O}

test tests: ${SLAXPROC}
	@${MKDIR} -p out
	-@(for data in ${TEST_XML} ; do \
	    basedata=`${BASENAME} $$data .xml` ; \
 	    cases=`cd ${srcdir} ; echo $${basedata}-*.slax`; \
	    (for test in $$cases ; do \
	      echo "... $$test ..."; \
	      ${TEST_ONE}; \
              true; \
	    done); \
	done)

one:
	-@(test=${TEST_CASE}; data=${TEST_DATA}; ${TEST_ONE} ; true)

accept:
	-@(for data in ${TEST_XML} ; do \
	    basedata=`${BASENAME} $$data .xml` ; \
 	    cases=`cd ${srcdir} ; echo $${basedata}-*.slax`; \
	    (for test in $$cases ; do \
	      echo "... $$test ..."; \
	      base=`${BASENAME} $$test .slax` ; \
	      ${CP} out/$$base.xsl ${srcdir}/saved/$$base.xsl ; \
	      ${CP} out/$$base.slax2 ${srcdir}/saved/$$base.slax2 ; \
	      ${CP} out/$$base.out ${srcdir}/saved/$$base.out ; \
	    done); \
	done)
