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

run_tests () {
    oname=$name.$ds
    out=out/$oname
    ${ECHO} -n "... $test ... $name ... $ds ..."
    run "$test $data input $input > $out.out 2> $out.err"
    ${ECHO} "    done"

    run "diff -Nu ${SRCDIR}/saved/$oname.out out/$oname.out | ${S2O}"
    run "diff -Nu ${SRCDIR}/saved/$oname.err out/$oname.err | ${S2O}"
}

do_run_tests () {
    mkdir -p out

    for test in ${TESTS}; do
	base=`basename $test .test`

	for input in `echo ${SRCDIR}/${base}*.in`; do
            if [ -f $input ]; then
		name=`basename $input .in`
		ds=1
		grep '^#' $input | while read comment data ; do
		    run_tests
		    ds=`expr $ds + 1`
		done
	    fi
	done
    done
}

accept_file () {
    if ! cmp -s $*; then
        echo "... $1 ..."
        run "cp $*"
    fi
}

accept_tests () {
    oname=$name.$ds

    accept_file out/$oname.out ${SRCDIR}/saved/$oname.out
    accept_file out/$oname.err ${SRCDIR}/saved/$oname.err
}

do_accept () {
    for test in ${TESTS}; do
	base=`basename $test .test`

	for input in `echo ${SRCDIR}/${base}*.in`; do
            if [ -f $input ]; then
		name=`basename $input .in`
		ds=1
		grep '^#' $input | while read comment data ; do
		    accept_tests
		    ds=`expr $ds + 1`
		done
	    fi
	done
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

    *)
        ${ECHO} "unknown verb: $verb" 1>&2
	;;
esac

exit 0
