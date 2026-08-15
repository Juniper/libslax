/*
 * Copyright (c) 2016-2026, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer, August 2026
 *
 * pin_05.c -- test pin_filter_create and trie action dispatch.
 *
 * Exercises the filter integration in pin_parse:
 *   Phase 2: xfdo_value_of (attribute lookup for predicate evaluation)
 *   Phase 3: pin_filter_add_with_action + three-way DEAD/FULL/TRACK dispatch
 *
 * Command-line args (one set per '#' line in each .in file):
 *   input <file>     XML file to parse
 *   filter <xpath>   add XPath pattern; default rule applies when FULL
 *   save   <xpath>   add XPath pattern; matched elements get SAVE action
 *   discard <xpath>  add XPath pattern; matched elements get DISCARD action
 *   dump             emit the parsed tree as XML to stdout after parsing
 *   quiet            suppress debug logging
 *   clean            delete the mmap backing file before opening
 *   debug            enable PIN_PF_DEBUG on the parser
 *   db <file>        override the mmap backing file (default /tmp/pin05.sxb)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <err.h>
#include <sys/types.h>
#include <assert.h>

#include <libpsu/psulog.h>
#include <parrotdb/pacommon.h>
#include <parrotdb/paconfig.h>
#include <parrotdb/pammap.h>
#include <libpin/pin_common.h>
#include <libpin/pin_workspace.h>
#include <libpin/pin_rules.h>
#include <libpin/pin_tree.h>
#include <libpin/pin_parse.h>
#include <libpin/pin_filter.h>

#define PIN05_DB_FILE	"/tmp/pin05.sxb"
#define PIN05_MAX_PATS	32

static const char *
check_arg (const char *arg, const char *name)
{
    if (arg == NULL)
	errx(1, "missing argument for '%s'", name);
    return arg;
}

typedef enum { PAT_FILTER, PAT_SAVE, PAT_DISCARD } pin05_pat_type_t;

typedef struct {
    pin05_pat_type_t type;
    const char *xpath;
} pin05_pat_t;

static pin05_pat_t pats[PIN05_MAX_PATS];
static int n_pats;

static void
add_pat (pin05_pat_type_t type, const char *xpath)
{
    if (n_pats < PIN05_MAX_PATS) {
	pats[n_pats].type = type;
	pats[n_pats].xpath = xpath;
	n_pats++;
    }
}

int
main (int argc, char **argv)
{
    const char *opt_filename = NULL;
    const char *opt_db = PIN05_DB_FILE;
    int opt_quiet = 0;
    int opt_dump = 0;
    int opt_clean = 0;
    int opt_debug = 0;
    pin_source_flags_t flags = 0;

    for (argc = 1; argv[argc]; argc++) {
	const char *cp = argv[argc];

	if (strcmp(cp, "input") == 0 || strcmp(cp, "file") == 0) {
	    opt_filename = check_arg(argv[++argc], "filename");
	} else if (strcmp(cp, "filter") == 0) {
	    add_pat(PAT_FILTER, check_arg(argv[++argc], "filter xpath"));
	} else if (strcmp(cp, "save") == 0) {
	    add_pat(PAT_SAVE, check_arg(argv[++argc], "save xpath"));
	} else if (strcmp(cp, "discard") == 0) {
	    add_pat(PAT_DISCARD, check_arg(argv[++argc], "discard xpath"));
	} else if (strcmp(cp, "dump") == 0) {
	    opt_dump = 1;
	} else if (strcmp(cp, "quiet") == 0) {
	    opt_quiet = 1;
	} else if (strcmp(cp, "clean") == 0) {
	    opt_clean = 1;
	} else if (strcmp(cp, "debug") == 0) {
	    opt_debug = 1;
	} else if (strcmp(cp, "db") == 0) {
	    opt_db = check_arg(argv[++argc], "db filename");
	}
    }

    if (!opt_quiet)
	psu_log_enable(1);

    if (opt_clean)
	unlink(opt_db);

    assert(opt_filename != NULL);

    pa_mmap_t *pmp = pa_mmap_open(opt_db, "pin05", 0, 0644);
    assert(pmp);

    pin_workspace_t *workp = pin_workspace_open(pmp, "pin05");
    assert(workp);

    /* Build filter if any XPath patterns were given */
    xo_filter_t *xfp = NULL;
    pin_rulebook_t *rb = NULL;
    pin_rule_id_t save_rid = pin_rule_id_null_atom();
    pin_rule_id_t discard_rid = pin_rule_id_null_atom();

    if (n_pats > 0) {
	xfp = pin_filter_create(NULL, workp);
	assert(xfp);

	/* Allocate rulebook only if save/discard patterns require action ids */
	int need_rb = 0;
	for (int i = 0; i < n_pats; i++)
	    if (pats[i].type != PAT_FILTER) { need_rb = 1; break; }

	if (need_rb) {
	    rb = pin_rulebook_setup(workp, NULL, "pin05");
	    assert(rb);

	    pin_rule_t *sp = pin_rule_alloc(rb, &save_rid);
	    sp->pr_action = PIA_SAVE;

	    pin_rule_t *dp = pin_rule_alloc(rb, &discard_rid);
	    dp->pr_action = PIA_DISCARD;
	}

	for (int i = 0; i < n_pats; i++) {
	    if (pats[i].type == PAT_FILTER)
		pin_filter_add(xfp, pats[i].xpath);
	    else if (pats[i].type == PAT_SAVE)
		pin_filter_add_with_action(xfp, pats[i].xpath, save_rid);
	    else
		pin_filter_add_with_action(xfp, pats[i].xpath, discard_rid);
	}
    }

    pin_parse_t *parsep = pin_parse_open(pmp, workp, "pin05", opt_filename, flags);
    assert(parsep);

    if (opt_debug)
	pin_parse_flags_set(parsep, PIN_PF_DEBUG);

    pin_parse_set_default_rule(parsep, PIA_SAVE);

    if (xfp)
	pin_parse_set_filter(parsep, xfp);
    if (rb)
	pin_parse_set_rulebook(parsep, rb);

    pin_parse(parsep);

    if (opt_dump)
	pin_parse_emit_xml(parsep, stdout);

    pin_workspace_close(workp);
    pa_mmap_close(pmp);
    pin_parse_destroy(parsep);

    return 0;
}
