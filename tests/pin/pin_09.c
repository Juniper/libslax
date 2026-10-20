/*
 * Copyright (c) 2016-2026, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer, October 2026
 *
 * pin_09.c -- like pin_08.c, but with slaxEnable(1) called before
 * compiling, so that libslax's pin_dyn_ops_t provider is registered
 * with libpin (pin_set_dyn_ops()).  This lets xfdo_find_function calls
 * for "slax:"/"bit:"-prefixed functions actually resolve, exercising
 * the eager (slax:) and dynamically-loaded (bit:) extension paths
 * through libpin's compile pipeline, not just through libslax's own
 * XSLT-based tests.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <err.h>
#include <sys/types.h>
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
#include <libpin/pin_compile.h>

#include <libslax/slax.h>

#define PIN09_DB_FILE	"/tmp/pin09.sxb"

static const char *
check_arg (const char *arg, const char *name)
{
    if (arg == NULL)
	errx(1, "missing argument for '%s'", name);
    return arg;
}

int
main (int argc, char **argv)
{
    const char *opt_xslt = NULL;
    const char *opt_xml = NULL;
    const char *opt_db = PIN09_DB_FILE;
    pin_action_type_t opt_action = PIA_DISCARD;
    int opt_quiet = 0;
    int opt_dump = 0;
    int opt_clean = 0;
    int opt_debug = 0;
    int opt_passthru = 1;

    argc = xo_parse_args(argc, argv);
    if (argc < 0)
	xo_err(1, "pin_09: argument issue");

    for (int i = 1; argv[i]; i++) {
	const char *cp = argv[i];

	if (strcmp(cp, "input") == 0 || strcmp(cp, "file") == 0) {
	    opt_xslt = check_arg(argv[++i], "xslt filename");
	} else if (strcmp(cp, "xml") == 0) {
	    opt_xml = check_arg(argv[++i], "xml filename");
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
	} else if (strcmp(cp, "passthru") == 0) {
	    opt_passthru = 1;
	} else if (strcmp(cp, "nopassthru") == 0) {
	    opt_passthru = 0;
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

    if (opt_xml == NULL)
	errx(1, "missing required 'xml' argument");

    /*
     * Resolve a bare xml filename (no slashes) relative to the XSLT
     * file's directory, same as pin_08.c.
     */
    char xml_path[PATH_MAX];
    if (strchr(opt_xml, '/') == NULL) {
	const char *slash = strrchr(opt_xslt, '/');
	if (slash) {
	    size_t dirlen = slash - opt_xslt + 1;
	    snprintf(xml_path, sizeof(xml_path), "%.*s%s",
		     (int) dirlen, opt_xslt, opt_xml);
	    opt_xml = xml_path;
	}
    }

    slaxEnable(1);

    pa_mmap_t *pmp = pa_mmap_open(opt_db, "pin09", 0, 0644);
    if (pmp == NULL)
	errx(1, "%s: pa_mmap_open failed", opt_db);

    pin_workspace_t *workp = pin_workspace_open(pmp, "pin09");
    if (workp == NULL)
	errx(1, "%s: pin_workspace_open failed", opt_db);

    xo_filter_t *xfp = pin_filter_create(NULL, workp);
    pin_rulebook_t *rb = pin_rulebook_setup(workp, NULL, "pin09");

    int count = pin_compile(docp, xfp, rb, opt_action, 0);
    xmlFreeDoc(docp);

    if (count < 0)
	errx(1, "pin_compile failed");

    printf("compiled %d pattern%s\n", count, count == 1 ? "" : "s");

    pin_parse_t *parsep = pin_parse_open(pmp, workp, "pin09", opt_xml, 0);
    if (parsep == NULL)
	errx(1, "%s: pin_parse_open failed", opt_xml);

    if (opt_debug)
	pin_parse_flags_set(parsep, PIN_PF_DEBUG);

    pin_parse_passthru(parsep, opt_passthru);
    pin_parse_set_filter(parsep, xfp);
    pin_parse_set_rulebook(parsep, rb);

    pin_parse(parsep);

    if (opt_dump)
	pin_parse_emit_xml(parsep, stdout);

    pin_workspace_close(workp);
    pa_mmap_close(pmp);
    pin_parse_destroy(parsep);

    slaxEnable(SLAX_CLEANUP);

    return 0;
}
