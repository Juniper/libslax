#
# Copyright 2011-2013, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

PERL = perl
OXTRADOC_PREFIX = ${srcdir}
OXTRADOC_BINDIR = oxtradoc
OXTRADOC_DIR = ${OXTRADOC_PREFIX}/oxtradoc
SLAXPROC_BINDIR = ../slaxproc

OXTRADOC = ${OXTRADOC_BINDIR}/oxtradoc
XML2RFC = ${OXTRADOC_DIR}/xml2rfc.tcl
XML2HTMLDIR = ${OXTRADOC_DIR}
XML2HTMLBIN = ${XML2HTMLDIR}/rfc2629-to-html.slax
SLAXPROC = ${SLAX_BINDIR}/slaxproc

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

OX_ARGS = -P ${OXTRADOC_PREFIX} -L ${OXTRADOC_PREFIX}/oxtradoc
OX_ARGS += -S ${SLAXPROC_BINDIR}/slaxproc -p doc
OX_CMD = ${PERL} ${PERLOPTS} ${OXTRADOC_BINDIR}/oxtradoc ${OX_ARGS}
OXTRADOC_CMD = ${OX_CMD}

OUTPUT = slax-manual
INPUT = slax.txt

QUICKREF = slax-quick-reference

EXTRA_DIST = \
    ${INPUT} \
    ${OUTPUT}.html \
    ${OUTPUT}.txt \
    ${QUICKREF}.pdf

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

doc docs: ${OUTPUT}.txt ${OUTPUT}.html html

${OUTPUT}.txt: ${INPUT} ${OXTRADOC}
	${OXTRADOC_CMD} -m text -o $@ $<

${OUTPUT}.html: ${INPUT} ${OXTRADOC} ${XML2HTMLBIN}
	${OXTRADOC_CMD} -m html -o $@ $<

CLEANFILES = \
${INPUT:.txt=.xml} \
${OUTPUT}.txt \
${INPUT:.txt=.fxml} \
${OUTPUT}.html

OXTRADOC_INSTALL_DIR = ${DESTDIR}${prefix}/share/oxtradoc

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
	@${RM} -f ${DESTDIR}${bindir}/oxtradoc
	@${LN_S} ${prefix}/share/oxtradoc/oxtradoc ${DESTDIR}${bindir}/oxtradoc

uninstall-hook:
	@echo "Uninstalling oxtradoc ... "
	@for file in ${OXTRADOC_INSTALL_FILES} ; do \
		${RM} -f ${OXTRADOC_INSTALL_DIR}/`basename $$file` ; \
        done
	${RM} -f ${DESTDIR}${bindir}/oxtradoc

SPHINX = python3.4 -msphinx

html sphinx sphinx-html:
	${SPHINX} -M html ${srcdir} sphinx

clean-local: sphinx-clean

sphinx-clean:
	${SPHINX} -M clean ${srcdir} sphinx
