## Process this file with automake to produce Makefile.in

$(top_builddir)/libbxslt/xsltproc/xsltproc:
	@(cd ../../libbxslt/xsltproc ; $(MAKE) xsltproc)

EXTRA_DIST = \
    dict.dtd \
    dict.xml \
    dict.xsl \
    $(addprefix saved/, $(shell cd ${srcdir} && ls saved 2>/dev/null))

CLEANFILES = .memdump
CLEANDIRS = out

all:

test tests:
	@${MAKE} check-local

check-local: $(top_builddir)/libbxslt/xsltproc/xsltproc
	@echo '## Running multiple tests'
	@${MKDIR_P} out
	@(echo > .memdump)
	@$(CHECKER) $(top_builddir)/libbxslt/xsltproc/xsltproc \
		$(srcdir)/dict.xsl $(srcdir)/dict.xml \
		> out/dict.out 2> out/dict.err ; \
	fail=0 ; \
	for i in out/dict.out out/dict.err out/letter*.html out/titlepage.html ; do \
	    base=`${BASENAME} $$i` ; \
	    if [ ! -f ${srcdir}/saved/$$base ] ; then \
		echo "missing ${srcdir}/saved/$$base" ; fail=1 ; \
	    elif ! ${DIFF} -Nbu ${srcdir}/saved/$$base $$i ; then \
	        fail=1 ; \
	    fi ; \
	done ; \
	grep "MORY ALLO" .memdump | grep -v "MEMORY ALLOCATED : 0" || true ; true

accept:
	@${MKDIR_P} ${srcdir}/saved
	@for i in out/dict.out out/dict.err out/letter*.html out/titlepage.html ; do \
	    ${CP} $$i ${srcdir}/saved/`${BASENAME} $$i` ; \
	done
