#
# Copyright 2017, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

if SLAX_WARNINGS_HIGH
SLAX_WARNINGS = HIGH
endif
if HAVE_GCC
GCC_WARNINGS = yes
endif
include ${top_srcdir}/warnings.mk

AM_CFLAGS = \
    -I${top_builddir} \
    -I${top_srcdir} \
    ${WARNINGS}

lib_LTLIBRARIES = libparrotdb.la
libparrotdb_la_LIBADD = $(top_builddir)/libpsu/libpsu.la

parrotdbincdir = ${includedir}/parrotdb

parrotdbinc_HEADERS = \
    paarb.h \
    pabitmap.h \
    pacommon.h \
    paconfig.h \
    pafixed.h \
    paistr.h \
    palog2.h \
    pammap.h \
    papat.h

libparrotdb_la_SOURCES = \
    paarb.c \
    pacommon.c \
    paconfig.c \
    pafixed.c \
    paistr.c \
    pammap.c \
    papat.c

GEN_CODE = ${top_srcdir}/bin/gen-code.sh
GEN_STAMP = gen/.stamp

GENERATED = \
    gen/paarb_gen.h \
    gen/pabitmap_gen.h \
    gen/pafixed_gen.h \
    gen/paistr_data_gen.h \
    gen/paistr_gen.h \
    gen/pammap_gen.h \
    gen/papat_data_gen.h \
    gen/papat_gen.h

BUILT_SOURCES = ${GENERATED}

# Run gen-code.sh once and record it with a stamp file.
# Touching the stamp AFTER the script ensures the generated headers are
# always newer than the stamp, so make does not re-run the script on
# subsequent builds unless gen-code.sh itself has changed.
$(GEN_STAMP): $(GEN_CODE)
	$(MKDIR_P) gen
	sh $(GEN_CODE)
	touch $(GEN_STAMP)

# Individual headers depend on the stamp; the recipe only fires when a
# header is missing (e.g. after a partial clean).
$(GENERATED): $(GEN_STAMP)
	@test -f $@ || $(MAKE) $(AM_MAKEFLAGS) $(GEN_STAMP)

gen_headers: $(GEN_STAMP)

CLEANFILES = $(GEN_STAMP)

CLEANDIRS = gen

clean-local:
	rm -rf ${CLEANDIRS}
