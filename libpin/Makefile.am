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
    -I${top_builddir}/libpin \
    -I${top_srcdir} \
    -I${top_builddir}/libxo \
    -I${top_builddir}/libxo/filter \
    ${WARNINGS}

lib_LTLIBRARIES = libpin.la

libpinincdir = ${includedir}/libpin

libpininc_HEADERS = \
    pin_body.h \
    pin_common.h \
    pin_exec.h \
    pin_filter.h \
    pin_nodeset.h \
    pin_parse.h \
    pin_rules.h \
    pin_slax.h \
    pin_source.h \
    pin_tree.h \
    pin_value.h \
    pin_whiffle.h \
    pin_workspace.h \
    pin_xpath.h

libpin_la_SOURCES = \
    pin_exec.c \
    pin_filter.c \
    pin_slax.c \
    pin_source.c \
    pin_workspace.c \
    pin_tree.c \
    pin_parse.c \
    pin_rules.c

libpin_la_LIBADD = \
    $(top_builddir)/libpsu/libpsu.la \
    $(top_builddir)/parrotdb/libparrotdb.la \
    $(libdir)/libxo.la

XXXX=\
    pinwhiffle.c \
    pinxpath.c

GEN_CODE = ${top_srcdir}/bin/gen-code.sh
GEN_STAMP = gen/.stamp

GENERATED = \
    gen/pin_rule_id_gen.h \
    gen/pin_rstate_id_gen.h \
    gen/pin_node_id_gen.h \
    gen/pin_ns_map_id_gen.h \
    gen/pin_body_instr_id_gen.h \
    gen/pin_apply_id_gen.h \
    gen/pin_op_id_gen.h \
    gen/pin_rule_id_funcs_gen.h \
    gen/pin_rstate_id_funcs_gen.h \
    gen/pin_node_gen.h \
    gen/pin_ns_map_gen.h \
    gen/pin_nodeset_chunk_gen.h \
    gen/pin_nodeset_info_gen.h \
    gen/pin_body_instr_id_funcs_gen.h \
    gen/pin_apply_id_funcs_gen.h \
    gen/pin_op_id_funcs_gen.h

BUILT_SOURCES = ${GENERATED}

$(GEN_STAMP): $(GEN_CODE)
	$(MKDIR_P) gen
	sh $(GEN_CODE) pin
	touch $(GEN_STAMP)

$(GENERATED): $(GEN_STAMP)
	@test -f $@ || $(MAKE) $(AM_MAKEFLAGS) $(GEN_STAMP)

gen_headers: $(GEN_STAMP)

CLEANFILES = $(GEN_STAMP)

CLEANDIRS = gen

clean-local:
	rm -rf ${CLEANDIRS}
