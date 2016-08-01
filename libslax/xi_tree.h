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
    xi_node_flags_t xn_flags;	/* Flags (XNF_*) */
    xi_ns_id_t xn_ns_map;	/* Namespace map for this node (in ns_map) */
    xi_name_id_t xn_name;	/* Name of this node (in name db) */
    xi_node_id_t xn_next;	/* Next node (or parent if last) */
    xi_node_id_t xn_contents;	/* Child node or data (in this tree or data) */
} xi_node_t;

#define XI_DEPTH_MIN	1	/* Depth of top of tree (origin 1) */
#define XI_DEPTH_MAX	254	/* Max depth of tree */

/* Flags for xn_flags */
#define XNF_ATTRIBS_PRESENT	(1<<0) /* Attributes available */
#define XNF_ATTRIBS_EXTRACTED	(1<<1) /* Attributes aleady extracted */

/*
 * Each tree (document or RTF) is represented as a tree.  The
 * xi_tree_info_t is the information that needs to persist.
 */
typedef struct xi_tree_info_s {
    xi_node_id_t xti_root;	/* Number of the root node */
    xi_depth_t xti_max_depth;	/* Max depth of the tree */
} xi_tree_info_t;

/*
 * The in-memory representation of a tree
 */
typedef struct xi_tree_s {
    xi_tree_info_t *xt_infop;	/* Base information */
    xi_workspace_t *xt_workspace; /* Our workspace */
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
    xi_rstate_t *xs_statep;	/* Current parser state */
    pa_atom_t xs_old_name;	/* Old (original) name atom; for use-tag="xxx" */
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

void
xi_node_dump (xi_workspace_t *xwp, xi_node_type_t op,
	      xi_node_t *nodep, pa_atom_t atom);

#endif /* LIBSLAX_XI_TREE_H */
