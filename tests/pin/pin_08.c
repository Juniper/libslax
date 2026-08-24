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
 * pin_08.c -- test for-each streaming dispatch.
 *
 * Compiles an XSLT stylesheet containing <xsl:for-each select="..."> inside
 * a template body.  Templates with a for-each body are compiled as SAVE with
 * a state transition that, within the matched element's children, saves only
 * the selected elements and discards the rest.
 *
 * Command-line args (one set per '#' line in each .in file):
 *   input <xslt_file>    XSLT stylesheet (passed automatically by run-tests.sh)
 *   xml <xml_file>       XML document to parse
 *   modes                enumerate modes in the stylesheet, then exit
 *   action save|discard  action for non-foreach matched templates (default: discard)
 *   dump                 emit parsed tree as XML to stdout after parsing
 *   quiet                suppress debug logging
 *   clean                delete the mmap backing file before opening
 *   debug                enable PIN_PF_DEBUG on the parser
 *   db <file>            override mmap backing file (default /tmp/pin08.sxb)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <err.h>
#include <sys/types.h>
#include <assert.h>
#include <limits.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

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
#include <libpin/pin_slax.h>

#define PIN08_DB_FILE	"/tmp/pin08.sxb"

static const char *
check_arg (const char *arg, const char *name)
{
    if (arg == NULL)
	errx(1, "missing argument for '%s'", name);
    return arg;
}

typedef struct {
    int total;
} pin08_modes_ctx_t;

static void
pin08_mode_cb (void *opaque, const char *mode, int count)
{
    pin08_modes_ctx_t *ctx = opaque;
    ctx->total++;
    printf("mode: %s (%d pattern%s)\n",
	   mode ?: "(default)", count, count == 1 ? "" : "s");
}

int
main (int argc, char **argv)
{
    const char *opt_xslt = NULL;
    const char *opt_xml = NULL;
    const char *opt_db = PIN08_DB_FILE;
    pin_action_type_t opt_action = PIA_DISCARD;
    int opt_quiet = 0;
    int opt_dump = 0;
    int opt_clean = 0;
    int opt_debug = 0;
    int opt_modes = 0;

    argc = xo_parse_args(argc, argv);
    if (argc < 0)
	xo_err(1, "pin_08: argument issue");

    for (int i = 1; argv[i]; i++) {
	const char *cp = argv[i];

	if (strcmp(cp, "input") == 0 || strcmp(cp, "file") == 0) {
	    opt_xslt = check_arg(argv[++i], "xslt filename");
	} else if (strcmp(cp, "xml") == 0) {
	    opt_xml = check_arg(argv[++i], "xml filename");
	} else if (strcmp(cp, "modes") == 0) {
	    opt_modes = 1;
	} else if (strcmp(cp, "action") == 0) {
	    const char *a = check_arg(argv[++i], "action");
	    opt_action = (strcmp(a, "save") == 0) ? PIA_SAVE : PIA_DISCARD;
	} else if (strcmp(cp, "dump") == 0) {
	    opt_dump = 1;
	} else if (strcmp(cp, "quiet") == 0) {
	    opt_quiet = 1;
	} else if (strcmp(cp, "clean") == 0) {
	    opt_clean = 1;
	} else if (strcmp(cp, "debug") == 0) {
	    opt_debug = 1;
	} else if (strcmp(cp, "db") == 0) {
	    opt_db = check_arg(argv[++i], "db filename");
	}
    }

    if (opt_xslt == NULL)
	errx(1, "missing required 'input' argument");

    if (!opt_quiet)
	psu_log_enable(1);

    if (opt_clean)
	unlink(opt_db);

    xmlDocPtr docp = xmlReadFile(opt_xslt, NULL, 0);
    if (docp == NULL)
	errx(1, "xmlReadFile failed for '%s'", opt_xslt);

    if (opt_modes) {
	pin08_modes_ctx_t ctx = { 0 };
	int n = pin_slax_for_each_mode(docp, pin08_mode_cb, &ctx);
	printf("total: %d mode%s\n", n, n == 1 ? "" : "s");
	xmlFreeDoc(docp);
	return 0;
    }

    if (opt_xml == NULL)
	errx(1, "missing required 'xml' argument");

    /* Resolve xml path relative to the XSLT file's directory if not absolute */
    char xml_path[PATH_MAX];
    if (opt_xml[0] != '/') {
	const char *slash = strrchr(opt_xslt, '/');
	if (slash) {
	    size_t dirlen = slash - opt_xslt + 1;
	    snprintf(xml_path, sizeof(xml_path), "%.*s%s",
		     (int) dirlen, opt_xslt, opt_xml);
	    opt_xml = xml_path;
	}
    }

    pa_mmap_t *pmp = pa_mmap_open(opt_db, "pin08", 0, 0644);
    assert(pmp);

    pin_workspace_t *workp = pin_workspace_open(pmp, "pin08");
    assert(workp);

    xo_filter_t *xfp = pin_filter_create(NULL, workp);
    pin_rulebook_t *rb = pin_rulebook_setup(workp, NULL, "pin08");

    int count = pin_slax_compile(docp, xfp, rb, opt_action);
    xmlFreeDoc(docp);

    if (count < 0)
	errx(1, "pin_slax_compile failed");

    printf("compiled %d pattern%s\n", count, count == 1 ? "" : "s");

    pin_parse_t *parsep = pin_parse_open(pmp, workp, "pin08", opt_xml, 0);
    assert(parsep);

    if (opt_debug)
	pin_parse_flags_set(parsep, PIN_PF_DEBUG);

    pin_parse_set_default_rule(parsep, PIA_SAVE);
    pin_parse_set_filter(parsep, xfp);
    pin_parse_set_rulebook(parsep, rb);

    pin_parse(parsep);

    if (opt_dump)
	pin_parse_emit_xml(parsep, stdout);

    pin_workspace_close(workp);
    pa_mmap_close(pmp);
    pin_parse_destroy(parsep);

    return 0;
}
