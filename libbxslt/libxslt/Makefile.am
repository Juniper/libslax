include ${top_srcdir}/lto.mk

AM_CPPFLAGS = \
-I$(top_builddir) \
-I$(top_srcdir) \
-I$(top_builddir)/bxslt \
-I$(top_srcdir)/bxslt \
-I$(top_srcdir)/bxslt/libxslt \
-I$(top_builddir)/libbxml/include \
-I$(top_srcdir)/libbxml/include

AM_CPPFLAGS += -DMODULE_EXTENSION='"@MODULE_EXTENSION@"'
AM_CFLAGS = $(LTO_CFLAGS)

#
# Accessor functions for libxslt's own internal structs, generated
# from the field table in gen-xsltaccessors.sh in both inline
# and non-inline flavors
#
XSLT_ACCESSORS_GEN = $(top_srcdir)/bin/gen-xsltaccessors.sh

GENACC_H_DIR = gen
GENACC_C_FILES = \
	../gen/xsltaccessors.c

GENACC_H_FILES = \
	$(GENACC_H_DIR)/xsltaccessors-inline.h \
	$(GENACC_H_DIR)/xsltaccessors-decl.h

GENACC_FILES = ${GENACC_H_FILES} ${GENACC_C_FILES}

#
# GENACC_FILES lists three files produced by one recipe. Under parallel
# make, giving that recipe directly to "${GENACC_FILES}:" lets each of
# the three targets independently decide it's out of date and race to
# run the script concurrently, corrupting the output. Routing through a
# single stamp file means only one target ever carries the recipe, so
# make's own per-target serialization keeps it from running twice.
#
GENACC_STAMP = gen/xsltaccessors-stamp

$(GENACC_STAMP): $(XSLT_ACCESSORS_GEN)
	@$(AM_V_GEN)$(SHELL) $(XSLT_ACCESSORS_GEN) $(top_srcdir) $(top_builddir)
	@touch $@

${GENACC_FILES}: $(GENACC_STAMP)
	@if test -f $@; then :; else \
	    rm -f $(GENACC_STAMP); \
	    $(MAKE) $(AM_MAKEFLAGS) $(GENACC_STAMP); \
	fi

BUILT_SOURCES = $(GENACC_STAMP) ${GENACC_FILES}

nodist_libbxslt_la_SOURCES = $(GENACC_C_FILES)

CLEANFILES = ${GENACC_FILES} $(GENACC_STAMP)

lib_LTLIBRARIES = libbxslt.la

xsltincdir = $(includedir)/libxslt

xsltinc_HEADERS = 			\
	xslt.h				\
	xsltutils.h			\
	pattern.h			\
	templates.h			\
	variables.h			\
	keys.h				\
	numbersInternals.h		\
	extensions.h			\
	extra.h				\
	functions.h			\
	namespaces.h			\
	imports.h			\
	attributes.h			\
	documents.h			\
	preproc.h			\
	transform.h			\
	transformInternals.h	\
	security.h			\
	xsltInternals.h			\
	xsltexports.h			\
	xsltlocale.h
nodist_xsltinc_HEADERS = 		\
	xsltconfig.h

libbxslt_la_SOURCES = 			\
	attrvt.c			\
	xslt.c				\
	xsltlocale.c			\
	xsltutils.c			\
	pattern.c			\
	templates.c			\
	variables.c			\
	keys.c				\
	numbers.c			\
	extensions.c			\
	extra.c				\
	functions.c			\
	namespaces.c			\
	imports.c			\
	attributes.c			\
	documents.c			\
	preproc.c			\
	transform.c			\
	security.c			\
	win32config.h			\
	libxslt.h

if USE_VERSION_SCRIPT
LIBXSLT_VERSION_SCRIPT = $(VERSION_SCRIPT_FLAGS)$(srcdir)/libxslt.syms
else
LIBXSLT_VERSION_SCRIPT =
endif

libbxslt_la_LIBADD = $(top_builddir)/libbxml/libbxml.la $(EXTRA_LIBS) $(LIBM)
libbxslt_la_LDFLAGS =					\
		$(AM_LDFLAGS) $(LTO_LDFLAGS) -no-undefined	\
		$(LIBXSLT_VERSION_SCRIPT)		\
		-version-info $(LIBXSLT_VERSION_INFO)

man_MANS = libxslt.3

EXTRA_DIST = $(man_MANS) trio.h triodef.h libxslt.syms

xsltproc: all
	@(cd ../xsltproc ; $(MAKE))

if WITH_XSLT_MODULES
install-exec-hook:
	$(MKDIR_P) "$(DESTDIR)$(libdir)/libxslt-plugins"
endif
