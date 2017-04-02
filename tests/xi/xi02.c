/*
 * $Id$
 *
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer (phil@) June 2016
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <err.h>
#include <sys/types.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>

#include "slaxconfig.h"
#include <libslax/slax.h>
#include <libslax/pa_common.h>
#include <libslax/pa_config.h>
#include <libslax/pa_mmap.h>
#include <libslax/pa_fixed.h>
#include <libslax/pa_arb.h>
#include <libslax/pa_istr.h>
#include <libslax/pa_pat.h>
#include <libslax/pa_bitmap.h>
#include <libslax/xi_common.h>
#include <libslax/xi_source.h>
#include <libslax/xi_rules.h>
#include <libslax/xi_tree.h>
#include <libslax/xi_workspace.h>
#include <libslax/xi_parse.h>
#include <libslax/xi_nodeset.h>

typedef struct test_data_s {
    xi_workspace_t *td_workp;
    xi_nodeset_t *td_nsp;
} test_data_t;

static int
test_node_set (xi_parse_t *parsep UNUSED, xi_node_type_t type,
	       pa_atom_t node_atom, xi_node_t *nodep UNUSED,
	       const char *data UNUSED, void *opaque)
{
    test_data_t *tdp = opaque;

    if (type == XI_TYPE_OPEN) {
	xi_nodeset_add(tdp->td_nsp, node_atom);
	xi_nodeset_dump(tdp->td_nsp);
    }

    return 0;
}

int
main (int argc, char **argv)
{
    const char *opt_filename = NULL;
    const char *opt_database = "test.db";
    const char *opt_config = NULL;
    int opt_read = 0;
    int opt_quiet = 0;
    int opt_dump = 0;
    int opt_unescape = 0;
    int opt_clean = 0;
    xi_source_flags_t flags = 0;

    for (argc = 1; argv[argc]; argc++) {
	if (strcmp(argv[argc], "file") == 0
	    || strcmp(argv[argc], "input") == 0) {
	    if (argv[argc + 1])
		opt_filename = argv[++argc];
	} else if (strcmp(argv[argc], "config") == 0) {
	    if (argv[argc + 1])
		opt_config = argv[++argc];
	} else if (strcmp(argv[argc], "database") == 0) {
	    if (argv[argc + 1])
		opt_database = argv[++argc];
	} else if (strcmp(argv[argc], "read") == 0) {
	    opt_read = 1;
	} else if (strcmp(argv[argc], "dump") == 0) {
	    opt_dump = 1;
	} else if (strcmp(argv[argc], "quiet") == 0) {
	    opt_quiet = 1;
	} else if (strcmp(argv[argc], "clean") == 0) {
	    opt_clean = 1;
	} else if (strcmp(argv[argc], "unescape") == 0) {
	    opt_unescape = 1;
	} else if (strcmp(argv[argc], "line") == 0) {
	    flags |= XPSF_LINE_NO;
	} else if (strcmp(argv[argc], "trim") == 0) {
	    flags |= XPSF_TRIM_WS;
	} else if (strcmp(argv[argc], "ignore") == 0) {
	    flags |= XPSF_IGNORE_WS;
	} else if (strcmp(argv[argc], "ignore-comments") == 0) {
	    flags |= XPSF_IGNORE_COMMENTS;
	} else if (strcmp(argv[argc], "ignore-dtd") == 0) {
	    flags |= XPSF_IGNORE_DTD;
	}
    }

    if (!opt_quiet)
	slaxLogEnable(1);

    if (opt_clean)
	unlink(opt_database);

    assert (opt_database != NULL && opt_filename != NULL);

    if (opt_config)
	pa_config_read(opt_config);

    pa_mmap_t *pmp = pa_mmap_open(opt_database, 0, 0644);
    assert(pmp);

    xi_workspace_t *workp = xi_workspace_open(pmp, "test");
    assert(workp);

    xi_parse_t *parsep = xi_parse_open(pmp, workp, "test",
				       opt_filename, flags);
    assert(parsep);

    xi_parse(parsep);

    if (opt_dump) {
	if (!opt_quiet)
	    slaxLogEnable(1);
	xi_parse_dump(parsep);
	xi_parse_emit_xml(parsep, stdout);
    }

    /* Test nodesets */
    xi_nodeset_t *nsp = xi_nodeset_alloc(workp, XI_NSTYPE_NORMAL, 0);
    if (nsp) {
	test_data_t test_data = { workp, nsp };
	xi_parse_emit(parsep, test_node_set, &test_data);
	xi_nodeset_dump(nsp);
	xi_nodeset_free(nsp);
    }

    xi_parse_destroy(parsep);

    return 0;
}
