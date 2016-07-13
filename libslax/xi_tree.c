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
#include <libslax/pa_bitmap.h>
#include <libslax/xi_common.h>
#include <libslax/xi_rules.h>
#include <libslax/xi_tree.h>

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
xi_tree_namepool_atom (xi_tree_t *treep, const char *data, int createp)
{
    uint16_t len = strlen(data) + 1;
    pa_pat_t *ppp = treep->xt_names->xnp_names_index;
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
xi_tree_get_attrib (xi_tree_t *treep, xi_node_t *nodep, pa_atom_t name_atom)
{
    pa_atom_t node_atom;
    int attrib_seen = FALSE;
    xi_depth_t depth = nodep->xn_depth;

    if (!(nodep->xn_flags & XNF_ATTRIBS_PRESENT))
	return PA_NULL_ATOM;

#if 0
    if (!(nodep->xn_flags & XNF_ATTRIBS_EXTRACTED))
	xi_node_attrib_extract(treep, nodep);
#endif

    for (node_atom = nodep->xn_contents; node_atom != PA_NULL_ATOM;
	 node_atom = nodep->xn_next) {
	nodep = xi_node_addr(treep, node_atom);
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
