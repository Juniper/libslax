## Process this file with automake to produce Makefile.in

$(top_builddir)/bxslt/xsltproc/xsltproc:
	@(cd ../../xsltproc ; $(MAKE) xsltproc)

EXTRA_DIST =		\
	e.xml		\
	e.xsl		\
	x1.xml		\
	x2.xml		\
	normal.out	\
	xinclude.out

CLEANFILES = .memdump

check-local: $(top_builddir)/bxslt/xsltproc/xsltproc
	@echo '## Running XInclude test'
	@(echo > .memdump)
	@($(CHECKER) $(top_builddir)/bxslt/xsltproc/xsltproc $(srcdir)/e.xsl $(srcdir)/e.xml > res ; \
	diff $(srcdir)/normal.out res; \
	grep "MORY ALLO" .memdump  | grep -v "MEMORY ALLOCATED : 0" || true;\
	rm -f res)
	@($(CHECKER) $(top_builddir)/bxslt/xsltproc/xsltproc --xinclude $(srcdir)/e.xsl $(srcdir)/e.xml > res ; \
	diff $(srcdir)/xinclude.out res; \
	grep "MORY ALLO" .memdump  | grep -v "MEMORY ALLOCATED : 0" || true;\
	rm -f res)



