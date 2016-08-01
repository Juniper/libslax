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
    pa_istr_t *names = NULL;
    pa_pat_t *names_index = NULL;
    pa_fixed_t *ns_map = NULL;
    pa_pat_t *ns_map_index = NULL;
    pa_istr_t *pip = NULL;
    pa_arb_t *pap = NULL;
    pa_pat_t *ppp = NULL;
    xi_node_t *nodep = NULL;
    pa_fixed_t *nodes = NULL;
    xi_workspace_t *workp = NULL;
    char namebuf[PA_MMAP_HEADER_NAME_LEN];

    /* Holds the names of our elements, attributes, etc */
    xi_mk_name(namebuf, name, "names");
    xi_namepool_open(pmp, namebuf, &names, &names_index);
    if (names == NULL)
	goto fail;

    xi_mk_name(namebuf, name, "namespaces");
    xi_ns_open(pmp, namebuf, &ns_map, &ns_map_index);
    if (ns_map == NULL)
	goto fail;

    nodes = pa_fixed_open(pmp, xi_mk_name(namebuf, name, "nodes"), XI_SHIFT,
			 sizeof(*nodep), XI_MAX_ATOMS);
    if (nodes == NULL)
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
    workp->xw_names_index = names_index;
    workp->xw_ns_map = ns_map;
    workp->xw_ns_map_index = ns_map_index;
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
    if (names)
	pa_istr_close(names);
    if (names_index)
	pa_pat_close(names_index);
    if (ns_map)
	pa_fixed_close(ns_map);
    if (ns_map_index)
	pa_pat_close(ns_map_index);
    return NULL;
}

void
xi_namepool_open (pa_mmap_t *pmap, const char *basename,
		  pa_istr_t **namesp, pa_pat_t **names_indexp)
{
    char namebuf[PA_MMAP_HEADER_NAME_LEN];
    pa_istr_t *pip = NULL;
    pa_pat_t *ppp = NULL;

    /* The name pool holds the names of our elements, attributes, etc */
    pip = pa_istr_open(pmap, xi_mk_name(namebuf, basename, "data"),
		       XI_SHIFT, XI_ISTR_SHIFT, XI_MAX_ATOMS);
    if (pip == NULL)
	return;

    ppp = pa_pat_open(pmap, xi_mk_name(namebuf, basename, "index"),
		      pip, pa_pat_istr_key_func,
		      PA_PAT_MAXKEY, XI_SHIFT, XI_MAX_ATOMS);
    if (ppp == NULL) {
	pa_istr_close(pip);
	return;
    }

    *namesp = pip;
    *names_indexp = ppp;
}

static const uint8_t *
xi_ns_key_func (pa_pat_t *pp, pa_pat_node_t *node)
{
    return pa_fixed_atom_addr(pp->pp_data, node->ppn_data);
}

void
xi_ns_open (pa_mmap_t *pmap, const char *basename,
	    pa_fixed_t **nsp, pa_pat_t **ns_indexp)
{
    char namebuf[PA_MMAP_HEADER_NAME_LEN];
    pa_fixed_t *pfp = NULL;
    pa_pat_t *ppp = NULL;

    /* The ns pool holds the names of our elements, attributes, etc */
    pfp = pa_fixed_open(pmap, xi_mk_name(namebuf, basename, "data"),
			XI_SHIFT, sizeof(xi_ns_map_t), XI_MAX_ATOMS);
    if (pfp == NULL)
	return;

    ppp = pa_pat_open(pmap, xi_mk_name(namebuf, basename, "index"),
		      pfp, xi_ns_key_func,
		      PA_PAT_MAXKEY, XI_SHIFT, XI_MAX_ATOMS);
    if (ppp == NULL) {
	pa_fixed_close(pfp);
	return;
    }

    *nsp = pfp;
    *ns_indexp = ppp;
}

pa_atom_t
xi_namepool_atom (xi_workspace_t *xwp, const char *data, xi_boolean_t createp)
{
    uint16_t len = strlen(data) + 1;
    pa_pat_t *ppp = xwp->xw_names_index;

    pa_atom_t atom = pa_pat_get_atom(ppp, len, data);
    if (atom == PA_NULL_ATOM && createp) {
	/* Allocate the name from our pool and add it to the tree */
	atom = pa_istr_string(xwp->xw_names, data);
	if (atom == PA_NULL_ATOM)
	    pa_warning(0, "namepool create key failed for key '%s'", data);
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

#if 0 /* XXX */
    if (!(nodep->xn_flags & XNF_ATTRIBS_EXTRACTED))
	xi_node_attrib_extract(xwp, nodep);
#endif

    for (node_atom = nodep->xn_contents; node_atom != PA_NULL_ATOM;
	 node_atom = nodep->xn_next) {
	nodep = xi_node_addr(xwp, node_atom);
	if (nodep == NULL)	/* Should not occur */
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

/*
 * Find the index of a given prefix-to-uri mapping.
 *
 * Note that we allow empty strings for either of these values, since
 * that's how we define the current namespace (when prefix is empty)
 * or the default namespace (when uri is empty).
 *
 * Note also that different return values from this do not imply
 * different namespace, just different prefix mappings.  One can use
 * distinct prefixes to access same namespace, like:
 *    <a xmlns="a.men"><amen:b xmlns:amen="a.men"/></a>
 * Retaining this information allows us to emit XML identical to the
 * original input.  The cost is an extra lookup in xw_ns_map to see
 * the underlaying atom numbers of the URI strings (which reside in
 * the name pool).  Another fine engineering trade off that's such to
 * bite me in the lower cheeks one day.
 */
pa_atom_t
xi_ns_find (xi_workspace_t *xwp, const char *prefix, const char *uri,
	    xi_boolean_t createp)
{
    pa_atom_t prefix_atom = PA_NULL_ATOM, uri_atom = PA_NULL_ATOM;

    if (prefix != NULL && *prefix != '\0') {
	prefix_atom = xi_namepool_atom(xwp, prefix, TRUE);
	if (prefix_atom == PA_NULL_ATOM)
	    return PA_NULL_ATOM;
    }

    if (uri != NULL && *uri != '\0') {
	uri_atom = xi_namepool_atom(xwp, uri, TRUE);
	if (uri_atom == PA_NULL_ATOM)
	    return PA_NULL_ATOM;
    }

    pa_pat_t *ppp = xwp->xw_ns_map_index;
    pa_fixed_t *pfp = xwp->xw_ns_map;
    xi_ns_map_t ns = { prefix_atom, uri_atom };
    pa_atom_t atom = pa_pat_get_atom(ppp, sizeof(ns), &ns);
    if (atom == PA_NULL_ATOM && createp) {
	atom = pa_fixed_alloc_atom(pfp);
	if (atom == PA_NULL_ATOM) {
	    pa_warning(0, "namespace create key failed for '%s%s%s'",
		       prefix ?: "", prefix ? ":" : "", uri ?: "");
	    return PA_NULL_ATOM;
	}

	xi_ns_map_t *nsp = pa_fixed_atom_addr(pfp, atom);
	if (nsp == NULL)
	    return PA_NULL_ATOM; /* Should not occur */

	*nsp = ns;		/* Initialize newly allocated ns_map entry */

	/* Add it to the patricia tree */
	if (!pa_pat_add(ppp, atom, sizeof(ns))) {
	    pa_fixed_free_atom(pfp, atom);
	    pa_warning(0, "duplicate key failure for namespace '%s%s%s'",
		       prefix ?: "", prefix ? ":" : "", uri ?: "");
	    return PA_NULL_ATOM;
	}
    }

    return atom;

}
