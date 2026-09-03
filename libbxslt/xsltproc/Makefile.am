include ${top_srcdir}/lto.mk

AM_CPPFLAGS = \
    -I$(top_builddir) \
    -I$(top_srcdir) \
    -I$(top_srcdir)/libbxslt \
    -I$(top_srcdir)/libbxslt/libxslt \
    -I$(top_srcdir)/libbxslt/libexslt \
    -I$(top_builddir)/libbxslt \
    -I$(top_builddir)/libbxslt/libxslt \
    -I$(top_builddir)/libbxslt/libexslt \
    -I$(top_builddir)/libbxml/include \
    -I$(top_srcdir)/libbxml/include

AM_CFLAGS = $(LTO_CFLAGS)

bin_PROGRAMS = xsltproc

check_PROGRAMS = testThreads

xsltproc_SOURCES = xsltproc.c
xsltproc_LDFLAGS = $(LTO_LDFLAGS) $(LTO_STATIC_LDFLAGS)
xsltproc_DEPENDENCIES = $(DEPS)

testThreads_SOURCES=testThreads.c
testThreads_LDFLAGS =
testThreads_DEPENDENCIES = $(DEPS)
testThreads_LDADD=  $(THREAD_LIBS) $(LDADDS)

DEPS = $(top_builddir)/libbxslt/libxslt/libbxslt.la \
	$(top_builddir)/libbxslt/libexslt/libexslt.la

LDADDS = $(top_builddir)/libbxslt/libxslt/libbxslt.la \
         $(top_builddir)/libbxslt/libexslt/libexslt.la \
	$(top_builddir)/libbxml/libbxml.la $(EXTRA_LIBS) $(LIBM)

xsltproc_LDADD = $(LDADDS)

CLEANFILES = .memdump

$(top_builddir)/libbxslt/libxslt/libbxslt.la:
	cd $(top_builddir)/libbxslt/libxslt && $(MAKE) libbxslt.la

$(top_builddir)/libbxslt/libexslt/libexslt.la: \
		$(top_builddir)/libbxslt/libxslt/libbxslt.la
	cd $(top_builddir)/libbxslt/libexslt && $(MAKE) libexslt.la

check-local:
	@echo > .memdump
	@echo '## Running testThreads'
	@($(CHECKER) ./testThreads > /dev/null; \
		grep "MORY ALLO" .memdump \
			| grep -v "MEMORY ALLOCATED : 0" || true)
