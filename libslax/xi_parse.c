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
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>

#include "slaxconfig.h"
#include <libslax/slaxdef.h>
#include <libslax/pa_common.h>
#include <libslax/pa_mmap.h>
#include <libslax/pa_fixed.h>
#include <libslax/pa_arb.h>
#include <libslax/pa_istr.h>
#include <libslax/pa_pat.h>
#include <libslax/xi_source.h>
#include <libslax/xi_parse.h>

#define XI_MAX_ATOMS	(1<<24)
#define XI_SHIFT	12
#define XI_ISTR_SHIFT	2

static const char *
xi_mk_name (char *namebuf, const char *name, const char *ext)
{
    snprintf(namebuf, PA_MMAP_HEADER_NAME_LEN, "%s.%s", name, ext);
    return namebuf;
}

xi_parse_t *
xi_parse_open (pa_mmap_t *pmp, const char *name UNUSED,
	       const char *input, xi_source_flags_t flags)
{
    xi_source_t *srcp = NULL;
    xi_parse_t *parsep = NULL;
    xi_insertion_t *xip = NULL;
    xi_tree_t *xtp = NULL;
    xi_namepool_t *xnp = NULL;
    pa_istr_t *pip = NULL;
    pa_pat_t *ppp = NULL;
    xi_node_t *nodep = NULL;
    pa_fixed_t *tree = NULL;
    pa_atom_t node_atom;
    char namebuf[PA_MMAP_HEADER_NAME_LEN];

    /*
     * XXX okay, so this is crap, just a bunch of initialization that
     * needs to be broken out in distinct functions.
     */

    srcp = xi_source_open(input, flags);
    if (srcp == NULL)
	goto fail;

    /* The namepool holds the names of our elements, attributes, etc */

    pip = pa_istr_open(pmp, xi_mk_name(namebuf, name, ".names"),
		       XI_SHIFT, XI_ISTR_SHIFT, XI_MAX_ATOMS);
    if (pip == NULL)
	goto fail;

    ppp = pa_pat_open(pmp, xi_mk_name(namebuf, name, ".nindex"),
		      pip, pa_pat_istr_key_func,
		      PA_PAT_MAXKEY, XI_SHIFT, XI_MAX_ATOMS);
    if (ppp == NULL)
	goto fail;

    xnp = calloc(1, sizeof(*xnp));
    if (xnp == NULL)
	goto fail;
    xnp->xnp_names = pip;
    xnp->xnp_names_index = ppp;

    /* The xi_tree_t is the tree we'll be inserting into */
    xtp = calloc(1, sizeof(*xtp));
    if (xtp == NULL)
	goto fail;

    tree = pa_fixed_open(pmp, xi_mk_name(namebuf, name, ".nodes"), XI_SHIFT,
			 sizeof(*nodep), XI_MAX_ATOMS);
    if (tree == NULL)
	goto fail;
    xtp->xt_infop = pa_mmap_header(pmp, xi_mk_name(namebuf, name, "tree"),
				   PA_TYPE_TREE, 0, sizeof(*xtp->xt_infop));
    xtp->xt_mmap = pmp;
    xtp->xt_namepool = xnp;
    xtp->xt_tree = tree;
    xtp->xt_max_depth = 0;

    /* The xi_insertion_t is the point in the tree at which we are inserting */
    xip = calloc(1, sizeof(*xip));
    if (xip == NULL)
	goto fail;
    xip->xi_tree = xtp;
    xip->xi_depth = 0;
    xip->xi_relation = XIR_CHILD;

    /* And finally, Fill in the parse structure */
    parsep = calloc(1, sizeof(*parsep));
    if (parsep == NULL)
	goto fail;
    parsep->xp_srcp = srcp;
    parsep->xp_insertion = xip;

    node_atom = pa_fixed_alloc_atom(tree);
    nodep = pa_fixed_atom_addr(tree, node_atom);
    if (nodep == NULL)
	goto fail;
    nodep->xn_type = XI_TYPE_ROOT;
    nodep->xn_depth = 0;
    nodep->xn_ns = PA_NULL_ATOM;
    nodep->xn_name = PA_NULL_ATOM;
    nodep->xn_next = PA_NULL_ATOM;
    nodep->xn_contents = PA_NULL_ATOM;

    xtp->xt_root = node_atom;
    xip->xi_stack[xip->xi_depth] = node_atom;

    return parsep;

 fail:
    if (pip)
	pa_istr_close(pip);
    if (ppp)
	pa_pat_close(ppp);
    if (xip)
	free(xip);
    if (xtp)
	free(xtp);
    if (xnp)
	free(xnp);
    if (parsep)
	free(parsep);
    if (srcp)
	xi_source_destroy(srcp);
    return NULL;
}

void
xi_parse_destroy (xi_parse_t *parsep UNUSED)
{
    return;
}

int
xi_parse (xi_parse_t *parsep)
{
    xi_source_t *srcp = parsep->xp_srcp;
    char *data, *rest;
    xi_node_type_t type;
    int opt_quiet = 0;
    int opt_unescape = 0;

    for (;;) {
	type = xi_source_next_token(srcp, &data, &rest);

	switch (type) {
	case XI_TYPE_NONE:	/* Unknown type */
	    return 1;

	case XI_TYPE_EOF:	/* End of file */
	    return 0;

	case XI_TYPE_FAIL:	/* Failure mode */
	    return -1;
	case XI_TYPE_TEXT:	/* Text content */
	    if (!opt_quiet) {
		int len;
		if (opt_unescape && data && rest)
		    len = xi_source_unescape(srcp, data, rest - data);
		else len = rest - data;
		printf("data [%.*s]\n", len, data);
	    }
	    break;

	case XI_TYPE_OPEN:	/* Open tag */
	    if (!opt_quiet)
		printf("open tag [%s] [%s]\n", data ?: "", rest ?: "");
	    break;

	case XI_TYPE_CLOSE:	/* Close tag */
	    if (!opt_quiet)
		printf("close tag [%s] [%s]\n", data ?: "", rest ?: "");
	    break;

	case XI_TYPE_PI:	/* Processing instruction */
	    if (!opt_quiet)
		printf("pi [%s] [%s]\n", data ?: "", rest ?: "");
	    break;

	case XI_TYPE_DTD:	/* DTD nonsense */
	    if (!opt_quiet)
		printf("dtd [%s] [%s]\n", data ?: "", rest ?: "");
	    break;

	case XI_TYPE_COMMENT:	/* Comment */
	    if (!opt_quiet)
		printf("comment [%s] [%s]\n", data ?: "", rest ?: "");
	    break;

	case XI_TYPE_CDATA:	/* cdata */
	    if (!opt_quiet)
		printf("cdata [%.*s]\n", (int)(rest - data), data);
	    break;

	case XI_TYPE_ATTR:	/* XML attribute */
	    break;

	case XI_TYPE_NS:	/* XML namespace */
	    break;

	}
    }

    return 0;
}
