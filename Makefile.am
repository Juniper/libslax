#
# $Id$
#
# Copyright 2011, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

ACLOCAL_AMFLAGS = -I m4

SUBDIRS = libslax extensions slaxproc tests doc bin
bin_SCRIPTS=slax-config
dist_doc_DATA = Copyright doc/slax.txt

EXTRA_DIST = slax-config.in warnings.mk README.md INSTALL

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

pkgconfigdir=$(libdir)/pkgconfig
pkgconfig_DATA = libslax.pc

get-wiki:
	git clone https://github.com/Juniper/libslax.wiki.git wiki

get-gh-pages:
	git clone https://github.com/Juniper/libslax.git gh-pages -b gh-pages
