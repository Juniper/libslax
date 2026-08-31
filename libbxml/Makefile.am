## Process this file with automake to produce Makefile.in

ACLOCAL_AMFLAGS = -I m4

include ${top_srcdir}/lto.mk

SUBDIRS = include . example
if WITH_DOXYGEN
SUBDIRS += doc
endif
if WITH_PYTHON
SUBDIRS += python
endif
if WITH_GLOB
SUBDIRS += fuzz
endif

DIST_SUBDIRS = include . doc example fuzz python

AM_CPPFLAGS = \
    -I$(top_builddir) \
    -I$(top_srcdir) \
    -I$(builddir)/include \
    -I$(srcdir)/include

bin_PROGRAMS = xmllint

bin_SCRIPTS = bxml-config

lib_LTLIBRARIES = libbxml.la
libbxml_la_CFLAGS = $(AM_CFLAGS) $(XML_PRIVATE_CFLAGS) $(LTO_CFLAGS)
libbxml_la_LIBADD = $(XML_PRIVATE_LIBS)

libbxml_la_LDFLAGS = \
    $(AM_LDFLAGS) \
    $(LTO_LDFLAGS) \
    -no-undefined \
    -version-info \
    $(LIBXML_VERSION_INFO)

libbxml_la_SOURCES = \
    buf.c \
    chvalid.c \
    dict.c \
    entities.c \
    encoding.c \
    error.c \
    globals.c \
    hash.c \
    list.c \
    parser.c \
    parserInternals.c \
    SAX2.c \
    threads.c \
    tree.c \
    uri.c \
    valid.c \
    xmlIO.c \
    xmlmemory.c \
    xmlstring.c

#
# xmlNode/xmlNs/xmlAttr/xmlDoc/xmlDtd accessor functions, generated
# from the field table in gen-xmlaccessors.sh in both inline
# and non-inline
#
XML_ACCESSORS_GEN = $(top_srcdir)/bin/gen-xmlaccessors.sh

GENACC_H_DIR = include/libxml/gen
GENACC_C_FILES = \
	gen/xmlaccessors.c

GENACC_H_FILES = \
	$(GENACC_H_DIR)/xmlaccessors-inline.h \
	$(GENACC_H_DIR)/xmlaccessors-decl.h

GENACC_FILES = ${GENACC_H_FILES} ${GENACC_C_FILES}

#
# GENACC_FILES lists three files produced by one recipe. Under parallel
# make, giving that recipe directly to "${GENACC_FILES}:" lets each of
# the three targets independently decide it's out of date and race to
# run the script concurrently, corrupting the output. Routing through a
# single stamp file means only one target ever carries the recipe, so
# make's own per-target serialization keeps it from running twice.
#
GENACC_STAMP = gen/xmlaccessors-stamp

$(GENACC_STAMP): $(XML_ACCESSORS_GEN)
	@$(AM_V_GEN)$(SHELL) $(XML_ACCESSORS_GEN) $(top_srcdir) $(top_builddir)
	@touch $@

${GENACC_FILES}: $(GENACC_STAMP)
	@if test -f $@; then :; else \
	    rm -f $(GENACC_STAMP); \
	    $(MAKE) $(AM_MAKEFLAGS) $(GENACC_STAMP); \
	fi

BUILT_SOURCES = $(GENACC_STAMP) ${GENACC_FILES}

nodist_libbxml_la_SOURCES = $(GENACC_C_FILES)

CLEANFILES = ${GENACC_FILES} $(GENACC_STAMP)

if WITH_C14N_SOURCES
libbxml_la_SOURCES += c14n.c
endif
if WITH_CATALOG_SOURCES
if WITH_OUTPUT_SOURCES
bin_PROGRAMS += xmlcatalog

xmlcatalog_SOURCES = xmlcatalog.c
xmlcatalog_CFLAGS = $(AM_CFLAGS) $(RDL_CFLAGS) $(LTO_CFLAGS)
xmlcatalog_DEPENDENCIES = $(DEPS)
xmlcatalog_LDADD = $(RDL_LIBS) $(LDADDS)
xmlcatalog_LDFLAGS = $(LTO_LDFLAGS) $(LTO_STATIC_LDFLAGS)
endif

libbxml_la_SOURCES += catalog.c
endif
if WITH_DEBUG_SOURCES
libbxml_la_SOURCES += debugXML.c
endif
if WITH_HTML_SOURCES
libbxml_la_SOURCES += HTMLparser.c HTMLtree.c
endif
if WITH_HTTP_SOURCES
libbxml_la_SOURCES += nanohttp.c
endif
if WITH_MODULES_SOURCES
libbxml_la_SOURCES += xmlmodule.c
endif
if WITH_OUTPUT_SOURCES
libbxml_la_SOURCES += xmlsave.c
endif
if WITH_PATTERN_SOURCES
libbxml_la_SOURCES += pattern.c
endif
if WITH_READER_SOURCES
libbxml_la_SOURCES += xmlreader.c
endif
if WITH_REGEXPS_SOURCES
libbxml_la_SOURCES += xmlregexp.c
endif
if WITH_RELAXNG_SOURCES
libbxml_la_SOURCES += relaxng.c
endif
if WITH_SCHEMAS_SOURCES
libbxml_la_SOURCES += xmlschemas.c xmlschemastypes.c
endif
if WITH_SCHEMATRON_SOURCES
libbxml_la_SOURCES += schematron.c
endif
if WITH_WRITER_SOURCES
libbxml_la_SOURCES += xmlwriter.c
endif
if WITH_XINCLUDE_SOURCES
libbxml_la_SOURCES += xinclude.c
endif
if WITH_XPATH_SOURCES
libbxml_la_SOURCES += xpath.c
endif
if WITH_XPTR_SOURCES
libbxml_la_SOURCES += xlink.c xpointer.c
endif

DEPS = libbxml.la
LDADDS = libbxml.la

xmllint_SOURCES = xmllint.c shell.c lintmain.c
xmllint_CFLAGS = $(AM_CFLAGS) $(RDL_CFLAGS) $(Z_CFLAGS) $(LTO_CFLAGS)
xmllint_DEPENDENCIES = $(DEPS)
xmllint_LDADD = $(RDL_LIBS) $(Z_LIBS) $(LDADDS)
xmllint_LDFLAGS = $(LTO_LDFLAGS) $(LTO_STATIC_LDFLAGS)

dist-hook:
	(cd $(srcdir) ; \
	    tar -cf - --exclude .git win32 result) | (cd $(distdir); tar xf -)

EXTRA_DIST = \
    Copyright \
    autogen.sh \
    libxml.h \
    codegen/charset.inc \
    codegen/escape.inc \
    codegen/genCharset.py \
    codegen/genEscape.py \
    codegen/genHtml5Ent.py \
    codegen/genHtml5LibTests.py \
    codegen/genRanges.py \
    codegen/genTestApi.py \
    codegen/genUnicode.py \
    codegen/html5ent.inc \
    codegen/ranges.def \
    codegen/ranges.inc \
    codegen/rangetab.py \
    codegen/unicode.inc \
    codegen/xmlmod.py \
    timsort.h \
    README.zOS \
    README.md

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libxml-2.0.pc
