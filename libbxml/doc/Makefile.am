## Process this file with automake to produce Makefile.in

EXTRA_DIST = \
	Doxyfile \
	libbxml.css \
	mainpage.md \
	meson.build \
	bxml-config.1 \
	xmlcatalog.xml \
	xmllint.xml

all-local: html.stamp

html.stamp: Doxyfile
	echo ... doxygen for libbxml ...
	@SOURCE_ROOT=$(top_srcdir)/libbxml/ BUILD_ROOT=$(top_builddir)/libbxml/ \
	    $(DOXYGEN) -q $<
	@touch $@

CLEANFILES = html.stamp

clean-local:
	rm -rf html xml

if WITH_DOCS

man_MANS = bxml-config.1 xmllint.1
doc_DATA = xmllint.html
CLEANFILES += xmllint.1 xmllint.html

if WITH_CATALOG_SOURCES
if WITH_OUTPUT_SOURCES
man_MANS += xmlcatalog.1
doc_DATA += xmlcatalog.html
CLEANFILES += xmlcatalog.1 xmlcatalog.html
endif
endif

DOCBOOK_MAN  = http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl
DOCBOOK_HTML = http://docbook.sourceforge.net/release/xsl/current/html/docbook.xsl

.xml.1:
	$(XSLTPROC) --nonet --novalid --param man.output.quietly 1 \
	    -o $@ $(DOCBOOK_MAN) $<

.xml.html:
	$(XSLTPROC) --nonet --novalid -o $@ $(DOCBOOK_HTML) $<

install-data-local:
	$(MKDIR_P) "$(DESTDIR)$(docdir)/html/search"
	$(INSTALL_DATA) html/*.* "$(DESTDIR)$(docdir)/html"
	$(INSTALL_DATA) html/search/*.* "$(DESTDIR)$(docdir)/html/search"

uninstall-local:
	-rm -rf "$(DESTDIR)$(docdir)/html"

endif
