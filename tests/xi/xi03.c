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

int
main (int argc, char **argv)
{
    const char *opt_filename = NULL;
    const char *opt_script = "script.xml";
    const char *opt_rulebook = "rulebook.sxb";
    const char *opt_database = "test.sxb";
    const char *opt_config = NULL;
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
	} else if (strcmp(argv[argc], "rulebook") == 0) {
	    if (argv[argc + 1])
		opt_rulebook = argv[++argc];
	} else if (strcmp(argv[argc], "database") == 0) {
	    if (argv[argc + 1])
		opt_database = argv[++argc];
	} else if (strcmp(argv[argc], "script") == 0) {
	    if (argv[argc + 1])
		opt_script = argv[++argc];
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

    if (opt_clean) {
	unlink(opt_rulebook);
	unlink(opt_database);
    }

    assert (opt_database != NULL && opt_filename != NULL);

    if (opt_config)
	pa_config_read(opt_config);

    pa_mmap_t *pmp = pa_mmap_open(opt_database, 0, 0644);
    assert(pmp);

    xi_workspace_t *workp = xi_workspace_open(pmp, "test");
    assert(workp);

    xi_parse_t *script = xi_parse_open(pmp, workp, "script",
				       opt_script, flags);
    assert(script);

    /* We need to save all attributes */
    xi_parse_set_default_rule(script, XIA_SAVE_ATTRIB);

    xi_parse(script);

    if (opt_dump) {
	if (!opt_quiet)
	    slaxLogEnable(1);
	xi_parse_dump(script);
	xi_parse_emit_xml(script, stdout);
    }

    /* We prep the rulebook to build our states and rules */
    xi_rulebook_t *rb = xi_rulebook_prep(script, "rulebook");
    assert(rb);

    xi_rulebook_dump(rb);

    xi_parse_t *parsep = xi_parse_open(pmp, workp, "test",
				       opt_filename, flags);
    assert(parsep);

    xi_parse_set_rulebook(parsep, rb);

    xi_parse(parsep);

    if (opt_dump) {
	if (!opt_quiet)
	    slaxLogEnable(1);
	xi_parse_dump(parsep);
	xi_parse_emit_xml(parsep, stdout);
    }

    xi_parse_destroy(parsep);

    return 0;
}
