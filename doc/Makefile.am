#
# $Id$
#
# Copyright 2011, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

PERL = perl
OXDIR = oxtradoc
OXTRADOC_PREFIX = ${srcdir}
OXSRCDIR = ${OXTRADOC_PREFIX}/oxtradoc
OXTRADOC = ${OXDIR}/oxtradoc
XML2RFC = ${OXSRCDIR}/xml2rfc.tcl
XML2HTMLDIR = `dirname ${XML2HTMLBIN}`
OXTRADOC_DIR = ${OXTRADOC_PREFIX}/oxtradoc
XML2HTMLBIN = ${OXTRADOC_DIR}/rfc2629-to-html.slax
SLAXPROC = ../slaxproc/slaxproc ${SLAXOPTS}

OX_ARGS = -P ${OXTRADOC_PREFIX} -S ../slaxproc/slaxproc
OX_CMD = ${PERL} ${PERLOPTS} ${OXDIR}/oxtradoc ${OX_ARGS}
OXTRADOC_CMD = ${OX_CMD}

SLAXPROC_ARGS = \
    -a oxtradoc-dir ${OXTRADOC_DIR} \
    -a oxtradoc-install-dir ${OXTRADOC_DIR} \
    -a anchor-prefix docs

SLAXPROC_ARGS_INLINE = \
    -a oxtradoc-inline yes

SLAXPROC_ARGS += ${SLAXPROC_ARGS_INLINE}

XML2HTML = \
    ${SLAXPROC} -g -e -I ${OXTRADOC_DIR} -I . \
    ${SLAXPROC_ARGS} \
    ${XML2HTMLBIN}

OUTPUT = slax-manual-${PACKAGE_VERSION}
INPUT = slax.txt

# We cheat and add the PDF for the quick reference card to
# SVN since printing docx requires ms-word.  We want the
# package version number in the file that we put on the
# web site, so we just copy it.
QUICKREF = slax-quick-reference
QUICKREFVERS = ${QUICKREF}-${PACKAGE_VERSION}

EXTRA_DIST = \
    ${OUTPUT}.html \
    ${OUTPUT}.txt \
    ${QUICKREF}.pdf \
    ${QUICKREFVERS}.pdf

OXTRADOC_COMMON_FILES = \
    oxtradoc/xml2rfc.tcl \
    oxtradoc/jquery-ui.js \
    oxtradoc/jquery.dbgpr.js \
    oxtradoc/jquery.js \
    oxtradoc/rfc2629-to-html.slax \
    oxtradoc/rfc2629-other.ent \
    oxtradoc/rfc2629-xhtml.ent \
    oxtradoc/rfc2629.dtd \
    oxtradoc/oxtradoc-nav.js \
    oxtradoc/oxtradoc.css \
    oxtradoc/oxtradoc-tips.js

OXTRADOC_INSTALL_FILES = \
    ${OXTRADOC_COMMON_FILES} \
    oxtradoc/oxtradoc

OXTRADOC_SRC_FILES = \
    ${OXTRADOC_COMMON_FILES} \
    oxtradoc/oxtradoc.in

EXTRA_DIST += ${OXTRADOC_SRC_FILES}

doc docs: ${OUTPUT}.txt ${OUTPUT}.html ${QUICKREFVERS}.pdf

${OUTPUT}.txt: ${INPUT} ${OXTRADOC}
	${OXTRADOC_CMD} -m text -o $@ $<

${OUTPUT}.html: ${INPUT} ${OXTRADOC} ${XML2HTMLBIN}
	${OXTRADOC_CMD} -m html -o $@ $<

${QUICKREFVERS}.pdf: ${QUICKREF}.pdf
	cp $< $@

CLEANFILES = \
${INPUT:.txt=.xml} \
${OUTPUT}.txt \
${INPUT:.txt=.fxml} \
${OUTPUT}.html \
${QUICKREFVERS}.pdf

OXTRADOC_INSTALL_DIR = ${prefix}/share/oxtradoc

install-data-hook:
	@echo "Installing oxtradoc ... "
	@-mkdir -p ${OXTRADOC_INSTALL_DIR}
	@for file in ${OXTRADOC_INSTALL_FILES} ; do \
		if [ -f $$file ]; then \
			rfile=$$file ; \
		else \
			rfile=${srcdir}/$$file ; \
		fi ; \
		mdir=${OXTRADOC_INSTALL_DIR}/ ; \
		mkdir -p $$mdir ; \
		cp $$rfile $$mdir/ ; \
        done
	@${CHMOD} a+x ${OXTRADOC_INSTALL_DIR}/oxtradoc
