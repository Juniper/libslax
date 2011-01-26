#
# $Id$
#

BINDIR = ~/work/yang/bin
OUTLINE2XML = ${BINDIR}/outline2xml
XML2RFC = ${BINDIR}/xml2rfc.tcl
XML2HTMLBIN = ${BINDIR}/rfc2629-yang.xslt
XML2HTMLDIR = `dirname ${XML2HTMLBIN}`
XML2HTML = xsltproc --path ${XML2HTMLDIR} ${XML2HTMLBIN}

OUTPUT = slax-manual-${PACKAGE_VERSION}
INPUT = slax.txt

EXTRA_DIST = \
    ${OUTPUT}.html \
    ${OUTPUT}.txt

doc docs: ${OUTPUT}.txt ${OUTPUT}.html

${OUTPUT}.txt: ${OUTPUT}.xml
	${XML2RFC} $< > $@

${OUTPUT}.xml: ${INPUT} ${OUTLINE2XML}
	${OUTLINE2XML} $< > $@

${OUTPUT}.html: ${OUTPUT}.fxml ${XML2HTMLBIN}
	${XML2HTML} $< > $@

${OUTPUT}.fxml: ${INPUT} ${OUTLINE2XML}
	${OUTLINE2XML} -D $< > $@

CLEANFILES = \
${OUTPUT}.xml \
${OUTPUT}.txt \
${OUTPUT}.fxml \
${OUTPUT}.html