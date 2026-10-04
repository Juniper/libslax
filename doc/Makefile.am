#
# Copyright 2011-2013, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

doc docs: html

SPHINX = python3 -msphinx

html sphinx sphinx-html:
	${GREP} '.. admonition: ' ${srcdir}/*rst; true
	${SPHINX} -M html ${srcdir} sphinx

clean-local: sphinx-clean

sphinx-clean:
	-${SPHINX} -M clean ${srcdir} sphinx
