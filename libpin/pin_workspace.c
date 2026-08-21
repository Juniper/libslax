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
#include <libpsu/psulog.h>
#include <parrotdb/pacommon.h>
#include <parrotdb/paconfig.h>
#include <parrotdb/pammap.h>
#include <parrotdb/pafixed.h>
#include <parrotdb/paarb.h>
#include <parrotdb/paistr.h>
#include <parrotdb/papat.h>
#include <parrotdb/pabitmap.h>
#include <libpin/pin_common.h>
#include <libpin/pin_source.h>
#include <libpin/pin_rules.h>
#include <libpin/pin_tree.h>
#include <libpin/pin_workspace.h>
#include <libpin/pin_nodeset.h>
#include <libpin/pin_parse.h>

pin_workspace_t *
pin_workspace_open (pa_mmap_t *pmp, const char *name)
{
    pa_istr_t *names = NULL;
    pa_pat_t *names_index = NULL;
    pa_fixed_t *ns_map = NULL;
    pa_pat_t *ns_map_index = NULL;
    pa_istr_t *pip = NULL;
    pa_arb_t *pap = NULL;
    pa_pat_t *ppp = NULL;
    pin_node_t *nodep = NULL;
    pa_fixed_t *nodes = NULL;
    pin_workspace_t *workp = NULL;
    char namebuf[PA_MMAP_HEADER_NAME_LEN];
    pa_fixed_t *nodeset_chunks = NULL, *nodeset_info = NULL;

    /* Holds the names of our elements, attributes, etc */
    pin_mk_name(namebuf, name, "names");
    pin_namepool_open(pmp, namebuf, &names, &names_index);
    if (names == NULL)
	goto fail;

    pin_mk_name(namebuf, name, "namespaces");
    pin_ns_open(pmp, namebuf, &ns_map, &ns_map_index);
    if (ns_map == NULL)
	goto fail;

    nodes = pa_fixed_open(pmp, pin_mk_name(namebuf, name, "nodes"), PIN_SHIFT,
			 sizeof(*nodep), PIN_MAX_ATOMS);
    if (nodes == NULL)
	goto fail;

    pap = pa_arb_open(pmp, pin_mk_name(namebuf, name, "data"));
    if (pap == NULL)
	goto fail;

    nodeset_chunks = pa_fixed_open(pmp,
			pin_mk_name(namebuf, name, "nodeset-chunks"), PIN_SHIFT,
			PIN_NODESET_CHUNK_SIZE, PIN_MAX_ATOMS);
    if (nodeset_chunks == NULL)
	goto fail;

    /* Ensure that our freshly allocated data is zeroed */
    pa_fixed_set_flags(nodeset_chunks, PFF_INIT_ZERO);

    nodeset_info = pa_fixed_open(pmp,
			pin_mk_name(namebuf, name, "nodeset-info"), PIN_SHIFT,
			sizeof(pin_nodeset_info_t), PIN_MAX_ATOMS);
    if (nodeset_info == NULL)
	goto fail;

    /* Ensure that our freshly allocated data is zeroed */
    pa_fixed_set_flags(nodeset_info, PFF_INIT_ZERO);

    workp = calloc(1, sizeof(*workp));
    if (workp == NULL)
	goto fail;

    workp->pw_mmap = pmp;
    workp->pw_nodes = nodes;
    workp->pw_names = names;
    workp->pw_names_index = names_index;
    workp->pw_ns_map = ns_map;
    workp->pw_ns_map_index = ns_map_index;
    workp->pw_textpool = pap;
    workp->pw_nodeset_chunks = nodeset_chunks;
    workp->pw_nodeset_info = nodeset_info;

    return workp;

 fail:
    if (nodeset_chunks != NULL)
	pa_fixed_close(nodeset_chunks);
    if (nodeset_info != NULL)
	pa_fixed_close(nodeset_info);
    if (pap != NULL)
	pa_arb_close(pap);
    if (pip != NULL)
	pa_istr_close(pip);
    if (ppp != NULL)
	pa_pat_close(ppp);
    if (nodes != NULL)
	pa_fixed_close(nodes);
    if (names != NULL)
	pa_istr_close(names);
    if (names_index != NULL)
	pa_pat_close(names_index);
    if (ns_map != NULL)
	pa_fixed_close(ns_map);
    if (ns_map_index != NULL)
	pa_pat_close(ns_map_index);

    return NULL;
}

/* Key function for the namepool patricia tree (istr-backed) */
static const psu_byte_t *
pin_namepool_key_func (pa_pat_t *pp, pa_pat_data_atom_t datom)
{
    return (const psu_byte_t *)
	pa_istr_atom_string(pp->pp_data,
			    pa_istr_atom(pa_pat_data_atom_of(datom)));
}

/* Key function for the namespace-map patricia tree (fixed-backed) */
static const uint8_t *
pin_ns_key_func (pa_pat_t *pp, pa_pat_data_atom_t datom)
{
    return pa_fixed_atom_addr(pp->pp_data,
			      pa_fixed_atom(pa_pat_data_atom_of(datom)));
}

void
pin_namepool_open (pa_mmap_t *pmap, const char *basename,
		  pa_istr_t **namesp, pa_pat_t **names_indexp)
{
    char namebuf[PA_MMAP_HEADER_NAME_LEN];
    pa_istr_t *pip = NULL;
    pa_pat_t *ppp = NULL;

    /* The name pool holds the names of our elements, attributes, etc */
    pip = pa_istr_open(pmap, pin_mk_name(namebuf, basename, "data"),
		       PIN_SHIFT, PIN_ISTR_SHIFT, PIN_MAX_ATOMS);
    if (pip == NULL)
	return;

    ppp = pa_pat_open(pmap, pin_mk_name(namebuf, basename, "index"),
		      pip, pin_namepool_key_func,
		      PA_PAT_MAXKEY, PIN_SHIFT, PIN_MAX_ATOMS);
    if (ppp == NULL) {
	pa_istr_close(pip);
	return;
    }

    *namesp = pip;
    *names_indexp = ppp;
}

void
pin_ns_open (pa_mmap_t *pmap, const char *basename,
	    pa_fixed_t **nsp, pa_pat_t **ns_indexp)
{
    char namebuf[PA_MMAP_HEADER_NAME_LEN];
    pa_fixed_t *pfp = NULL;
    pa_pat_t *ppp = NULL;

    /* The ns pool holds the names of our elements, attributes, etc */
    pfp = pa_fixed_open(pmap, pin_mk_name(namebuf, basename, "data"),
			PIN_SHIFT, sizeof(pin_ns_map_t), PIN_MAX_ATOMS);
    if (pfp == NULL)
	return;

    ppp = pa_pat_open(pmap, pin_mk_name(namebuf, basename, "index"),
		      pfp, pin_ns_key_func,
		      PA_PAT_MAXKEY, PIN_SHIFT, PIN_MAX_ATOMS);
    if (ppp == NULL) {
	pa_fixed_close(pfp);
	return;
    }

    *nsp = pfp;
    *ns_indexp = ppp;
}

/*
 * Return a name atom for a string in the name pool.  Our patricia tree
 * has data atoms that are istr atoms, which we turn into name atoms.
 * It's some ugly "atom smashing" that keeps us type safe.  Think of it
 * as lead shielding.
 */
pin_name_id_t
pin_namepool_atom (pin_workspace_t *pwp, const char *data, pin_boolean_t createp)
{
    uint16_t len = strlen(data) + 1;
    pa_pat_t *ppp = pwp->pw_names_index;

    pa_pat_data_atom_t datom = pa_pat_get_atom(ppp, len, data);
    if (pa_pat_data_is_null(datom) && createp) {
	/* Allocate the name from our pool and add it to the tree */
	pa_istr_atom_t iatom = pa_istr_string(pwp->pw_names, data);
	datom = pa_pat_data_atom(pa_istr_atom_of(iatom));
	if (pa_istr_is_null(iatom))
	    pa_warning(0, "namepool create key failed for key '%s'", data);
	else if (!pa_pat_add(ppp, datom, len))
	    pa_warning(0, "duplicate key: %s", data);
    }

    return pin_name_id(pa_pat_data_is_null(datom) ? PA_NULL_ATOM
					           : pa_pat_data_atom_of(datom));
}

pa_arb_atom_t
pin_get_attrib (pin_workspace_t *pwp, pin_node_t *nodep, pin_name_id_t name_id)
{
    pin_node_id_t node_id;
    pin_depth_t depth = nodep->pn_depth;

    if (!(nodep->pn_flags & PNF_ATTRIBS_PRESENT))
	return pa_arb_atom(PA_NULL_ATOM);

#if 0 /* XXX */
    if (!(nodep->pn_flags & PNF_ATTRIBS_EXTRACTED))
	pin_node_attrib_extract(pwp, nodep);
#endif

    for (node_id = pin_node_child(nodep); !pin_node_id_is_null(node_id);
	 node_id = nodep->pn_next) {
	nodep = pin_node_addr(pwp, node_id);
	if (nodep == NULL)	/* Should not occur */
	    break;

	if (nodep->pn_depth <= depth)
	    break;		/* Found end of children */

	if (nodep->pn_type != PIN_TYPE_ATTRIB)
	    continue;

	if (pin_name_id_equal(nodep->pn_name, name_id))
	    return pin_node_text(nodep);
    }

    return pa_arb_atom(PA_NULL_ATOM);
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
 * original input.  The cost is an extra lookup in pw_ns_map to see
 * the underlaying atom numbers of the URI strings (which reside in
 * the name pool).  Another fine engineering trade off that's such to
 * bite me in the lower cheeks one day.
 */
pin_ns_map_id_t
pin_ns_find (pin_workspace_t *pwp, const char *prefix, const char *uri,
	    pin_boolean_t createp)
{
    pin_name_id_t prefix_atom = pin_name_id_null_atom();
    pin_name_id_t uri_atom = pin_name_id_null_atom();

    if (prefix != NULL && *prefix != '\0') {
	prefix_atom = pin_namepool_atom(pwp, prefix, TRUE);
	if (pin_name_id_is_null(prefix_atom))
	    return pin_ns_map_id_null_atom();
    }

    if (uri != NULL && *uri != '\0') {
	uri_atom = pin_namepool_atom(pwp, uri, TRUE);
	if (pin_name_id_is_null(uri_atom))
	    return pin_ns_map_id_null_atom();
    }

    pa_pat_t *ppp = pwp->pw_ns_map_index;
    pin_ns_map_t ns = { prefix_atom, uri_atom };
    pa_pat_data_atom_t datom = pa_pat_get_atom(ppp, sizeof(ns), &ns);
    if (pa_pat_data_is_null(datom) && createp) {
	pin_ns_map_id_t ns_id;
	pin_ns_map_t *nsp = pin_ns_map_alloc(pwp, &ns_id);
	if (nsp == NULL) {
	    pa_warning(0, "namespace create key failed for '%s%s%s'",
		       prefix ?: "", prefix ? ":" : "", uri ?: "");
	    return pin_ns_map_id_null_atom();
	}

	*nsp = ns;		/* Initialize newly allocated ns_map entry */

	datom = pa_pat_data_atom(pa_fixed_atom_of(pin_ns_map_id_atom_of(ns_id)));
	if (!pa_pat_add(ppp, datom, sizeof(ns))) {
	    pin_ns_map_free(pwp, ns_id);

	    pa_warning(0, "duplicate key failure for namespace '%s%s%s'",
		       prefix ?: "", prefix ? ":" : "", uri ?: "");
	    return pin_ns_map_id_null_atom();
	}

	return ns_id;
    }

    return pa_pat_data_is_null(datom) ? pin_ns_map_id_null_atom()
				      : pin_ns_map_id(pa_pat_data_atom_of(datom));
}

void
pin_namepool_close (pa_istr_t *names, pa_pat_t *names_index)
{
    pa_pat_close(names_index);
    pa_istr_close(names);
}

void
pin_ns_close (pa_fixed_t *ns_map, pa_pat_t *ns_map_index)
{
    pa_pat_close(ns_map_index);
    pa_fixed_close(ns_map);
}

void
pin_workspace_close (pin_workspace_t *workp)
{
    if (workp == NULL)
	return;

    pa_fixed_close(workp->pw_nodeset_info);
    pa_fixed_close(workp->pw_nodeset_chunks);
    pa_arb_close(workp->pw_textpool);
    pin_ns_close(workp->pw_ns_map, workp->pw_ns_map_index);
    pin_namepool_close(workp->pw_names, workp->pw_names_index);
    pa_fixed_close(workp->pw_nodes);

    free(workp);
}
