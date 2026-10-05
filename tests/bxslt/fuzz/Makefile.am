LIBXSLT_LIBS = $(top_builddir)/libbxslt/libxslt/libbxslt.la \
               $(top_builddir)/libbxslt/libexslt/libexslt.la

EXTRA_PROGRAMS = genSeed
if HAVE_LIBFUZZER
EXTRA_PROGRAMS += xpath xslt
endif
noinst_PROGRAMS = testTargets
EXTRA_DIST = xpath.dict xslt.dict
CLEANFILES = $(EXTRA_PROGRAMS)

AM_CPPFLAGS = \
-I$(top_builddir) \
-I$(top_srcdir) \
-I$(top_builddir)/libbxslt \
-I$(top_srcdir)/libbxslt \
-I$(top_builddir)/libbxml/include \
-I$(top_srcdir)/libbxml/include

DEPENDENCIES = $(LIBXSLT_LIBS)
LDADD = $(LIBXSLT_LIBS) \
        $(top_builddir)/libbxml/libbxml.la $(EXTRA_LIBS) $(LIBM)

testTargets_SOURCES = testTargets.c fuzz.c fuzz.h

$(top_builddir)/libbxslt/libxslt/libbxslt.la:
	cd $(top_builddir)/libbxslt/libxslt && $(MAKE) libbxslt.la

$(top_builddir)/libbxslt/libexslt/libexslt.la: $(top_builddir)/libbxslt/libxslt/libbxslt.la
	cd $(top_builddir)/libbxslt/libexslt && $(MAKE) libexslt.la

.PHONY: fuzz-xpath fuzz-xslt

check-local: $(noinst_PROGRAMS)
	@echo '## Running fuzz target tests'
	@./testTargets

# Seed corpus

genSeed_SOURCES = genSeed.c fuzz.c

# XPath fuzzer

seed/xpath.stamp: genSeed$(EXEEXT)
	@mkdir -p seed/xpath
	@./genSeed$(EXEEXT) xpath "$(top_srcdir)/tests/bxslt"
	@touch seed/xpath.stamp

seed/xslt.stamp: genSeed$(EXEEXT)
	@mkdir -p seed/xslt
	@./genSeed$(EXEEXT) xslt "$(top_srcdir)/tests/bxslt"
	@touch seed/xslt.stamp

# libFuzzer targets, built only when the compiler supports -fsanitize=fuzzer

if HAVE_LIBFUZZER

xpath_SOURCES = xpath.c fuzz.c fuzz.h
xpath_LDFLAGS = -fsanitize=fuzzer

fuzz-xpath: xpath$(EXEEXT) seed/xpath.stamp
	@mkdir -p corpus/xpath
	./xpath$(EXEEXT) \
            -dict=$(srcdir)/xpath.dict \
	    corpus/xpath seed/xpath

xslt_SOURCES = xslt.c fuzz.c fuzz.h
xslt_LDFLAGS = -fsanitize=fuzzer

fuzz-xslt: xslt$(EXEEXT) seed/xslt.stamp
	@mkdir -p corpus/xslt
	./xslt$(EXEEXT) \
            -dict=$(srcdir)/xslt.dict \
	    corpus/xslt seed/xslt

endif

