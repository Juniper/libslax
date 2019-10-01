/*
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

#define XI_MAX_ATOMS	(1<<26)	/* Max number of nodes in a document */
#define XI_SHIFT	12	/* Bit shift for packed array paging */

#define XI_ISTR_SHIFT	2	/* Bit shift for immutable string storage */

/*
 * Each tree (document or RTF) is represented as a tree.  The
 * xi_tree_info_t is the information that needs to persist in
 * the database.
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
    pa_atom_t xs_old_name;	/* Old (original) name atom; for use-tag="x" */
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

#endif /* LIBSLAX_XI_TREE_H */
