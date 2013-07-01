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

EXTRA_DIST = slax-config.in warnings.mk README.md INSTALL.md

.PHONY: test tests

test tests:
	@(cd tests ; ${MAKE} tests)

errors:
	@(cd tests/errors ; ${MAKE} test)

docs:
	@(cd doc ; ${MAKE} docs)

USER_INFO := $(shell if [ -f .upload ]; then cat .upload; fi )

UPLOAD = python ~/bin/googlecode_upload.py -p libslax ${USER_INFO}
upload-google: dist
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

DIST_FILES_DIR = ~/Dropbox/dist-files/
GH_PAGES_DIR = gh-pages/
PACKAGE_FILE = ${PACKAGE_TARNAME}-${PACKAGE_VERSION}.tar.gz

upload: dist params.json
	@echo "Uploading libslax-${PACKAGE_VERSION}.tar.gz ... "
	@cp libslax-${PACKAGE_VERSION}.tar.gz ${DIST_FILES_DIR}
	@echo "Uploading slax-quick-reference.pdf ... "
	@cp ${srcdir}/doc/slax-quick-reference.pdf ${DIST_FILES_DIR}
	@echo "Uploading slax-manual.html ... "
	@cp doc/slax-manual.html ${DIST_FILES_DIR}
	@-[ -d ${GH_PAGES_DIR} ] \
		&& echo "Updating manual on gh-pages ..." \
		&& cp doc/slax-manual.html ${GH_PAGES_DIR} \
		&& (cd ${GH_PAGES_DIR} \
			&& git commit -m 'new docs' slax-manual.html \
			&& git push origin gh-pages )
	@-[ -d ${GH_PAGES_DIR} ] \
		&& echo "Updating distfiles.json on gh-pages ..." \
		&& sh ${srcdir}/bin/add-distfile.sh \
	           ${PACKAGE_FILE} ${PACKAGE_VERSION} gh-pages/distfiles.json \
		&& (cd ${GH_PAGES_DIR} \
			&& git commit -m 'new release' distfiles.json \
			&& git push origin gh-pages )
	@echo "Remember to run: gt tag ${PACKAGE_VERSION}"

pkgconfigdir=$(libdir)/pkgconfig
pkgconfig_DATA = ${PACKAGE_NAME}.pc

get-wiki:
	git clone https://github.com/Juniper/${PACKAGE_NAME}.wiki.git wiki

get-gh-pages:
	git clone https://github.com/Juniper/${PACKAGE_NAME}.git gh-pages -b gh-pages
