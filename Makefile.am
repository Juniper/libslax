#
# $Id$
#

ACLOCAL_AMFLAGS = -I m4

SUBDIRS = libslax slaxproc tests doc
bin_SCRIPTS=slax-config
dist_doc_DATA = Copyright README INSTALL doc/slax.txt

EXTRA_DIST = slax-config.in warnings.mk

.PHONY: test tests

test tests:
	@(cd tests ; ${MAKE} tests)

errors:
	@(cd tests/errors ; ${MAKE} test)

docs:
	@(cd doc ; ${MAKE} docs)
