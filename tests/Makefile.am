#
# Copyright 2011-2025, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

SUBDIRS= \
    base \
    core \
    bugs \
    errors \
    art \
    pa \
    xi

if USE_LIBXSLT_TESTS
SUBDIRS += libxslt
endif

test tests:
	@(cur=`pwd` ; for dir in $(SUBDIRS) ; do \
		cd $$dir ; \
		$(MAKE) tests ; \
		cd $$cur ; \
	done)

accept:
	@(cur=`pwd` ; for dir in $(SUBDIRS) ; do \
		cd $$dir ; \
		$(MAKE) accept ; \
		cd $$cur ; \
	done)

valgrind:
	@echo '## Running the regression tests under Valgrind'
	@echo '## Go get a cup of coffee it is gonna take a while ...'
	${MAKE} CHECKER="valgrind -q" tests

SCRIBBLE=DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib MallocPreScribble=1

scribble:
	@echo '## Running the regression tests under scribble'
	@echo '## Go get a cup of coffee it is gonna take a while ...'
	${MAKE} CHECKER="${SCRIBBLE}" tests
