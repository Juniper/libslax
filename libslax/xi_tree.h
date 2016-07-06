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
 *
 * Functions for manipulating trees.
 */

#ifndef LIBSLAX_XI_TREE_H
#define LIBSLAX_XI_TREE_H

#define XI_MAX_ATOMS	(1<<26)
#define XI_SHIFT	12
#define XI_ISTR_SHIFT	2

/*
 * A node in an XML hierarchy, made as small as possible.  We use the
 * trick where the last sibling points to the parent, allowing us to
 * work back up the hierarchy, guided by xn_depth.
 *
 * If xn_name == PA_NULL_ATOM, the node is the top node in the
 * hierarchy.  We call this the "top" node, as opposed to the "root"
 * node, which appears as a child of the root node.  This scheme
 * allows the hierarchies with multipe root nodes, which is needed for
 * RTFs.
 */
typedef struct xi_node_s {
    xi_node_type_t xn_type;	/* Type of this node */
    xi_depth_t xn_depth;	/* Depth of this node (origin XI_DEPTH_MIN) */
    xi_ns_id_t xn_ns;		/* Namespace of this node (in namespace db) */
    xi_name_id_t xn_name;	/* Name of this node (in name db) */
    xi_node_id_t xn_next;	/* Next node (or parent if last) */
    xi_node_id_t xn_contents;	/* Child node or data (in this tree or data) */
} xi_node_t;

#define XI_DEPTH_MIN	1	/* Depth of top of tree (origin 1) */
#define XI_DEPTH_MAX	254	/* Max depth of tree */

typedef struct xi_namepool_s {
    pa_istr_t *xnp_names;	/* Array of names (element, attr, etc) */
    pa_pat_t *xnp_names_index;	/* Patricia tree for names */
} xi_namepool_t;

/*
 * Each tree (document or RTF) is represented as a tree.  The
 * xi_tree_info_t is the information that needs to persist.
 */
typedef struct xi_tree_info_s {
    xi_node_id_t xti_root;	/* Number of the root node */
    xi_depth_t xti_max_depth;	/* Max depth of the tree */
} xi_tree_info_t;

/*
 * Each node has a prefix mapping that tells us which namespace it's
 * in.  We want to make this simple and reusable, but since prefixes
 * can be remapped within any hierarchy, it's only reusable in the
 * window where that mapping isn't changed.  But this makes finding
 * the prefix and url a simple lookup.  This means that it two nodes
 * have the same mapping (xn_ns) then they are in the same namespace,
 * but if they are different, then those two mappings' xnm_url fields
 * must be compared to see if they have the same atom number.  Since
 * they are in a name-pool, "There can be only one!" applies, so
 * comparing the url atom number is sufficient.
 */
typedef struct xi_ns_map_s {
    pa_atom_t xnm_prefix;	/* Atom of prefix string (in xt_prefix_names)*/
    pa_atom_t xnm_url;		/* Atom of URL string (in xt_nsurl_names) */
} xi_ns_map_t;

/*
 * The in-memory representation of a tree
 */
typedef struct xi_tree_s {
    pa_mmap_t *xt_mmap;	/* Base memory information */
    xi_tree_info_t *xt_infop;	/* Base information */
    pa_fixed_t *xt_nodes;	/* Pool of nodes (xi_node_t) */
    xi_namepool_t *xt_names; /* Namepool for local part of names */
    pa_fixed_t *xt_prefix_mapping; /* Map from prefixes to URLs (xi_ns_map_t)*/
    pa_arb_t *xt_textpool;	/* Text data values */
} xi_tree_t;

#define xt_root xt_infop->xti_root
#define xt_max_depth xt_infop->xti_max_depth

/*
 * The insertion stack
 */
typedef struct xi_istack_s {
    xi_node_id_t xs_atom;	/* Our node (atom) */
    xi_node_t *xs_node;		/* Our node (pointer) */
    xi_node_id_t xs_last_atom;	/* Last child we appended (atom) */
    xi_node_t *xs_last_node;	/* Last child we appended (pointer) */
    xi_action_type_t xs_action;	/* Action being taken (XIA_*) */
} xi_istack_t;

/*
 * An insertion point is all the information we need to add a node to
 * some sort of output tree.
 */
typedef struct xi_insert_s {
    xi_tree_t *xi_tree;		/* Tree we are inserted into */
    xi_depth_t xi_depth;	/* Current depth in hierarchy */
    xi_depth_t xi_maxdepth;	/* Maximum depth seen */
    unsigned xi_relation;	/* How to handle the next insertion */
    xi_istack_t xi_stack[XI_DEPTH_MAX]; /* Insertion points */
} xi_insert_t;

/* Values for xi_relation */
#define XIR_SIBLING	1	/* Insert as sibling */
#define XIR_CHILD	2	/* Insert as child */

static inline const char *
xi_mk_name (char *namebuf, const char *name, const char *ext)
{
    return pa_config_name(namebuf, PA_MMAP_HEADER_NAME_LEN, name, ext);
}

static inline xi_namepool_t *
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

static inline void
xi_namepool_close (xi_namepool_t *pool)
{
    if (pool) {
	pa_istr_close(pool->xnp_names);
	pa_pat_close(pool->xnp_names_index);
	free(pool);
    }
}

static inline xi_node_t *
xi_node_alloc (xi_tree_t *treep, pa_atom_t *atomp)
{
    if (atomp == NULL)		/* Should not occur */
	return NULL;

    pa_atom_t node_atom = pa_fixed_alloc_atom(treep->xt_nodes);
    xi_node_t *nodep = pa_fixed_atom_addr(treep->xt_nodes, node_atom);

    *atomp = node_atom;
    return nodep;
}

static inline xi_node_t *
xi_node_addr(xi_tree_t *treep, pa_atom_t node_atom)
{
    return pa_fixed_atom_addr(treep->xt_nodes, node_atom);
}

static inline pa_atom_t
xi_namepool_atom (xi_tree_t *treep, const char *data, int createp)
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

static inline const char *
xi_namepool_string (xi_tree_t *treep, pa_atom_t name_atom)
{
    return pa_istr_atom_string(treep->xt_names->xnp_names, name_atom);
}

#endif /* LIBSLAX_XI_TREE_H */
