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
    -I${top_builddir}/parrotdb \
    -I${top_builddir}/libxi \
    -I${top_srcdir} \
    ${WARNINGS}

lib_LTLIBRARIES = libxi.la

libxiincdir = ${includedir}/libxi

libxiinc_HEADERS = \
    xicommon.h \
    xinodeset.h \
    xiparse.h \
    xirules.h \
    xisource.h \
    xitree.h \
    xiwhiffle.h \
    xiworkspace.h \
    xixpath.h

libxi_la_SOURCES = \
    xisource.c \
    xiworkspace.c \
    xitree.c \
    xiparse.c \
    xirules.c

libxi_la_LIBADD = \
    $(top_builddir)/libpsu/libpsu.la \
    $(top_builddir)/parrotdb/libparrotdb.la

XXXX=\
    xiwhiffle.c \
    xixpath.c

GEN_CODE = ${top_srcdir}/bin/gen-code.sh
GEN_STAMP = gen/.stamp

GENERATED = \
    gen/xi_rule_id_gen.h \
    gen/xi_rstate_id_gen.h \
    gen/xi_node_id_gen.h \
    gen/xi_ns_map_id_gen.h \
    gen/xi_rule_id_funcs_gen.h \
    gen/xi_rstate_id_funcs_gen.h \
    gen/xi_node_gen.h \
    gen/xi_ns_map_gen.h \
    gen/xi_nodeset_chunk_gen.h \
    gen/xi_nodeset_info_gen.h

BUILT_SOURCES = ${GENERATED}

$(GEN_STAMP): $(GEN_CODE)
	$(MKDIR_P) gen
	sh $(GEN_CODE) xi
	touch $(GEN_STAMP)

$(GENERATED): $(GEN_STAMP)
	@test -f $@ || $(MAKE) $(AM_MAKEFLAGS) $(GEN_STAMP)

gen_headers: $(GEN_STAMP)

CLEANFILES = $(GEN_STAMP)

CLEANDIRS = gen

clean-local:
	rm -rf ${CLEANDIRS}
