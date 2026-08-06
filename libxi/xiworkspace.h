/*
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

#include <parrotdb/pafixed.h>
#include <parrotdb/paarb.h>
#include <parrotdb/paistr.h>
#include <parrotdb/papat.h>
#include <libxi/xinode.h>

typedef struct xi_workspace_s {
    pa_mmap_t *xw_mmap;	/* Base memory information */
    pa_fixed_t *xw_nodes;	/* Pool of nodes (xi_node_t) */
    pa_istr_t *xw_names;	/* Array of names (element, attr, etc) */
    pa_pat_t *xw_names_index;	/* Patricia tree for names */
    pa_fixed_t *xw_ns_map; /* Map from prefixes to URLs (xi_ns_map_t) */
    pa_pat_t *xw_ns_map_index;	/* Index of xw_ns_map entries */
    pa_arb_t *xw_textpool;	/* Text data values */
    pa_fixed_t *xw_nodeset_chunks; /* Pool of chunks for nodesets node lists */
    pa_fixed_t *xw_nodeset_info; /* Pool of chunks for nodeset "info" data */
} xi_workspace_t;

xi_workspace_t *
xi_workspace_open (pa_mmap_t *pmp, const char *name);

void
xi_namepool_open (pa_mmap_t *pmap, const char *basename,
		  pa_istr_t **namesp, pa_pat_t **names_indexp);

void
xi_ns_open (pa_mmap_t *pmap, const char *basename,
	    pa_fixed_t **nsp, pa_pat_t **ns_indexp);

xi_ns_map_id_t
xi_ns_find (xi_workspace_t *xwp, const char *prefix, const char *uri,
	    xi_boolean_t createp);

#include "gen/xi_node_gen.h"

pa_atom_t
xi_namepool_atom (xi_workspace_t *xwp, const char *data, xi_boolean_t createp);

static inline const char *
xi_namepool_string (xi_workspace_t *xwp, pa_atom_t name_atom)
{
    return pa_istr_atom_string(xwp->xw_names, pa_istr_atom(name_atom));
}

pa_atom_t
xi_get_attrib (xi_workspace_t *xwp, xi_node_t *nodep, pa_atom_t name_atom);

static inline const char *
xi_textpool_string (xi_workspace_t *xwp, pa_atom_t atom)
{
    return pa_arb_atom_addr(xwp->xw_textpool, pa_arb_atom(atom));
}

static inline const char *
xi_get_attrib_string (xi_workspace_t *xwp, xi_node_t *nodep,
		      pa_atom_t name_atom)
{
    pa_atom_t atom = xi_get_attrib(xwp, nodep, name_atom);
    return (atom == PA_NULL_ATOM) ? NULL : xi_textpool_string(xwp, atom);
}

#include "gen/xi_ns_map_gen.h"

#endif /* LIBSLAX_XI_WORKSPACE_H */

