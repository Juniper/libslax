#
# $Id$
#
# Copyright 2011, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

BINDIR = ~/work/yang/bin
OUTLINE2XML = ${BINDIR}/outline2xml
XML2RFC = ${BINDIR}/xml2rfc.tcl
XML2HTMLBIN = ${BINDIR}/rfc2629-yang.xslt
XML2HTMLDIR = `dirname ${XML2HTMLBIN}`
XML2HTML = xsltproc --path ${XML2HTMLDIR} ${XML2HTMLBIN}

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

doc docs: ${OUTPUT}.txt ${OUTPUT}.html ${QUICKREF}.pdf

${OUTPUT}.txt: ${OUTPUT}.xml
	${XML2RFC} $< > $@

${OUTPUT}.xml: ${INPUT} ${OUTLINE2XML}
	${OUTLINE2XML} $< > $@

${OUTPUT}.html: ${OUTPUT}.fxml ${XML2HTMLBIN}
	${XML2HTML} $< > $@

${OUTPUT}.fxml: ${INPUT} ${OUTLINE2XML}
	${OUTLINE2XML} -D $< > $@

${QUICKREF}.pdf: slax-quick-reference.pdf
	cp $< $@

CLEANFILES = \
${OUTPUT}.xml \
${OUTPUT}.txt \
${OUTPUT}.fxml \
${OUTPUT}.html \
${QUICKREF}.pdf
