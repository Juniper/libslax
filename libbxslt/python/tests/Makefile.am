if WITH_XSLT_PYTHON
TESTSPY =	\
    basic.py	\
    exslt.py	\
    extelem.py	\
    extfunc.py

exampledir = $(docdir)/python/examples
dist_example_DATA = test.xml test.xsl pyxsltproc.py setup_test.py $(TESTSPY)

check-local: $(TESTSPY)
	@(export PYTHONPATH="..:../.libs:$(srcdir)/..:$$PYTHONPATH"; \
	  export LD_LIBRARY_PATH="$(top_builddir)/bxslt/libxslt/.libs:$(top_builddir)/bxslt/libexslt/.libs:$$LD_LIBRARY_PATH"; \
	  export DYLD_LIBRARY_PATH="$(top_builddir)/bxslt/libxslt/.libs:$(top_builddir)/bxslt/libexslt/.libs:$$DYLD_LIBRARY_PATH"; \
	  export PATH="$(top_builddir)/bxslt/libxslt/.libs:$(top_builddir)/bxslt/libexslt/.libs:$$PATH" ; \
	  for test in $(TESTSPY) ; do \
	    echo "-- $$test" ; \
	    $(PYTHON) $(srcdir)/$$test; \
	  done)
endif

CLEANFILES = *.pyc core
