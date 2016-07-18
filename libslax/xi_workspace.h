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

#ifndef LIBSLAX_XI_WORKSPACE_H
#define LIBSLAX_XI_WORKSPACE_H

typedef struct xi_workspace_s {
    pa_mmap_t *xw_mmap;	/* Base memory information */
    pa_fixed_t *xw_nodes;	/* Pool of nodes (xi_node_t) */
    xi_namepool_t *xw_names; /* Namepool for local part of names */
    pa_fixed_t *xw_prefix_mapping; /* Map from prefixes to URLs (xi_ns_map_t)*/
    pa_arb_t *xw_textpool;	/* Text data values */
} xi_workspace_t;

xi_workspace_t *
xi_workspace_open (pa_mmap_t *pmp, const char *name);

xi_namepool_t *
xi_namepool_open (pa_mmap_t *pmap, const char *basename);

void
xi_namepool_close (xi_namepool_t *pool);

static inline xi_node_t *
xi_node_alloc (xi_workspace_t *xwp, pa_atom_t *atomp)
{
    if (atomp == NULL)		/* Should not occur */
	return NULL;

    pa_atom_t node_atom = pa_fixed_alloc_atom(xwp->xw_nodes);
    xi_node_t *nodep = pa_fixed_atom_addr(xwp->xw_nodes, node_atom);

    *atomp = node_atom;
    return nodep;
}

static inline xi_node_t *
xi_node_addr(xi_workspace_t *xwp, pa_atom_t node_atom)
{
    return pa_fixed_atom_addr(xwp->xw_nodes, node_atom);
}

pa_atom_t
xi_namepool_atom (xi_workspace_t *xwp, const char *data, int createp);

static inline const char *
xi_namepool_string (xi_workspace_t *xwp, pa_atom_t name_atom)
{
    return pa_istr_atom_string(xwp->xw_names->xnp_names, name_atom);
}

pa_atom_t
xi_get_attrib (xi_workspace_t *xwp, xi_node_t *nodep, pa_atom_t name_atom);

static inline const char *
xi_textpool_string (xi_workspace_t *xwp, pa_atom_t atom)
{
    return pa_arb_atom_addr(xwp->xw_textpool, atom);
}

static inline const char *
xi_get_attrib_string (xi_workspace_t *xwp, xi_node_t *nodep,
		      pa_atom_t name_atom)
{
    pa_atom_t atom = xi_get_attrib(xwp, nodep, name_atom);
    return (atom == PA_NULL_ATOM) ? NULL : xi_textpool_string(xwp, atom);
}

#endif /* LIBSLAX_XI_WORKSPACE_H */

