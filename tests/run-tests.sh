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

Exists () {
    if [ -f $1 ]; then
	true
    else
	false
    fi
}

run_tests () {
    oname=$name.$ds
    out=out/$oname
    ${ECHO} -n "... $test ... $name ... $ds ..."
    run "./$test $data input $input > $out.out 2> $out.err"
    ${ECHO} "    done"

    run "diff -Nu ${SRCDIR}/saved/$oname.out out/$oname.out | ${S2O}"
    run "diff -Nu ${SRCDIR}/saved/$oname.err out/$oname.err | ${S2O}"
}

run_one_test () {
    oname=$base
    out=out/$oname
    ${ECHO} -n "... $test ... "
    run "./$test $data </dev/null > $out.out 2> $out.err"
    ${ECHO} "    done"

    run "diff -Nu ${SRCDIR}/saved/$oname.out out/$oname.out | ${S2O}"
    run "diff -Nu ${SRCDIR}/saved/$oname.err out/$oname.err | ${S2O}"
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
# Run a set of pre-converted .xsl files against their matching .xml inputs.
#
# Each .xsl is named test-{base}-{NN}.xsl; the XML input is
# ${BASEDIR}/test-{base}.xml (the trailing -NN is stripped to find it).
# We use pin_08.test as the runner since it accepts an XSLT file directly
# via "input <xsl>" and an XML document via "xml <xml>".
#
# Output lands in out/core/; saved references live in ${SRCDIR}/saved/core/.
#
do_run_slax () {
    mkdir -p out/core
    absbasedir=`cd "${BASEDIR}" && pwd`

    for file in ${TESTS}; do
	EXT=`echo $file | sed -E 's:.*\.([^\./]*)$:\1:'`

	base=`basename $file .$EXT`

	basedata=`echo $base | sed 's/-[0-9]*$//'`
	xmlfile=${absbasedir}/${basedata}.xml

	if [ ! -f "$xmlfile" ]; then
	    info "... skipping $base (no xml: $xmlfile)"
	    continue
	fi

	oname=core/${base}
	out=out/${oname}

	${ECHO} -n "... ${base} ..."
	run "./pin_08.test input ${file} xml ${xmlfile} quiet dump clean \
	    > ${out}.out 2> ${out}.err"
	${ECHO} "    done"

	run "diff -Nu ${SRCDIR}/saved/${oname}.out ${out}.out | ${S2O}"
	run "diff -Nu ${SRCDIR}/saved/${oname}.err ${out}.err | ${S2O}"
    done
}

do_accept_slax () {
    mkdir -p ${SRCDIR}/saved/core

    for file in ${TESTS}; do
	base=`basename $file .xsl`
	oname=core/${base}

	accept_file out/${oname}.out ${SRCDIR}/saved/${oname}.out
	accept_file out/${oname}.err ${SRCDIR}/saved/${oname}.err
    done
}

do_run_one_slax () {
    local slaxproc=${SLAXPROC:-../../slaxproc/slaxproc}
    for file in "$@"; do
	EXT=`echo $file | sed -E 's:.*\.([^\./]*)$:\1:'`

	base=`basename $file .EXT`

	basedata=`echo $base | sed 's/-[0-9]*$//'`
	xmlfile=${BASEDIR}/${basedata}.xml

	oname=core/${base}
	out=${OUTDIR:-out}/${oname}
	rfile=${file}

	if [ "$EXT" = "slax" ]; then
	    run "$slaxproc -x $file > $out.xsl"
	    rfile=$out.xsl
	fi

	${ECHO} -n "... ${base} ..."
	run "./pin_08.test input ${tfile} xml ${xmlfile} quiet dump clean \
	    > ${out}.out 2> ${out}.err"
	${ECHO} "    done"

	run "diff -Nu ${SRCDIR}/saved/${oname}.out ${out}.out | ${S2O}"
	run "diff -Nu ${SRCDIR}/saved/${oname}.err ${out}.err | ${S2O}"
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
    -b) BASEDIR=$2; shift;;
    -d) SRCDIR=$2; shift;;
    -o) OUTDIR=$2; shift;;
    -s) SLAXPROC=$2; shift;;
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

    run-one-slax)
        do_run_one_slax "$@"
    ;;

    run-slax)
        TESTS="$@"
        do_run_slax
    ;;

    accept-slax)
        TESTS="$@"
        do_accept_slax
    ;;

    *)
        ${ECHO} "unknown verb: $verb" 1>&2
	;;
esac

exit 0
