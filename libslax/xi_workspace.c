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
 * Phil Shafer (phil@) July 2016
 *
 * A "workspace" is a space where we work, creating nodes, names,
 * atoms, etc.   It can commonize these pools from which these are
 * built, allowing multiple trees (documents) to share.
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
#include <libslax/pa_bitmap.h>
#include <libslax/xi_common.h>
#include <libslax/xi_source.h>
#include <libslax/xi_rules.h>
#include <libslax/xi_tree.h>
#include <libslax/xi_workspace.h>
#include <libslax/xi_parse.h>

xi_workspace_t *
xi_workspace_open (pa_mmap_t *pmp, const char *name)
{
    xi_namepool_t *names = NULL;
    pa_istr_t *pip = NULL;
    pa_arb_t *pap = NULL;
    pa_pat_t *ppp = NULL;
    xi_node_t *nodep = NULL;
    pa_fixed_t *nodes = NULL;
    pa_fixed_t *prefix_mapping = NULL;
    xi_workspace_t *workp = NULL;
    char namebuf[PA_MMAP_HEADER_NAME_LEN];

    /* Holds the names of our elements, attributes, etc */
    xi_mk_name(namebuf, name, "names");
    names = xi_namepool_open(pmp, namebuf);
    if (names == NULL)
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

    workp = calloc(1, sizeof(*workp));
    if (workp == NULL)
	goto fail;

    workp->xw_mmap = pmp;
    workp->xw_nodes = nodes;
    workp->xw_names = names;
    workp->xw_prefix_mapping = prefix_mapping;
    workp->xw_textpool = pap;

    return workp;

 fail:
    if (pap)
	pa_arb_close(pap);
    if (pip)
	pa_istr_close(pip);
    if (ppp)
	pa_pat_close(ppp);
    if (nodes)
	pa_fixed_close(nodes);
    if (prefix_mapping)
	pa_fixed_close(prefix_mapping);
    if (names)
	xi_namepool_close(names);
    return NULL;
}

xi_namepool_t *
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

void
xi_namepool_close (xi_namepool_t *pool)
{
    if (pool) {
	pa_istr_close(pool->xnp_names);
	pa_pat_close(pool->xnp_names_index);
	free(pool);
    }
}

pa_atom_t
xi_namepool_atom (xi_workspace_t *xwp, const char *data, int createp)
{
    uint16_t len = strlen(data) + 1;
    pa_pat_t *ppp = xwp->xw_names->xnp_names_index;
    pa_atom_t atom = pa_pat_get_atom(ppp, len, data);
    if (atom == PA_NULL_ATOM && createp) {
	atom = pa_istr_string(ppp->pp_data, data);
	if (atom == PA_NULL_ATOM)
	    pa_warning(0, "create key failed: %s", data);
	else if (!pa_pat_add(ppp, atom, len))
	    pa_warning(0, "duplicate key: %s", data);
    }

    return atom;
}

pa_atom_t
xi_get_attrib (xi_workspace_t *xwp, xi_node_t *nodep, pa_atom_t name_atom)
{
    pa_atom_t node_atom;
    int attrib_seen = FALSE;
    xi_depth_t depth = nodep->xn_depth;

    if (!(nodep->xn_flags & XNF_ATTRIBS_PRESENT))
	return PA_NULL_ATOM;

#if 0
    if (!(nodep->xn_flags & XNF_ATTRIBS_EXTRACTED))
	xi_node_attrib_extract(xwp, nodep);
#endif

    for (node_atom = nodep->xn_contents; node_atom != PA_NULL_ATOM;
	 node_atom = nodep->xn_next) {
	nodep = xi_node_addr(xwp, node_atom);
	if (node_atom == PA_NULL_ATOM)	/* Should not occur */
	    break;

	if (nodep->xn_depth <= depth)
	    break;		/* Found end of children */

	if (nodep->xn_type != XI_TYPE_ATTRIB)
	    continue;

	attrib_seen = TRUE;

	if (nodep->xn_name == name_atom)
	    return nodep->xn_contents;
    }

    return PA_NULL_ATOM;
}
