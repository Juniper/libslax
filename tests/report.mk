REPORT_FILE_DEFAULT = report.list
REPORT_SH = ${abs_top_srcdir}/tests/run-tests.sh
REPORT_CMD = ${BASH} ${REPORT_SH}

CHECK_DIFF = S2O="${S2O}" ${REPORT_CMD} check-diff
CHECK_DIFFS = S2O="${S2O}" ${REPORT_CMD} check-diffs
REPORT_RECORD = ${REPORT_CMD} record

CLEANFILES += ${REPORT_FILE_DEFAULT}

.PHONY: test tests tests-run

${TEST_PREFIX}test ${TEST_PREFIX}tests:
	@if [ -n "$$REPORT_FILE" ] ; then \
	    ${MAKE} tests-run ; \
	else \
	    REPORT_FILE=`pwd`/${REPORT_FILE_DEFAULT} ; \
	    REPORT_PATH= ; \
	    export REPORT_FILE REPORT_PATH ; \
	    ${REPORT_CMD} report-start ; \
	    ${MAKE} tests-run || ${REPORT_RECORD} . "make tests failed" ; \
	    ${REPORT_CMD} report-finish ; \
	fi
