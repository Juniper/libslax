if WITH_XSLT_PYTHON
TESTSPY =	\
    basic.py	\
    exslt.py	\
    extelem.py	\
    extfunc.py

exampledir = $(docdir)/python/examples
dist_example_DATA = test.xml test.xsl pyxsltproc.py setup_test.py $(TESTSPY)

check-local: $(TESTSPY)
	@(export PYTHONPATH="$(top_builddir)/libbxslt/python:$(top_builddir)/libbxslt/python/.libs:$(top_srcdir)/libbxslt/python:$(top_builddir)/libbxml/python:$(top_builddir)/libbxml/python/.libs:$(top_srcdir)/libbxml/python:$$PYTHONPATH"; \
	  export LD_LIBRARY_PATH="$(top_builddir)/libbxslt/libxslt/.libs:$(top_builddir)/libbxslt/libexslt/.libs:$(top_builddir)/libbxml/.libs:$$LD_LIBRARY_PATH"; \
	  export DYLD_LIBRARY_PATH="$(top_builddir)/libbxslt/libxslt/.libs:$(top_builddir)/libbxslt/libexslt/.libs:$(top_builddir)/libbxml/.libs:$$DYLD_LIBRARY_PATH"; \
	  export PATH="$(top_builddir)/libbxslt/libxslt/.libs:$(top_builddir)/libbxslt/libexslt/.libs:$$PATH" ; \
	  for test in $(TESTSPY) ; do \
	    echo "-- $$test" ; \
	    $(PYTHON) -B $(srcdir)/$$test; \
	  done)
endif

CLEANFILES = *.pyc core
