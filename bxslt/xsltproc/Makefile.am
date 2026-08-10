AM_CPPFLAGS = -I$(top_builddir) \
	      -I$(top_srcdir)/bxslt -I$(top_srcdir)/bxslt/libxslt -I$(top_srcdir)/bxslt/libexslt \
	      -I$(top_builddir)/bxslt -I$(top_builddir)/bxslt/libxslt \
	      -I$(top_builddir)/bxslt/libexslt \
	      -I$(top_builddir)/bxml/include -I$(top_srcdir)/bxml/include

bin_PROGRAMS = xsltproc

check_PROGRAMS = testThreads

xsltproc_SOURCES = xsltproc.c
xsltproc_LDFLAGS = 
xsltproc_DEPENDENCIES = $(DEPS)

testThreads_SOURCES=testThreads.c
testThreads_LDFLAGS =
testThreads_DEPENDENCIES = $(DEPS)
testThreads_LDADD=  $(THREAD_LIBS) $(LDADDS)

DEPS = $(top_builddir)/bxslt/libxslt/libbxslt.la \
	$(top_builddir)/bxslt/libexslt/libexslt.la

LDADDS = $(top_builddir)/bxslt/libxslt/libbxslt.la \
         $(top_builddir)/bxslt/libexslt/libexslt.la \
	$(top_builddir)/bxml/libbxml.la $(EXTRA_LIBS) $(LIBM)

xsltproc_LDADD = $(LDADDS)

CLEANFILES = .memdump

$(top_builddir)/bxslt/libxslt/libbxslt.la:
	cd $(top_builddir)/bxslt/libxslt && $(MAKE) libbxslt.la

$(top_builddir)/bxslt/libexslt/libexslt.la: $(top_builddir)/bxslt/libxslt/libbxslt.la
	cd $(top_builddir)/bxslt/libexslt && $(MAKE) libexslt.la

check-local:
	@echo > .memdump
	@echo '## Running testThreads'
	@($(CHECKER) ./testThreads > /dev/null; grep "MORY ALLO" .memdump  | grep -v "MEMORY ALLOCATED : 0" || true)
