#!/bin/sh
# $Id$
#
# Copyright 2016-2025, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.
#

GOODDIR=${SRCDIR}/saved
S2O="sed 1,/@@/d"
ECHO=/bin/echo

# Used by the "run-reuse" verb, driven from tests/reuse/Makefile.am
WIDTH=${WIDTH:-80}
TEST_VERSION=${TEST_VERSION:-1.3}
F13="${CHECKER} ${SLAXPROC} ${SPDEBUG} --format --write-version ${TEST_VERSION} --width ${WIDTH}"
F12="${CHECKER} ${SLAXPROC} ${SPDEBUG} --format --write-version 1.2"
S2X="${CHECKER} ${SLAXPROC} ${SPDEBUG} --slax-to-xslt"
X2S="${CHECKER} ${SLAXPROC} ${SPDEBUG} --xslt-to-slax --write-version 1.2"
SRUN="${CHECKER} ${SLAXPROC} ${SPDEBUG} --run"

run () {
    cmd="$1"

    if [ "$DOC" = doc ]; then
        ${ECHO} "   - $cmd"
    else
        if [ ! -z ${TEST_VERBOSE} ]; then
            ${ECHO} "   - $cmd"
	fi
	# We need to eval to handle "&&" in commands
        eval $cmd
    fi
}

info () {
    ${ECHO} "$@"
}

Exists () {
    if [ -f $1 ]; then
	true
    else
	false
    fi
}

# Report verbs, usable from any Makefile.am (see tests/report.mk):
#   check-diff SAVED OUT
#   check-diffs SAVED_BASE OUT_BASE [STATUS]
#   record PATH [REASON ...]
#   report-start
#   report-finish

record () {
    local path="$1" ; shift

    if [ -z "${REPORT_FILE}" ]; then
        return 0
    fi

    if [ $# -gt 0 ]; then
        ${ECHO} "${REPORT_PATH}${path}: $*" >> "${REPORT_FILE}"
    else
        ${ECHO} "${REPORT_PATH}${path}" >> "${REPORT_FILE}"
    fi
}

check_diff () {
    local saved="$1"
    local out="$2"
    local output rc

    if [ "$DOC" = doc ]; then
        ${ECHO} "diff -Nu $saved $out | ${S2O}"
        return 0
    fi

    output=`diff -Nu "$saved" "$out"`
    rc=$?
    if [ $rc -ne 0 ]; then
        if [ ! -z "$output" ]; then
            printf '%s\n' "$output" | eval "${S2O}"
        fi
        record "$out"
    fi
}

check_status () {
    local status="$1"
    local out="$2"

    case "$status" in
    ''|*[!0-9]*)
        return 0
        ;;
    esac

    if [ "$status" -gt 128 ]; then
        ${ECHO} "*** $out: killed by signal `expr $status - 128`"
        record "$out" "killed by signal `expr $status - 128`"
    elif [ "$status" -ge 126 ]; then
        ${ECHO} "*** $out: could not run (status $status)"
        record "$out" "could not run (status $status)"
    fi
}

check_diffs () {
    check_diff "$1.out" "$2.out"
    check_diff "$1.err" "$2.err"
    if [ $# -gt 2 ]; then
        check_status "$3" "$2"
    fi
}

report_start () {
    if [ -z "${REPORT_FILE}" ]; then
        ${ECHO} "report-start: REPORT_FILE is not set" 1>&2
        return 0
    fi

    cp /dev/null "${REPORT_FILE}"
}

report_finish () {
    local count

    if [ -z "${REPORT_FILE}" ]; then
        ${ECHO} "report-finish: REPORT_FILE is not set" 1>&2
        return 0
    fi

    if [ -s "${REPORT_FILE}" ]; then
        count=`wc -l < "${REPORT_FILE}" | tr -d ' '`
        ${ECHO} ""
        ${ECHO} "*** ${count} test failure(s), listed in" \
            "`basename ${REPORT_FILE}`:"
        cat "${REPORT_FILE}"
    else
        ${ECHO} "--- no test failures ---"
    fi
}

run_tests () {
    oname=$name.$ds
    out=out/$oname
    ${ECHO} -n "... $test ... $name ... $ds ..."
    run "./$test $data input $input > $out.out 2> $out.err"
    ${ECHO} "    done"

    check_diffs "${SRCDIR}/saved/$oname" "out/$oname"
}

run_one_test () {
    oname=$base
    out=out/$oname
    ${ECHO} -n "... $test ... "
    run "./$test $data </dev/null > $out.out 2> $out.err"
    ${ECHO} "    done"

    check_diffs "${SRCDIR}/saved/$oname" "out/$oname"
}

do_run_tests () {
    mkdir -p out

    for test in ${TESTS}; do
	base=`basename $test .test`

	input_files=`echo ${SRCDIR}/${base}*.in`
	if Exists $input_files; then
	    for input in $input_files; do
                if [ -f $input ]; then
		    name=`basename $input .in`
		    ds=1
		    grep '^#' $input | while read comment data ; do
		        run_tests
		        ds=`expr $ds + 1`
		    done
	        fi
	    done
        else
	    run_one_test
	fi
    done
}

accept_file () {
    if ! cmp -s $*; then
        echo "... $1 ..."
        run "cp $*"
    fi
}

accept_tests () {
    oname=$1

    accept_file out/$oname.out ${SRCDIR}/saved/$oname.out
    accept_file out/$oname.err ${SRCDIR}/saved/$oname.err
}

accept_one_test () {
    oname=$1

    accept_file out/$oname.out ${SRCDIR}/saved/$oname.out
    accept_file out/$oname.err ${SRCDIR}/saved/$oname.err
}

do_accept () {
    for test in ${TESTS}; do
	base=`basename $test .test`

	input_files=`echo ${SRCDIR}/${base}*.in`
	if Exists $input_files; then
	    for input in $input_files; do
                if [ -f $input ]; then
		    name=`basename $input .in`
		    ds=1
		    grep '^#' $input | while read comment data ; do
		        accept_tests $name.$ds
		        ds=`expr $ds + 1`
		    done
	        fi
	    done
	else
	    accept_one_test $base
	fi
    done
}

#
# Most .xsl scripts share their basename with their XML input file
# (metric.xsl / metric.xml), but several XSLTMark benchmark scripts
# reuse a differently-named input across many scripts (identity.xsl
# and stringsort.xsl both run against db1000.xml) -- this table
# encodes those exceptions. Anything not listed here falls through
# to the default "$base.xml" case.
#
reuse_input_name () {
    case "$dir/$base" in
        XSLTMark/alphabetize|XSLTMark/avts|XSLTMark/creation|XSLTMark/dbtail|\
        XSLTMark/decoy|XSLTMark/encrypt|XSLTMark/functions|XSLTMark/patterns|\
        XSLTMark/prettyprint)
            input_name=db100.xml ;;
        XSLTMark/identity|XSLTMark/stringsort)
            input_name=db1000.xml ;;
        XSLTMark/dbonerow)
            input_name=db10000.xml ;;
        XSLTMark/attsets|XSLTMark/chart|XSLTMark/total)
            input_name=chart.xml ;;
        XSLTMark/backwards|XSLTMark/game)
            input_name=game.xml ;;
        XSLTMark/reverser)
            input_name=gettysburg.xml ;;
        XSLTMark/summarize)
            input_name=queens.xsl ;;
        XSLTMark/xslbench2|XSLTMark/xslbench3)
            input_name=xslbenchdream.xml ;;
        *)
            input_name=$base.xml ;;
    esac
}

# Resolve $input_name to an actual path: shared XSLTMark benchmark
# fixtures (db100.xml, db1000.xml, ...) live flat under $SRCDIR;
# an $SRCDIR/$dir override comes next; the $TESTDIR copy shipped
# alongside the source .xsl is the final fallback. If none exist
# (e.g. db10000.xml, which is only generated on-demand by
# XSLTMark/dbgen.pl when Perl is available), $input is left empty
# and the caller skips the run/diff step, same as today.
reuse_find_input () {
    reuse_input_name

    if [ -f ${SRCDIR}/$input_name ]; then
        input=${SRCDIR}/$input_name
    elif [ -f ${SRCDIR}/$dir/$input_name ]; then
        input=${SRCDIR}/$dir/$input_name
    elif [ -f ${TESTDIR}/$dir/$input_name ]; then
        input=${TESTDIR}/$dir/$input_name
    else
        input=
    fi
}

reuse_test_two () {
    run "${F13} out/$dir/$base.slax out/$dir/$base.slax3"
    run "${F12} out/$dir/$base.slax3 out/$dir/$base.slax4"
    run "${F13} out/$dir/$base.slax4 out/$dir/$base.slax5"

    output=`diff -Nu out/$dir/$base.slax3 out/$dir/$base.slax5`
    rc=$?
    if [ $rc -ne 0 ]; then
        if [ ! -z "$output" ]; then
            printf '%s\n' "$output" | eval "${S2O}"
        fi
        record "out/$dir/$base.slax5" "round-trip mismatch (slax3 vs slax5)"
    fi
}

run_reuse_test () {
    ${ECHO} "... $test ... (reuse) ..."
    mkdir -p out/$dir

    run "${X2S} ${TESTDIR}/$test out/$dir/$base.slax"
    run "${S2X} out/$dir/$base.slax out/$dir/$base.xsl2"
    check_diff "${SRCDIR}/saved/$dir/$base.slax" "out/$dir/$base.slax"
    check_diff "${SRCDIR}/saved/$dir/$base.xsl2" "out/$dir/$base.xsl2"

    reuse_test_two

    reuse_find_input
    if [ -n "$input" ]; then
        run "${SRUN} out/$dir/$base.slax $input > out/$dir/$base.out 2> out/$dir/$base.err"

        if [ -f ${SRCDIR}/saved/$dir/$base.out ]; then
            baseline=${SRCDIR}/saved/$dir/$base
        else
            baseline=${TESTDIR}/$dir/$base
        fi

        check_diffs "$baseline" "out/$dir/$base"
    fi
}

do_run_reuse () {
    for test in ${TESTS}; do
        base=`basename $test .xsl`
        dir=`dirname $test`
        run_reuse_test
    done
}

accept_reuse_test () {
    mkdir -p ${SRCDIR}/saved/$dir

    accept_file out/$dir/$base.slax ${SRCDIR}/saved/$dir/$base.slax
    accept_file out/$dir/$base.xsl2 ${SRCDIR}/saved/$dir/$base.xsl2

    if [ -f out/$dir/$base.out ]; then
        accept_file out/$dir/$base.out ${SRCDIR}/saved/$dir/$base.out
        accept_file out/$dir/$base.err ${SRCDIR}/saved/$dir/$base.err
    fi
}

do_accept_reuse () {
    for test in ${TESTS}; do
        base=`basename $test .xsl`
        dir=`dirname $test`
        accept_reuse_test
    done
}

#
# pa and xi tests do not work on linux yet
#
case `uname`-`basename $PWD` in
    Linux-pa|Linux-xi) exit 0;;
esac

while [ $# -gt 0 ]
do
    case "$1" in
    -d) SRCDIR=$2; shift;;
    -v) S2O=cat;;
    -*) echo "unknown option" >&2; exit;;
    *) break;;
    esac
    shift
done

verb=$1
shift

case $verb in
    run)
        TESTS="$@"
        do_run_tests
    ;;
    run-all)
        TESTS=`echo *test`
        do_run_tests
    ;;

    accept)
        TESTS="$@"
        do_accept
    ;;

    accept-all)
        TESTS=`echo *test`
        do_accept
    ;;

    run-reuse)
        TESTS="$@"
        do_run_reuse
    ;;

    accept-reuse)
        TESTS="$@"
        do_accept_reuse
    ;;

    check-diff)
        check_diff "$@"
    ;;

    check-diffs)
        check_diffs "$@"
    ;;

    record)
        record "$@"
    ;;

    report-start)
        report_start
    ;;

    report-finish)
        report_finish
    ;;

    *)
        ${ECHO} "unknown verb: $verb" 1>&2
	;;
esac

exit 0
