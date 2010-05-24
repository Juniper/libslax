SUBDIRS=spec bugs fakeproc

test tests:
	@(cur=`pwd` ; for dir in $(SUBDIRS) ; do \
		cd $$dir ; \
		$(MAKE) CHECKER='$(CHECKER)' tests ; \
		cd $$cur ; \
	done)

valgrind:
	@echo '## Running the regression tests under Valgrind'
	@echo '## Go get a cup of coffee it is gonna take a while ...'
	${MAKE} VALGRIND='valgrind -q' tests

full: tests libxslt-tests

.PHONY: libxslt-tests libxslt-setup

libxslt-tests: libxslt-setup
if USE_LIBXSLT_TESTS
	(cd libxslt ; sh configure)
	(FAKEPROC=`cd ${srcdir}; pwd`/fakeproc ; \
		cd libxslt ; ${MAKE} CHECKER=$$FAKEPROC)
else
	@echo "skipping libxslt tests"
endif

setup libxslt-setup:
if USE_LIBXSLT_TESTS
	${RM} -fr libxslt
	${CP} -r ${WITH_LIBXSLT_TESTS}/ libxslt
	@-(for i in libxslt/*/Makefile.am ; do \
		${MV} $$i $$i.save ; \
		${SED} -e 's/xsltproc/slaxproc/g' \
			-e 's:/slaxproc/slaxproc:/../../slaxproc/slaxproc:g' \
				< $$i.save > $$i; \
	done)
	${CP} ${srcdir}/libxslt.ac libxslt/configure.ac
	(cd libxslt ; /opt/local/bin/autoreconf --install)
	(cd libxslt ; ./configure )
endif