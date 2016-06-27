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
#include <libslax/slax.h>
#include <libslax/pa_common.h>
#include <libslax/pa_config.h>
#include <libslax/pa_mmap.h>
#include <libslax/pa_fixed.h>
#include <libslax/pa_arb.h>
#include <libslax/pa_istr.h>
#include <libslax/pa_pat.h>
#include <libslax/xi_source.h>
#include <libslax/xi_parse.h>

#define XI_MAX_ATOMS	(1<<26)
#define XI_SHIFT	12
#define XI_ISTR_SHIFT	2

static inline const char *
xi_mk_name (char *namebuf, const char *name, const char *ext)
{
    return pa_config_name(namebuf, PA_MMAP_HEADER_NAME_LEN, name, ext);
}

static xi_namepool_t *
xi_namepool_open (pa_mmap_t *pmap, const char *basename)
{
    char namebuf[PA_MMAP_HEADER_NAME_LEN];
    pa_istr_t *pip = NULL;
    pa_pat_t *ppp = NULL;
    xi_namepool_t *pool;

    /* The namepool holds the names of our elements, attributes, etc */
    pip = pa_istr_open(pmap, xi_mk_name(namebuf, basename, "data"),
		       XI_SHIFT, XI_ISTR_SHIFT, XI_MAX_ATOMS);
    if (pip == NULL)
	return NULL;

    ppp = pa_pat_open(pmap, xi_mk_name(namebuf, basename, "index"),
		      pip, pa_pat_istr_key_func,
		      PA_PAT_MAXKEY, XI_SHIFT, XI_MAX_ATOMS);
    if (ppp == NULL) {
	pa_istr_close(pip);
	return NULL;
    }

    pool = calloc(1, sizeof(*pool));
    if (pool == NULL) {
	pa_pat_close(ppp);
	pa_istr_close(pip);
	return NULL;
    }

    pool->xnp_names = pip;
    pool->xnp_names_index = ppp;

    return pool;
}

static void
xi_namepool_close (xi_namepool_t *pool)
{
    if (pool) {
	pa_istr_close(pool->xnp_names);
	pa_pat_close(pool->xnp_names_index);
	free(pool);
    }
}

xi_parse_t *
xi_parse_open (pa_mmap_t *pmp, const char *name UNUSED,
	       const char *input, xi_source_flags_t flags)
{
    xi_source_t *srcp = NULL;
    xi_parse_t *parsep = NULL;
    xi_insert_t *xip = NULL;
    xi_tree_t *xtp = NULL;
    xi_namepool_t *local_names = NULL;
    xi_namepool_t *prefix_names = NULL;
    xi_namepool_t *url_names = NULL;
    pa_istr_t *pip = NULL;
    pa_arb_t *pap = NULL;
    pa_pat_t *ppp = NULL;
    xi_node_t *nodep = NULL;
    pa_fixed_t *nodes = NULL;
    pa_fixed_t *prefix_mapping = NULL;
    pa_atom_t node_atom;
    char namebuf[PA_MMAP_HEADER_NAME_LEN];

    /*
     * XXX okay, so this is crap, just a bunch of initialization that
     * needs to be broken out in distinct functions.
     */

    srcp = xi_source_open(input, flags);
    if (srcp == NULL)
	goto fail;

    /* Holds the names of our elements, attributes, etc */
    xi_mk_name(namebuf, name, "local-names");
    local_names = xi_namepool_open(pmp, namebuf);
    if (local_names == NULL)
	goto fail;

    /* Holds the names of our prefix strings */
    xi_mk_name(namebuf, name, "prefix-names");
    prefix_names = xi_namepool_open(pmp, namebuf);
    if (prefix_names == NULL)
	goto fail;

    /* Holds the names of namespace URLs */
    xi_mk_name(namebuf, name, "url-names");
    url_names = xi_namepool_open(pmp, namebuf);
    if (url_names == NULL)
	goto fail;

    /* The xi_tree_t is the tree we'll be inserting into */
    xtp = calloc(1, sizeof(*xtp));
    if (xtp == NULL)
	goto fail;

    nodes = pa_fixed_open(pmp, xi_mk_name(namebuf, name, "nodes"), XI_SHIFT,
			 sizeof(*nodep), XI_MAX_ATOMS);
    if (nodes == NULL)
	goto fail;

    prefix_mapping = pa_fixed_open(pmp,
			    xi_mk_name(namebuf, name, "prefix-mapping"),
			    XI_SHIFT, sizeof(xi_ns_map_t), XI_MAX_ATOMS);
    if (prefix_mapping == NULL)
	goto fail;

    pap = pa_arb_open(pmp, xi_mk_name(namebuf, name, "data"));
    if (pap == NULL)
	goto fail;

    xtp->xt_infop = pa_mmap_header(pmp, xi_mk_name(namebuf, name, "tree"),
				   PA_TYPE_TREE, 0, sizeof(*xtp->xt_infop));
    xtp->xt_mmap = pmp;
    xtp->xt_max_depth = 0;
    xtp->xt_nodes = nodes;
    xtp->xt_local_names = local_names;
    xtp->xt_prefix_names = prefix_names;
    xtp->xt_url_names = url_names;
    xtp->xt_prefix_mapping = prefix_mapping;
    xtp->xt_textpool = pap;

    /* The xi_insert_t is the point in the tree at which we are inserting */
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
    parsep->xp_insert = xip;

    node_atom = pa_fixed_alloc_atom(nodes);
    nodep = pa_fixed_atom_addr(nodes, node_atom);
    if (nodep == NULL)
	goto fail;
    nodep->xn_type = XI_TYPE_ROOT;
    nodep->xn_depth = 0;
    nodep->xn_ns = PA_NULL_ATOM;
    nodep->xn_name = PA_NULL_ATOM;
    nodep->xn_next = PA_NULL_ATOM;
    nodep->xn_contents = PA_NULL_ATOM;

    xtp->xt_root = node_atom;
    xip->xi_stack[xip->xi_depth].xs_atom = node_atom;
    xip->xi_stack[xip->xi_depth].xs_node = nodep;

    return parsep;

 fail:
    if (pap)
	pa_arb_close(pap);
    if (pip)
	pa_istr_close(pip);
    if (ppp)
	pa_pat_close(ppp);
    if (xip)
	free(xip);
    if (xtp)
	free(xtp);
    if (nodes)
	pa_fixed_close(nodes);
    if (prefix_mapping)
	pa_fixed_close(prefix_mapping);
    if (local_names)
	xi_namepool_close(local_names);
    if (prefix_names)
	xi_namepool_close(prefix_names);
    if (url_names)
	xi_namepool_close(url_names);
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

static pa_atom_t
xi_namepool_atom (xi_namepool_t *xnp, const char *data)
{
    uint16_t len = strlen(data) + 1;
    pa_pat_t *ppp = xnp->xnp_names_index;
    pa_atom_t atom = pa_pat_get_atom(ppp, len, data);
    if (atom == PA_NULL_ATOM) {
	atom = pa_istr_string(ppp->pp_data, data);
	if (atom == PA_NULL_ATOM)
	    pa_warning(0, "create key failed: %s", data);
	else if (!pa_pat_add(ppp, atom, len))
	    pa_warning(0, "duplicate key: %s", data);
    }

    return atom;
}

static const char *
xi_namepool_string (xi_namepool_t *xnp, pa_atom_t name_atom)
{
    return pa_istr_atom_string(xnp->xnp_names, name_atom);
}

static void
xi_insert_push (xi_insert_t *xip, pa_atom_t atom, xi_node_t *nodep)
{
    xip->xi_depth += 1;
    xip->xi_stack[xip->xi_depth].xs_atom = atom;
    xip->xi_stack[xip->xi_depth].xs_node = nodep;
}

static void
xi_insert_open (xi_parse_t *parsep, const char *data)
{
    xi_insert_t *xip = parsep->xp_insert;
    xi_namepool_t *xnp = xip->xi_tree->xt_local_names;
    pa_atom_t name_atom = xi_namepool_atom(xnp, data);
    if (name_atom == PA_NULL_ATOM)
	return;

    pa_atom_t node_atom = pa_fixed_alloc_atom(xip->xi_tree->xt_nodes);
    xi_node_t *nodep = pa_fixed_atom_addr(xip->xi_tree->xt_nodes, node_atom);
    if (nodep == NULL)
	return;

    nodep->xn_type = XI_TYPE_ELT;
    nodep->xn_ns = PA_NULL_ATOM;
    nodep->xn_name = name_atom;
    nodep->xn_contents = PA_NULL_ATOM;

    slaxLog("xi_insert_open: [%s] %u (depth %u)", data, name_atom,
	   xip->xi_depth + 1);

    /*
     * If we don't have a child, make one.  Otherwise append it.
     */
    xi_istack_t *xsp = &xip->xi_stack[xip->xi_depth];
    if (xsp->xs_node->xn_contents == PA_NULL_ATOM) {
	/* Record us as the child of the current stack node */
	xsp->xs_node->xn_contents = node_atom;

	/* Set our "parent" as the current node */
	nodep->xn_next = xsp->xs_atom;

    } else {
	/* Append our node */
	nodep->xn_next = xsp->xs_atom;
	xsp->xs_last_node->xn_next = node_atom;
    }

    /* Mark the "last" as us */
    xsp->xs_last_atom = node_atom;
    xsp->xs_last_node = nodep;

    /* Push our node on the stack */
    xi_insert_push(xip, node_atom, nodep);

    /* Set our depth */
    nodep->xn_depth = xip->xi_depth;
}

static void
xi_insert_close (xi_parse_t *parsep, const char *data)
{
    xi_insert_t *xip = parsep->xp_insert;
    xi_namepool_t *xnp = xip->xi_tree->xt_local_names;
    pa_atom_t name_atom = xi_namepool_atom(xnp, data);
    if (name_atom == PA_NULL_ATOM)
	return;
    
    slaxLog("xi_insert_close: [%s] %u (depth %u)", data, name_atom,
	   xip->xi_depth);

    xi_istack_t *xsp = &xip->xi_stack[xip->xi_depth];

    if (xip->xi_depth == 0 || xsp->xs_node == NULL
	|| xsp->xs_node->xn_name != name_atom) {
	xi_source_failure(parsep->xp_srcp, 0, "close doesn't match");
	return;
    }

    bzero(xsp, sizeof(*xsp));
    xip->xi_depth -= 1;
}

static void
xi_insert_text (xi_parse_t *parsep, const char *data, size_t len)
{
    xi_insert_t *xip = parsep->xp_insert;
    pa_arb_t *prp = xip->xi_tree->xt_textpool;
    pa_atom_t data_atom = pa_arb_alloc(prp, len + 1);
    char *cp = pa_arb_atom_addr(prp, data_atom);

    if (cp == NULL)
	return;

    memcpy(cp, data, len);
    cp[len] = '\0';

    pa_atom_t node_atom = pa_fixed_alloc_atom(xip->xi_tree->xt_nodes);
    xi_node_t *nodep = pa_fixed_atom_addr(xip->xi_tree->xt_nodes, node_atom);
    if (nodep == NULL) {
	pa_arb_free_atom(prp, data_atom);
	return;
    }

    nodep->xn_type = XI_TYPE_TEXT;
    nodep->xn_ns = PA_NULL_ATOM;
    nodep->xn_name = PA_NULL_ATOM;
    nodep->xn_contents = data_atom;

    slaxLog("xi_insert_text: [%.*s] %u (depth %u)", len, data, data_atom,
	   xip->xi_depth + 1);


    /*
     * If we don't have a child, make one.  Otherwise append it.
     */
    xi_istack_t *xsp = &xip->xi_stack[xip->xi_depth];
    if (xsp->xs_node->xn_contents == PA_NULL_ATOM) {
	/* Record us as the child of the current stack node */
	xsp->xs_node->xn_contents = node_atom;

	/* Set our "parent" as the current node */
	nodep->xn_next = xsp->xs_atom;

    } else {
	/* Append our node */
	nodep->xn_next = xsp->xs_atom;
	xsp->xs_last_node->xn_next = node_atom;
    }

    /* Mark the "last" as us */
    xsp->xs_last_atom = node_atom;
    xsp->xs_last_node = nodep;

    /* Set our depth */
    nodep->xn_depth = xip->xi_depth + 1;
}

static void
xi_insert_attrs (xi_parse_t *parsep, const char *data)
{
    xi_insert_t *xip = parsep->xp_insert;
    pa_arb_t *prp = xip->xi_tree->xt_textpool;
    size_t len = strlen(data);
    pa_atom_t data_atom = pa_arb_alloc(prp, len + 1);
    char *cp = pa_arb_atom_addr(prp, data_atom);

    if (cp == NULL)
	return;

    memcpy(cp, data, len);
    cp[len] = '\0';

    pa_atom_t node_atom = pa_fixed_alloc_atom(xip->xi_tree->xt_nodes);
    xi_node_t *nodep = pa_fixed_atom_addr(xip->xi_tree->xt_nodes, node_atom);
    if (nodep == NULL) {
	pa_arb_free_atom(prp, data_atom);
	return;
    }

    nodep->xn_type = XI_TYPE_ATSTR;
    nodep->xn_ns = PA_NULL_ATOM;
    nodep->xn_name = PA_NULL_ATOM;
    nodep->xn_contents = data_atom;

    slaxLog("xi_insert_attrs: [%.*s] %u (depth %u)", len, data, data_atom,
	   xip->xi_depth + 1);

    /*
     * If we don't have a child, make one.  Otherwise append it.
     */
    xi_istack_t *xsp = &xip->xi_stack[xip->xi_depth];
    if (xsp->xs_node->xn_contents == PA_NULL_ATOM) {
	/* Record us as the child of the current stack node */
	xsp->xs_node->xn_contents = node_atom;

	/* Set our "parent" as the current node */
	nodep->xn_next = xsp->xs_atom;

    } else {
	/* Append our node */
	nodep->xn_next = xsp->xs_atom;
	xsp->xs_last_node->xn_next = node_atom;
    }

    /* Mark the "last" as us */
    xsp->xs_last_atom = node_atom;
    xsp->xs_last_node = nodep;

    /* Set our depth */
    nodep->xn_depth = xip->xi_depth + 1;
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
		slaxLog("text [%.*s]", len, data);
	    }
	    xi_insert_text(parsep, data, rest - data);
	    break;

	case XI_TYPE_OPEN:	/* Open tag */
	    if (!opt_quiet)
		slaxLog("open tag [%s] [%s]", data ?: "", rest ?: "");
	    xi_insert_open(parsep, data);
	    if (rest)
		xi_insert_attrs(parsep, rest);
	    break;

	case XI_TYPE_CLOSE:	/* Close tag */
	    if (!opt_quiet)
		slaxLog("close tag [%s] [%s]", data ?: "", rest ?: "");
	    xi_insert_close(parsep, data);
	    break;

	case XI_TYPE_PI:	/* Processing instruction */
	    if (!opt_quiet)
		slaxLog("pi [%s] [%s]", data ?: "", rest ?: "");
	    break;

	case XI_TYPE_DTD:	/* DTD nonsense */
	    if (!opt_quiet)
		slaxLog("dtd [%s] [%s]", data ?: "", rest ?: "");
	    break;

	case XI_TYPE_COMMENT:	/* Comment */
	    if (!opt_quiet)
		slaxLog("comment [%s] [%s]", data ?: "", rest ?: "");
	    break;

	case XI_TYPE_CDATA:	/* cdata */
	    if (!opt_quiet)
		slaxLog("cdata [%.*s]", (int)(rest - data), data);
	    break;
	}
    }

    return 0;
}

static int
xi_parse_dump_cb (xi_parse_t *parsep UNUSED, xi_node_type_t type,
		  xi_node_t *nodep UNUSED,
		  const char *data, void *opaque UNUSED)
{
    slaxLog("node: [%p] type %u, depth %u",
	    nodep, nodep ? nodep->xn_type : 0, nodep ? nodep->xn_depth : 0);

    switch (type) {
    case XI_TYPE_ROOT:
	slaxLog("(root)");
	break;

    case XI_TYPE_ELT:
	slaxLog("element: %s", data ?: "[error]");
	break;

    case XI_TYPE_TEXT:
	slaxLog("text: %s", data ?: "[error]");
	break;
    }

    return 0;
}

void
xi_parse_dump (xi_parse_t *parsep)
{
    xi_parse_emit(parsep, xi_parse_dump_cb, NULL);
}

static int
xi_parse_emit_xml_cb (xi_parse_t *parsep UNUSED, xi_node_type_t type,
		      xi_node_t *nodep UNUSED,
		      const char *data, void *opaque)
{
    FILE *out = opaque;

    switch (type) {
    case XI_TYPE_ROOT:
	fprintf(out, "<!-- start of output>\n");
	break;

    case XI_TYPE_OPEN:
	fprintf(out, "<%s>", data);
	break;

    case XI_TYPE_CLOSE:
	if (data)
	    fprintf(out, "</%s>\n", data);
	break;

    case XI_TYPE_TEXT:
	fprintf(out, "%s", data);
	break;

    case XI_TYPE_ATSTR:
	fprintf(out, "<attrib>%s</>", data);
	break;

    case XI_TYPE_EOF:
	fprintf(out, "<!-- end of output>\n");
	break;
    }

    return 0;
}

void
xi_parse_emit_xml (xi_parse_t *parsep, FILE *out)
{
    xi_parse_emit(parsep, xi_parse_emit_xml_cb, out);
}

void
xi_parse_emit (xi_parse_t *parsep, xi_parse_emit_fn func, void *opaque)
{
    xi_insert_t *xip = parsep->xp_insert;
    xi_tree_t *xtp = xip->xi_tree;
    const char *cp;
    pa_atom_t node_atom = xtp->xt_root;
    pa_atom_t next_node_atom;
    xi_node_t *nodep;
    xi_depth_t last_depth = 0;

    while (node_atom != PA_NULL_ATOM) {
	nodep = pa_fixed_atom_addr(xtp->xt_nodes, node_atom);
	if (nodep == NULL) {
	    slaxLog("null atom");
	    break;
	}

	if (last_depth && last_depth > nodep->xn_depth) {
	    cp = xi_namepool_string(xtp->xt_local_names, nodep->xn_name);
	    func(parsep, XI_TYPE_CLOSE, nodep, cp, opaque);
	    node_atom = nodep->xn_next;
	    last_depth = nodep->xn_depth;
	    continue;
	}

	if (nodep->xn_type == XI_TYPE_ROOT) {
	    next_node_atom = nodep->xn_contents ?: nodep->xn_next;
	    func(parsep, nodep->xn_type, nodep, NULL, opaque);

	} else if (nodep->xn_type == XI_TYPE_ELT) {
	    cp = xi_namepool_string(xtp->xt_local_names, nodep->xn_name);
	    next_node_atom = nodep->xn_contents ?: nodep->xn_next;
	    func(parsep, nodep->xn_type, nodep, cp, opaque);

	} else if (nodep->xn_type == XI_TYPE_TEXT) {
	    cp = pa_arb_atom_addr(xtp->xt_textpool, nodep->xn_contents);
	    next_node_atom = nodep->xn_next;
	    func(parsep, nodep->xn_type, nodep, cp, opaque);

	} else if (nodep->xn_type == XI_TYPE_ATSTR) {
	    cp = pa_arb_atom_addr(xtp->xt_textpool, nodep->xn_contents);
	    next_node_atom = nodep->xn_next;
	    func(parsep, nodep->xn_type, nodep, cp, opaque);

	} else {
	    slaxLog("unhandled node: %u", nodep->xn_type);
	    next_node_atom = PA_NULL_ATOM;
	}

	last_depth = nodep->xn_depth;
	node_atom = next_node_atom;
    }

    func(parsep, XI_TYPE_EOF, NULL, NULL, opaque);
}
