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
	@(cd tests ; ${MAKE} test)

errors:
	@(cd tests/errors ; ${MAKE} test)

docs:
	@(cd doc ; ${MAKE} docs)


DIST_FILES_DIR = ~/Dropbox/dist-files/
GH_PAGES_DIR = gh-pages/
PACKAGE_FILE = ${PACKAGE_TARNAME}-${PACKAGE_VERSION}.tar.gz

upload: dist
	@echo "Uploading slax-manual.html ... "
	@-[ -d ${GH_PAGES_DIR} ] \
		&& echo "Updating manual on gh-pages ..." \
		&& cp doc/slax-manual.html ${GH_PAGES_DIR} \
		&& echo "Uploading slax-quick-reference.pdf ... " \
		&& cp ${srcdir}/doc/slax-quick-reference.pdf ${GH_PAGES_DIR} \
		&& (cd ${GH_PAGES_DIR} \
			&& git commit -m 'new docs' \
                               slax-manual.html slax-quick-reference.pdf \
			&& git push origin gh-pages ) ; true
	@echo "Remember to run:"
	@echo "        gt tag ${PACKAGE_VERSION}"

pkgconfigdir=$(libdir)/pkgconfig
pkgconfig_DATA = packaging/${PACKAGE_NAME}.pc

get-wiki:
	git clone https://github.com/Juniper/${PACKAGE_NAME}.wiki.git wiki

get-gh-pages:
	git clone https://github.com/Juniper/${PACKAGE_NAME}.git \
		gh-pages -b gh-pages

UPDATE_PACKAGE_FILE = -e "s/__SHA1__/$$SHA1/"
GH_PACKAGING_DIR = packaging/${PACKAGE_VERSION}
GH_PAGES_PACKAGE_DIR = ${GH_PAGES_DIR}/${GH_PACKAGING_DIR}

packages:
	@-[ -d ${GH_PAGES_DIR} ] \
		&& echo "Updating packages on gh-pages ..." \
		&& SHA1=`openssl sha1 ${PACKAGE_FILE} | awk '{print $$2}'` \
		&& mkdir -p ${GH_PAGES_PACKAGE_DIR} \
		&& echo "... ${GH_PAGES_PACKAGE_DIR}/${PACKAGE_NAME}.rb ..." \
		&& sed ${UPDATE_PACKAGE_FILE} \
			packaging/${PACKAGE_NAME}.rb.base \
			> ${GH_PAGES_PACKAGE_DIR}/${PACKAGE_NAME}.rb \
		&& cp packaging/${PACKAGE_NAME}.spec \
			${GH_PAGES_PACKAGE_DIR}/${PACKAGE_NAME}.spec \
		&& (cd ${GH_PAGES_DIR} \
			&& git add ${GH_PACKAGING_DIR} \
			&& git commit -m 'new packaging data' \
                               ${GH_PACKAGING_DIR} \
			&& git push origin gh-pages ) ; true
