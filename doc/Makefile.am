#
# $Id$
#
# Copyright 2011, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

BINDIR = ${srcdir}/oxtradoc
OUTLINE2XML = ${BINDIR}/outline2xml
XML2RFC = ${BINDIR}/xml2rfc.tcl
XML2HTMLDIR = `dirname ${XML2HTMLBIN}`
OXTRADOC_DIR = ${srcdir}/oxtradoc
XML2HTMLBIN = ${OXTRADOC_DIR}/rfc2629-to-html.slax
SLAXPROC = ../slaxproc/slaxproc ${SLAXOPTS}

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

OXTRADOC_FILES = \
    oxtradoc/outline2xml \
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

EXTRA_DIST += ${OXTRADOC_FILES}

doc docs: ${OUTPUT}.txt ${OUTPUT}.html ${QUICKREFVERS}.pdf

${OUTPUT}.txt: ${OUTPUT}.xml
	${XML2RFC} $< > $@

${OUTPUT}.xml: ${INPUT} ${OUTLINE2XML}
	${OUTLINE2XML} $< > $@

${OUTPUT}.html: ${OUTPUT}.fxml ${XML2HTMLBIN}
	${XML2HTML} $< > $@

${OUTPUT}.fxml: ${INPUT} ${OUTLINE2XML}
	${OUTLINE2XML} -D $< > $@

${QUICKREFVERS}.pdf: ${QUICKREF}.pdf
	cp $< $@

CLEANFILES = \
${OUTPUT}.xml \
${OUTPUT}.txt \
${OUTPUT}.fxml \
${OUTPUT}.html \
${QUICKREFVERS}.pdf
