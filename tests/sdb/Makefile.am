#
# $Id$
#
# Copyright 2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

TEST_CASES := $(shell cd ${srcdir} ; echo *.slax )

EXTRA_DIST = \
    ${TEST_CASES} \
    $(wildcard *.cmd) \
    $(wildcard *.xml) \
    normalize-callgraph.pl \
    $(addprefix saved/, ${TEST_CASES:.slax=.out}) \
    $(addprefix saved/, ${TEST_CASES:.slax=.err}) \
    saved/profile-file.prof \
    saved/profile-callgraph.prof

SLAXPROC = ${top_builddir}/slaxproc/slaxproc
S2O = | ${SED} '1,/@@/d'
SDB = ${SLAXPROC} --debug --no-readline --no-tty --no-randomize --echo-input

# Each .cmd file issues its own explicit "run" command (after any "break"
# setup lines), the same way a .cmd file would type it at the "(sdb)"
# prompt; SDB above deliberately omits --run.

# The "profile report" table's Hits column (and the Total row's Hits
# total) are deterministic repeat counts, but the %, User, U/Hit, System,
# and S/Hit columns are real wall-clock timings that differ on every run
# and cannot be golden-file-diffed as-is; replace them with a fixed
# placeholder before diffing, leaving line numbers, hit counts, and
# source text intact.
CLEANPROFILE = sed -i.bak -E \
    -e 's/^([[:space:]]*[0-9]+[[:space:]]+[0-9]+[[:space:]]*\([0-9]+\))[[:space:]]+[0-9]+[[:space:]]*\([0-9]+\)[[:space:]]+[0-9]+\.[0-9]+[[:space:]]+[0-9]+[[:space:]]+[0-9]+\.[0-9]+/\1 TIME TIME TIME TIME/' \
    -e 's/^(Total[[:space:]]+[0-9]+)[[:space:]]+[0-9]+[[:space:]]+[0-9]+([[:space:]]+Total)$$/\1 TIME TIME\2/'

# "dump" and "verbose" print raw libxml2/libxslt object pointers, which
# differ on every run (and between machines); normalize them to a fixed
# placeholder before diffing.
CLEANADDR = sed -i.bak -E 's/0x[0-9a-f]+/0xADDR/g'

# libxslt's own xsltSaveProfiling() call-graph report (produced by
# "profile call-graph <file>") ranks templates by measured wall-clock
# time; for a fast test stylesheet that time is near-zero and noisy, so
# the "[N]" indices and row/block order reshuffle from run to run even
# with identical input.  normalize-callgraph.pl re-derives a stable
# numbering and masks the timing fields; see its header comment.
CLEANCALLGRAPH = ${PERL} ${srcdir}/normalize-callgraph.pl

CLEANDIRS = out

all:

${SLAXPROC}:
	@(cd ${top_builddir}/slaxproc ; ${MAKE} slaxproc)

# Most test cases drive the interactive debugger over stdin via a .cmd
# file.  The exception is "profile-file", which exercises slaxproc's
# separate non-interactive "--profile <file>" mode (no debugger prompt,
# no stdin) and has no .cmd file.  Either kind of test may produce an
# out/$base.prof report file -- "profile-file" always does (via
# --profile), and a .cmd test does if it issues "profile call-graph
# <file>" -- and when it does, that gets diffed against an extra
# saved/*.prof golden file too, after the appropriate cleaner.
TEST_ONE = \
 base=`${BASENAME} $$test .slax` ; \
 data=${srcdir}/$$base.xml ; \
 test -f $$data || data= ; \
 cmd=${srcdir}/$$base.cmd ; \
 if test -f $$cmd ; then \
   ${SDB} ${srcdir}/$$test $$data < $$cmd > out/$$base.out 2> out/$$base.err ; \
   if test -f out/$$base.prof ; then \
     ${CLEANCALLGRAPH} out/$$base.prof ; \
     ${DIFF} -Nbu ${srcdir}/saved/$$base.prof out/$$base.prof ${S2O} ; \
   fi ; \
 else \
   ${SLAXPROC} --profile out/$$base.prof --run ${srcdir}/$$test $$data \
     > out/$$base.out 2> out/$$base.err ; \
   ${CLEANPROFILE} out/$$base.prof ; \
   ${DIFF} -Nbu ${srcdir}/saved/$$base.prof out/$$base.prof ${S2O} ; \
 fi ; \
 ${CLEANPROFILE} out/$$base.err ; \
 ${CLEANADDR} out/$$base.err ; \
 ${DIFF} -Nbu ${srcdir}/saved/$$base.out out/$$base.out ${S2O} ; \
 ${DIFF} -Nbu ${srcdir}/saved/$$base.err out/$$base.err ${S2O}

test tests: ${SLAXPROC}
	@${MKDIR} -p out
	-@${DEB} (for test in ${TEST_CASES} ; do \
	    echo "... $$test ..."; \
	    ${TEST_ONE}; \
	    true; \
	done)

one:
	-@(test=${TEST_CASE}; ${TEST_ONE} ; true)

accept:
	-@(for test in ${TEST_CASES} ; do \
	    base=`${BASENAME} $$test .slax` ; \
	    ${CP} out/$$base.out ${srcdir}/saved/$$base.out ; \
	    ${CP} out/$$base.err ${srcdir}/saved/$$base.err ; \
	    test -f out/$$base.prof && ${CP} out/$$base.prof ${srcdir}/saved/$$base.prof ; \
	    true ; \
	done)

clean-local:
	${RM} -fr ${CLEANDIRS}
