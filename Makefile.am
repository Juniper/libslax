#
# $Id$
#

ACLOCAL_AMFLAGS = -I m4

SUBDIRS = libslax extensions slaxproc tests doc
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

USER_INFO := $(shell if [ -f .upload ]; then cat .upload; fi )

UPLOAD = python ~/bin/googlecode_upload.py -p libslax ${USER_INFO}
upload: dist
	@echo "Uploading libslax-${PACKAGE_VERSION}.tar.gz ... "
	@${UPLOAD} -s 'Release ${PACKAGE_VERSION}' \
		-l Type-Source,OpSys-All,Featured \
		libslax-${PACKAGE_VERSION}.tar.gz
	@echo "Uploading slax-manual-${PACKAGE_VERSION}.html ... "
	@${UPLOAD} -s 'Full Documentation' -l Type-Docs,Featured \
		doc/slax-manual-${PACKAGE_VERSION}.html
	@echo "Uploading slax-quick-reference-${PACKAGE_VERSION}.pdf ... "
	@${UPLOAD} -s 'Quick Reference Card' -l Type-Docs,Featured \
		doc/slax-quick-reference-${PACKAGE_VERSION}.pdf

svnignore:
	svn propset svn:ignore -F ${srcdir}/.svnignore ${srcdir}
