## Process this file with automake to produce Makefile.in

AM_CPPFLAGS = \
	-I$(top_builddir) -I$(top_srcdir) \
	-I$(top_builddir)/bxml -I$(top_srcdir)/bxml \
	-I$(top_builddir)/bxml/include -I$(top_srcdir)/bxml/include

noinst_PROGRAMS = \
	runsuite \
	runtest \
	runxmlconf \
	testModule \
	testapi \
	testcatalog \
	testchar \
	testdict \
	testlimits \
	testparser \
	testrecurse

DEPS = $(top_builddir)/bxml/libbxml.la
LDADDS = $(top_builddir)/bxml/libbxml.la

runtest_SOURCES=runtest.c httploader.c
runtest_DEPENDENCIES = $(DEPS)
runtest_LDADD= $(THREAD_LIBS) $(Z_LIBS) $(LDADDS)

testrecurse_SOURCES=testrecurse.c
testrecurse_DEPENDENCIES = $(DEPS)
testrecurse_LDADD= $(LDADDS)

testlimits_SOURCES=testlimits.c
testlimits_DEPENDENCIES = $(DEPS)
testlimits_LDADD= $(LDADDS)

testcatalog_SOURCES=testcatalog.c
testcatalog_DEPENDENCIES = $(DEPS)
testcatalog_LDADD= $(LDADDS)

testchar_SOURCES=testchar.c
testchar_DEPENDENCIES = $(DEPS)
testchar_LDADD= $(LDADDS)

testdict_SOURCES=testdict.c
testdict_DEPENDENCIES = $(DEPS)
testdict_LDADD= $(LDADDS)

testparser_SOURCES=testparser.c
testparser_DEPENDENCIES = $(DEPS)
testparser_LDADD= $(LDADDS)

runsuite_SOURCES=runsuite.c
runsuite_DEPENDENCIES = $(DEPS)
runsuite_LDADD= $(LDADDS)

testModule_SOURCES=testModule.c
testModule_DEPENDENCIES = $(DEPS)
testModule_LDADD= $(LDADDS)

noinst_LTLIBRARIES = testdso.la
testdso_la_SOURCES = testdso.c
testdso_la_LDFLAGS = $(AM_LDFLAGS) \
		     -module -no-undefined -avoid-version -rpath $(libdir)

rebuild_testapi:
	cd $(top_srcdir)/bxml && python3 codegen/genTestApi.py $(abs_builddir)

testapi_SOURCES=testapi.c
testapi_DEPENDENCIES = $(DEPS)
testapi_LDADD= $(LDADDS)

runxmlconf_SOURCES=runxmlconf.c
runxmlconf_DEPENDENCIES = $(DEPS)
runxmlconf_LDADD= $(LDADDS)

check-local:
	[ -d test   ] || $(LN_S) $(srcdir)/test  test
	[ -d result ] || $(LN_S) $(srcdir)/result result
	$(CHECKER) ./runtest$(EXEEXT) 2> /dev/null
	$(CHECKER) ./testrecurse$(EXEEXT)
	$(CHECKER) ./testapi$(EXEEXT)
	$(CHECKER) ./testcatalog$(EXEEXT)
	$(CHECKER) ./testchar$(EXEEXT)
	$(CHECKER) ./testdict$(EXEEXT)
	$(CHECKER) ./testparser$(EXEEXT)
	$(CHECKER) ./testModule$(EXEEXT)
	$(CHECKER) ./runxmlconf$(EXEEXT) -d $(srcdir)/test/xmlconf
	$(CHECKER) ./runsuite$(EXEEXT)
if WITH_OUTPUT_SOURCES
if WITH_DEBUG_SOURCES
	test/scripts/test.sh $(top_builddir)/bxml/xmllint$(EXEEXT)
endif
if WITH_CATALOG_SOURCES
	ASAN_OPTIONS=detect_leaks=0 test/catalogs/test.sh $(top_builddir)/bxml/xmlcatalog$(EXEEXT)
	ASAN_OPTIONS=detect_leaks=0 test/catalogs/test_sgml.sh $(top_builddir)/bxml/xmlcatalog$(EXEEXT)
endif
endif

# Compatibility names so the top-level `tests`/`accept` loop (tests/Makefile.am)
# can drive this directory the same way it drives the other tests/* dirs.

runtests: check

S2O = | ${SED} '1,/@@/d'

test tests:
	@${MKDIR} -p out
	-@(for base in check; do \
	    echo "... $$base ..." ; \
	    $(MAKE) $$base > out/check.out 2> out/check.err ; \
	    ${DIFF} -Nu ${srcdir}/saved/$$base.out out/$$base.out ${S2O} ; \
	    ${DIFF} -Nu ${srcdir}/saved/$$base.err out/$$base.err ${S2O} ; \
	done);

accept:
	-@(for base in check; do \
	    ${CP} out/$$base.out ${srcdir}/saved/$$base.out ; \
	    ${CP} out/$$base.err ${srcdir}/saved/$$base.err ; \
	done);

VALGRIND = valgrind -q --leak-check=full --error-exitcode=1

check-valgrind valgrind:
	@echo '## Running the regression tests under Valgrind'
	@echo '## Go get a cup of coffee it is gonna take a while ...'
	$(MAKE) check CHECKER="$(LIBTOOL) --mode=execute $(VALGRIND)"

CLEANFILES = missing.lst runsuite.log runxmlconf.log test.out \
	     *.gcda *.gcno *.res test result

dist-hook:
	(cd $(srcdir) ; tar -cf - --exclude .git test result) | (cd $(distdir); tar xf -)
