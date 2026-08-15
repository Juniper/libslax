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

#ifndef LIBSLAX_PIN_WORKSPACE_H
#define LIBSLAX_PIN_WORKSPACE_H

#include <parrotdb/pafixed.h>
#include <parrotdb/paarb.h>
#include <parrotdb/paistr.h>
#include <parrotdb/papat.h>
#include <libpin/pin_node.h>

typedef struct pin_workspace_s {
    pa_mmap_t *pw_mmap;	/* Base memory information */
    pa_fixed_t *pw_nodes;	/* Pool of nodes (pin_node_t) */
    pa_istr_t *pw_names;	/* Array of names (element, attr, etc) */
    pa_pat_t *pw_names_index;	/* Patricia tree for names */
    pa_fixed_t *pw_ns_map; /* Map from prefixes to URLs (pin_ns_map_t) */
    pa_pat_t *pw_ns_map_index;	/* Index of pw_ns_map entries */
    pa_arb_t *pw_textpool;	/* Text data values */
    pa_fixed_t *pw_nodeset_chunks; /* Pool of chunks for nodesets node lists */
    pa_fixed_t *pw_nodeset_info; /* Pool of chunks for nodeset "info" data */
} pin_workspace_t;

pin_workspace_t *
pin_workspace_open (pa_mmap_t *pmp, const char *name);

void
pin_workspace_close (pin_workspace_t *workp);

void
pin_namepool_open (pa_mmap_t *pmap, const char *basename,
		  pa_istr_t **namesp, pa_pat_t **names_indexp);

void
pin_namepool_close (pa_istr_t *names, pa_pat_t *names_index);

void
pin_ns_open (pa_mmap_t *pmap, const char *basename,
	    pa_fixed_t **nsp, pa_pat_t **ns_indexp);

void
pin_ns_close (pa_fixed_t *ns_map, pa_pat_t *ns_map_index);

pin_ns_map_id_t
pin_ns_find (pin_workspace_t *pwp, const char *prefix, const char *uri,
	    pin_boolean_t createp);

#include "gen/pin_node_gen.h"

pa_atom_t
pin_namepool_atom (pin_workspace_t *pwp, const char *data, pin_boolean_t createp);

static inline const char *
pin_namepool_string (pin_workspace_t *pwp, pa_atom_t name_atom)
{
    return pa_istr_atom_string(pwp->pw_names, pa_istr_atom(name_atom));
}

pa_atom_t
pin_get_attrib (pin_workspace_t *pwp, pin_node_t *nodep, pa_atom_t name_atom);

static inline const char *
pin_textpool_string (pin_workspace_t *pwp, pa_atom_t atom)
{
    return pa_arb_atom_addr(pwp->pw_textpool, pa_arb_atom(atom));
}

static inline const char *
pin_get_attrib_string (pin_workspace_t *pwp, pin_node_t *nodep,
		      pa_atom_t name_atom)
{
    pa_atom_t atom = pin_get_attrib(pwp, nodep, name_atom);
    return (atom == PA_NULL_ATOM) ? NULL : pin_textpool_string(pwp, atom);
}

#include "gen/pin_ns_map_gen.h"

#endif /* LIBSLAX_PIN_WORKSPACE_H */

