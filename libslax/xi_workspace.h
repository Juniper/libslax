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

/*
 * Each node has a prefix mapping that tells us which namespace it's
 * in.  We want to make this simple and reusable, but since prefixes
 * can be remapped within any hierarchy, it's only reusable in the
 * window where that mapping isn't changed.  But this makes finding
 * the prefix and url a simple lookup.  This means that it two nodes
 * have the same mapping (xi_ns_id_t) then they are in the same
 * namespace, but if they are different, then those two mappings'
 * xnm_uri fields must be compared to see if they have the same atom
 * number.  Since they are in a name-pool, "There can be only one!"
 * applies, so comparing the url atom number is sufficient.
 */
typedef struct xi_ns_map_s {
    pa_atom_t xnm_prefix;	/* Atom of prefix string (in namepool) */
    pa_atom_t xnm_uri;		/* Atom of URL string (in namepool) */
} xi_ns_map_t;

typedef struct xi_workspace_s {
    pa_mmap_t *xw_mmap;	/* Base memory information */
    pa_fixed_t *xw_nodes;	/* Pool of nodes (xi_node_t) */
    pa_istr_t *xw_names;	/* Array of names (element, attr, etc) */
    pa_pat_t *xw_names_index;	/* Patricia tree for names */
    pa_fixed_t *xw_ns_map; /* Map from prefixes to URLs (xi_ns_map_t)*/
    pa_pat_t *xw_ns_map_index;	/* Index of xw_ns_map entries */
    pa_arb_t *xw_textpool;	/* Text data values */
} xi_workspace_t;

xi_workspace_t *
xi_workspace_open (pa_mmap_t *pmp, const char *name);

void
xi_namepool_open (pa_mmap_t *pmap, const char *basename,
		  pa_istr_t **namesp, pa_pat_t **names_indexp);

void
xi_ns_open (pa_mmap_t *pmap, const char *basename,
	    pa_fixed_t **nsp, pa_pat_t **ns_indexp);

pa_atom_t
xi_ns_find (xi_workspace_t *xwp, const char *prefix, const char *uri,
	    xi_boolean_t createp);

PA_FIXED_FUNCTIONS(xi_node_t, xi_workspace_t, xw_nodes,
		   xi_node_alloc, xi_node_free, xi_node_addr);

pa_atom_t
xi_namepool_atom (xi_workspace_t *xwp, const char *data, xi_boolean_t createp);

static inline const char *
xi_namepool_string (xi_workspace_t *xwp, pa_atom_t name_atom)
{
    return pa_istr_atom_string(xwp->xw_names, name_atom);
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

PA_FIXED_FUNCTIONS(xi_ns_map_t, xi_workspace_t, xw_ns_map,
		   xi_ns_map_alloc, xi_ns_map_free, xi_ns_map_addr);

#endif /* LIBSLAX_XI_WORKSPACE_H */

