## Process this file with automake to produce Makefile.in

AM_CPPFLAGS = \
-I$(top_builddir) \
-I$(top_srcdir) \
-I$(top_srcdir)/libbxslt \
-I$(top_builddir)/libbxslt \
-I$(top_builddir)/libbxml/include \
-I$(top_srcdir)/libbxml/include

SUBDIRS = xmlspec multiple xinclude XSLTMark docbook fuzz

LINKDIRS = \
	REC \
	REC2 \
	XSLTMark \
	docbook \
	documents \
	encoding \
	exslt \
	extensions \
	fuzz \
	general \
	keys \
	multiple \
	namespaces \
	numbers \
	plugins \
	reports \
	xinclude \
	xmlspec

DEPS = $(top_builddir)/libbxslt/libxslt/libbxslt.la \
       $(top_builddir)/libbxslt/libexslt/libexslt.la \
       $(top_builddir)/libbxml/libbxml.la

DEPENDENCIES = $(DEPS)

LDADD = $(DEPS)

noinst_PROGRAMS = runtest

runtest_SOURCES = runtest.c

if WITH_XSLT_MODULES

check_LTLIBRARIES = xmlsoft_org_xslt_testplugin.la

# our rpath is a rather unorthodox location as we
# don't want to pollute $(DESTDIR) with the test plugin

plugindir = $(abs_builddir)/.libs

xmlsoft_org_xslt_testplugin_la_CFLAGS = $(AM_CFLAGS) -DMODULE_COMPILE
xmlsoft_org_xslt_testplugin_la_SOURCES = testplugin.c
xmlsoft_org_xslt_testplugin_la_LIBADD = $(top_builddir)/libbxslt/libxslt/libbxslt.la $(top_builddir)/libbxml/libbxml.la $(EXTRA_LIBS)
xmlsoft_org_xslt_testplugin_la_LDFLAGS = \
	$(AM_LDFLAGS) -no-undefined \
	-module -avoid-version -rpath $(plugindir)

endif

all:

check-local:
	cd $(srcdir) && LIBXSLT_PLUGINS_PATH=$(plugindir) $(abs_builddir)/runtest

S2O = | ${SED} '1,/@@/d'

SHOW_DIFFS = \
 ${DIFF} -Nbu ${srcdir}/saved/$$file.out out/$$file.out ${S2O} ; \
 ${DIFF} -Nbu ${srcdir}/saved/$$file.err out/$$file.err ${S2O}

test tests: work-links
	@${MKDIR_P} out
	@for file in check; do \
		echo "... bxslt ... $$file ..." ; \
		( cd work ; \
		LIBXSLT_PLUGINS_PATH=$(plugindir) $(abs_builddir)/runtest ) \
		> out/check.out 2> out/check.err ; \
		${SHOW_DIFFS} ; \
		done

accept:
	-@(for file in check ; do \
	      echo "... bxslt ... $$file ..."; \
	      ${CP} out/$$file.out ${srcdir}/saved/$$file.out ; \
	      ${CP} out/$$file.err ${srcdir}/saved/$$file.err ; \
	done; true)

work-links:
	@${MKDIR_P} work
	@-for i in ${LINKDIRS}; do \
		${LN_S} -f ${top_srcdir}/../tests/bxslt/$$i work/$$i; \
		done

# runtest has its own built-in "update results in place" mode (-u),
# which overwrites the .out/.err baselines next to each test case in
# $(srcdir) with freshly generated output -- exactly the "copy generated
# files from the build tree to the source side" accept step, so there's
# no separate out/saved indirection to maintain here.
bad-accept:
	cd $(srcdir) && LIBXSLT_PLUGINS_PATH=$(plugindir) $(abs_builddir)/runtest -u

docbook_tests:
	@(cd docbook ; $(MAKE) full)

dist-hook:
	cp -a $(srcdir)/REC $(distdir)
	cp -a $(srcdir)/REC2 $(distdir)
	cp -a $(srcdir)/documents $(distdir)
	cp -a $(srcdir)/encoding $(distdir)
	cp -a $(srcdir)/exslt $(distdir)
	cp -a $(srcdir)/extensions $(distdir)
	cp -a $(srcdir)/general $(distdir)
	cp -a $(srcdir)/keys $(distdir)
	cp -a $(srcdir)/namespaces $(distdir)
	cp -a $(srcdir)/numbers $(distdir)
	cp -a $(srcdir)/plugins $(distdir)
	cp -a $(srcdir)/reports $(distdir)
