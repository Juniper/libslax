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

#include <libpin/pin_common.h>
#include <libpin/pin_node.h>

#define XI_MAX_ATOMS	(1<<26)	/* Max number of nodes in a document */
#define XI_SHIFT	12	/* Bit shift for packed array paging */

#define XI_ISTR_SHIFT	2	/* Bit shift for immutable string storage */

/*
 * Each tree (document or RTF) is represented as a tree.  The
 * pin_tree_info_t is the information that needs to persist in
 * the database.
 */
typedef struct pin_tree_info_s {
    pin_node_id_t xti_root;	/* Number of the root node */
    pin_depth_t xti_max_depth;	/* Max depth of the tree */
} pin_tree_info_t;

/*
 * The in-memory representation of a tree
 */
typedef struct pin_tree_s {
    pin_tree_info_t *xt_infop;	/* Base information */
    pin_workspace_t *xt_workspace; /* Our workspace */
} pin_tree_t;

#define xt_root xt_infop->xti_root
#define xt_max_depth xt_infop->xti_max_depth

/*
 * The insertion stack
 */
typedef struct pin_istack_s {
    pin_node_id_t xs_atom;	/* Our node (atom) */
    pin_node_t *xs_node;		/* Our node (pointer) */
    pin_node_id_t xs_last_atom;	/* Last child we appended (atom) */
    pin_node_t *xs_last_node;	/* Last child we appended (pointer) */
    pin_action_type_t xs_action;	/* Action being taken (XIA_*) */
    pin_rstate_t *xs_statep;	/* Current parser state */
    pa_atom_t xs_old_name;	/* Old (original) name atom; for use-tag="x" */
} pin_istack_t;

/*
 * An insertion point is all the information we need to add a node to
 * some sort of output tree.
 */
typedef struct pin_insert_s {
    pin_tree_t *pin_tree;		/* Tree we are inserted into */
    pin_depth_t pin_depth;	/* Current depth in hierarchy */
    pin_depth_t pin_maxdepth;	/* Maximum depth seen */
    unsigned pin_relation;	/* How to handle the next insertion */
    pin_istack_t pin_stack[XI_DEPTH_MAX]; /* Insertion points */
} pin_insert_t;

/* Values for pin_relation */
#define XIR_SIBLING	1	/* Insert as sibling */
#define XIR_CHILD	2	/* Insert as child */

static inline const char *
pin_mk_name (char *namebuf, const char *name, const char *ext)
{
    return pa_config_name(namebuf, PA_MMAP_HEADER_NAME_LEN, name, ext);
}

/*
 * Append new_node_atom as the last child of parent_atom.
 *
 * last_hint should be the return value of the previous pin_tree_append_child
 * call on this same parent, giving O(1) append for sequential builds.
 * Pass pin_node_id_null_atom() when the hint is unavailable; the sibling
 * chain will be scanned to locate the last child (O(n_children)).
 *
 * Returns new_node_atom so callers can chain: pass it as last_hint on the
 * next append to this parent.  Returns pin_node_id_null_atom() on error.
 */
pin_node_id_t
pin_tree_append_child (pin_workspace_t *xwp,
		      pin_node_id_t parent_atom, pin_node_id_t last_hint,
		      pin_node_id_t new_node_atom);

#endif /* LIBSLAX_XI_TREE_H */
