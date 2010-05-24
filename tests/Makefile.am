SUBDIRS=spec bugs

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

libxslt-tests:
if USE_LIBXSLT_TESTS
	${RM} -fr libxslt
	${CP} -r ${WITH_LIBXSLT_TESTS}/ libxslt
	${CP} ${srcdir}/libxslt.ac libxslt/configure.ac
	( cd libxslt ; sh configure )
	( FAKEPROC=`cd $srcdir; pwd` ; \
		cd libxslt ; ${MAKE} CHECKER=$$FAKEPROC )
else
	@echo "skipping libxslt tests"
endif

