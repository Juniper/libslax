#
# Copyright 2013, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.
#

ART_FILES = ${top_srcdir}/tests/art/test*txt
ASCIIART = ${top_srcdir}/doc/oxtradoc/asciiart

test tests:
	-@echo "... (skipping art) ...";

test-art:
	@-mkdir -p art
	@echo "Testing art files ..."
	@echo '' > art/art.html
	@echo '' > art/art-svg.html
	@for file in ${ART_FILES} ; do \
		echo "... $$file ..."; \
		base=`basename $$file .txt`; \
		( cd art; perl ../${ASCIIART} ../$$file ); \
		( cd art; perl ../${ASCIIART} -S ../$$file ); \
		( echo "<h3>$$base.txt</h3><pre style='font-family: monospace'>"; \
		  sed 's/</\&lt;/g' $$file ; echo '</pre>'; \
		  echo "<img src='$$base.png'/><hr/>"; ) >> art/art.html ; \
		( echo "<h3>$$base.txt</h3><pre style='font-family: monospace'>"; \
		  sed 's/</\&lt;/g' $$file ; echo '</pre>'; \
		  sed '1,2d' art/$$base.svg ; \
		  echo "<hr/>"; ) >> art/art-svg.html ; \
	done
