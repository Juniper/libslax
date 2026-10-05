AUTOMAKE_OPTIONS = -Wno-syntax
EXTRA_PROGRAMS = genSeed
if HAVE_LIBFUZZER
EXTRA_PROGRAMS += api html lint reader regexp schema uri valid xinclude \
		 xml xpath
endif
check_PROGRAMS = testFuzzer
EXTRA_DIST = html.dict regexp.dict schema.dict xml.dict xpath.dict \
	     fuzz.h
CLEANFILES = $(EXTRA_PROGRAMS)
AM_CPPFLAGS = -I$(top_srcdir)/libbxml/include -I$(top_builddir)/libbxml/include
DEPENDENCIES = $(top_builddir)/libbxml/libbxml.la
LDADD = $(top_builddir)/libbxml/libbxml.la

XML_MAX_LEN = 80000
# Single quotes to avoid wildcard expansion by the shell
XML_SEED_CORPUS_SRC = \
    '$(top_srcdir)/tests/bxml/test/*' \
    '$(top_srcdir)/tests/bxml/test/errors/*.xml' \
    '$(top_srcdir)/tests/bxml/test/errors10/*.xml' \
    '$(top_srcdir)/tests/bxml/test/namespaces/*' \
    '$(top_srcdir)/tests/bxml/test/recurse/*.xml' \
    '$(top_srcdir)/tests/bxml/test/SVG/*.xml' \
    '$(top_srcdir)/tests/bxml/test/valid/*.xml' \
    '$(top_srcdir)/tests/bxml/test/VC/*' \
    '$(top_srcdir)/tests/bxml/test/VCM/*' \
    '$(top_srcdir)/tests/bxml/test/XInclude/docs/*' \
    '$(top_srcdir)/tests/bxml/test/XInclude/without-reader/*' \
    '$(top_srcdir)/tests/bxml/test/xmlid/*'

testFuzzer_SOURCES = testFuzzer.c fuzz.c

.PHONY: corpus clean-corpus

corpus: seed/html.stamp seed/lint.stamp seed/reader.stamp \
    seed/schema.stamp seed/valid.stamp seed/xinclude.stamp \
    seed/xml.stamp seed/xpath.stamp

check-local: corpus
	./testFuzzer$(EXEEXT)

clean-corpus:
	rm -rf seed

clean-local: clean-corpus

# Seed corpus

genSeed_SOURCES = genSeed.c fuzz.c

seed/xml.stamp: genSeed$(EXEEXT)
	@mkdir -p seed/xml
	./genSeed$(EXEEXT) xml $(XML_SEED_CORPUS_SRC)
	@touch seed/xml.stamp

seed/valid.stamp: genSeed$(EXEEXT)
	@mkdir -p seed/valid
	./genSeed$(EXEEXT) valid $(XML_SEED_CORPUS_SRC)
	@touch seed/valid.stamp

seed/xinclude.stamp: genSeed$(EXEEXT)
	@mkdir -p seed/xinclude
	./genSeed$(EXEEXT) xinclude $(XML_SEED_CORPUS_SRC)
	@touch seed/xinclude.stamp

seed/html.stamp: genSeed$(EXEEXT)
	@mkdir -p seed/html
	./genSeed$(EXEEXT) html '$(top_srcdir)/tests/bxml/test/HTML/*'
	@touch seed/html.stamp

seed/reader.stamp: genSeed$(EXEEXT)
	@mkdir -p seed/reader
	./genSeed$(EXEEXT) reader $(XML_SEED_CORPUS_SRC)
	@touch seed/reader.stamp

seed/lint.stamp: genSeed$(EXEEXT)
	@mkdir -p seed/lint
	./genSeed$(EXEEXT) lint $(XML_SEED_CORPUS_SRC)
	@touch seed/lint.stamp

seed/schema.stamp: genSeed$(EXEEXT)
	@mkdir -p seed/schema
	./genSeed$(EXEEXT) schema '$(top_srcdir)/tests/bxml/test/schemas/*.xsd'
	@touch seed/schema.stamp

seed/xpath.stamp: genSeed$(EXEEXT)
	@mkdir -p seed/xpath
	./genSeed$(EXEEXT) xpath '$(top_srcdir)/tests/bxml/test/XPath'
	@touch seed/xpath.stamp

# libFuzzer targets, built only when the compiler supports -fsanitize=fuzzer

if HAVE_LIBFUZZER

xml_SOURCES = xml.c fuzz.c
xml_LDFLAGS = $(AM_LDFLAGS) -fsanitize=fuzzer

fuzz-xml: xml$(EXEEXT) seed/xml.stamp
	@mkdir -p corpus/xml
	./xml$(EXEEXT) \
	    -dict=$(srcdir)/xml.dict \
	    -max_len=$(XML_MAX_LEN) \
	    $$XML_FUZZ_OPTIONS \
	    corpus/xml seed/xml

valid_SOURCES = valid.c fuzz.c
valid_LDFLAGS = $(AM_LDFLAGS) -fsanitize=fuzzer

fuzz-valid: valid$(EXEEXT) seed/valid.stamp
	@mkdir -p corpus/valid
	./valid$(EXEEXT) \
	    -dict=$(srcdir)/xml.dict \
	    -max_len=$(XML_MAX_LEN) \
	    $$XML_FUZZ_OPTIONS \
	    corpus/valid seed/valid

xinclude_SOURCES = xinclude.c fuzz.c
xinclude_LDFLAGS = $(AM_LDFLAGS) -fsanitize=fuzzer

fuzz-xinclude: xinclude$(EXEEXT) seed/xinclude.stamp
	@mkdir -p corpus/xinclude
	./xinclude$(EXEEXT) \
	    -dict=$(srcdir)/xml.dict \
	    -max_len=$(XML_MAX_LEN) \
	    $$XML_FUZZ_OPTIONS \
	    corpus/xinclude seed/xinclude

html_SOURCES = html.c fuzz.c
html_LDFLAGS = $(AM_LDFLAGS) -fsanitize=fuzzer

fuzz-html: html$(EXEEXT) seed/html.stamp
	@mkdir -p corpus/html
	./html$(EXEEXT) \
	    -dict=$(srcdir)/html.dict \
	    -max_len=1000000 \
	    $$XML_FUZZ_OPTIONS \
	    corpus/html seed/html

reader_SOURCES = reader.c fuzz.c
reader_LDFLAGS = $(AM_LDFLAGS) -fsanitize=fuzzer

fuzz-reader: reader$(EXEEXT) seed/reader.stamp
	@mkdir -p corpus/reader
	./reader$(EXEEXT) \
	    -dict=$(srcdir)/xml.dict \
	    -max_len=$(XML_MAX_LEN) \
	    $$XML_FUZZ_OPTIONS \
	    corpus/reader seed/reader

lint_SOURCES = lint.c fuzz.c ../xmllint.c ../shell.c
lint_LDFLAGS = -fsanitize=fuzzer

fuzz-lint: lint$(EXEEXT) seed/lint.stamp
	@mkdir -p corpus/lint
	./lint$(EXEEXT) \
	    -dict=$(srcdir)/xml.dict \
	    -max_len=$(XML_MAX_LEN) \
	    $$XML_FUZZ_OPTIONS \
	    corpus/lint seed/lint

api_SOURCES = api.c fuzz.c
api_LDFLAGS = -fsanitize=fuzzer

fuzz-api: api$(EXEEXT)
	@mkdir -p corpus/api
	./api$(EXEEXT) \
	    -max_len=100 \
	    $$XML_FUZZ_OPTIONS \
	    corpus/api

regexp_SOURCES = regexp.c fuzz.c
regexp_LDFLAGS = $(AM_LDFLAGS) -fsanitize=fuzzer

fuzz-regexp: regexp$(EXEEXT)
	@mkdir -p corpus/regexp
	./regexp$(EXEEXT) \
	    -dict=$(srcdir)/regexp.dict \
	    -max_len=200 \
	    $$XML_FUZZ_OPTIONS \
	    corpus/regexp

uri_SOURCES = uri.c fuzz.c
uri_LDFLAGS = $(AM_LDFLAGS) -fsanitize=fuzzer

fuzz-uri: uri$(EXEEXT)
	@mkdir -p corpus/uri
	./uri$(EXEEXT) \
	    -max_len=10000 \
	    $$XML_FUZZ_OPTIONS \
	    corpus/uri

schema_SOURCES = schema.c fuzz.c
schema_LDFLAGS = $(AM_LDFLAGS) -fsanitize=fuzzer

fuzz-schema: schema$(EXEEXT) seed/schema.stamp
	@mkdir -p corpus/schema
	./schema$(EXEEXT) \
	    -dict=$(srcdir)/schema.dict \
	    -max_len=$(XML_MAX_LEN) \
	    $$XML_FUZZ_OPTIONS \
	    corpus/schema seed/schema

xpath_SOURCES = xpath.c fuzz.c
xpath_LDFLAGS = $(AM_LDFLAGS) -fsanitize=fuzzer

fuzz-xpath: xpath$(EXEEXT) seed/xpath.stamp
	@mkdir -p corpus/xpath
	./xpath$(EXEEXT) \
	    -dict=$(srcdir)/xpath.dict \
	    -max_len=10000 \
	    $$XML_FUZZ_OPTIONS \
	    corpus/xpath seed/xpath

endif
