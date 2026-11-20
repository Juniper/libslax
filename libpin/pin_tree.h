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

#ifndef LIBSLAX_PIN_TREE_H
#define LIBSLAX_PIN_TREE_H

#include <libpin/pin_common.h>
#include <libpin/pin_node.h>
#include <libpin/pin_body.h>
#include <libpin/pin_exec.h>

#define PIN_MAX_ATOMS	(1<<26)	/* Max number of nodes in a document */
#define PIN_SHIFT	12	/* Bit shift for packed array paging */

#define PIN_ISTR_SHIFT	2	/* Bit shift for immutable string storage */

/*
 * Each tree (document or RTF) is represented as a tree.  The
 * pin_tree_info_t is the information that needs to persist in
 * the database.
 */
typedef struct pin_tree_info_s {
    pin_node_id_t pti_root;	/* Number of the root node */
    pin_depth_t pti_max_depth;	/* Max depth of the tree */
} pin_tree_info_t;

/*
 * The in-memory representation of a tree
 */
typedef struct pin_tree_s {
    pin_tree_info_t *pt_infop;	/* Base information */
    pin_workspace_t *pt_workspace; /* Our workspace */
} pin_tree_t;

#define pt_root pt_infop->pti_root
#define pt_max_depth pt_infop->pti_max_depth

/*
 * The insertion stack
 */
typedef struct pin_istack_s {
    pin_node_id_t ps_atom;	/* Our node (atom) */
    pin_node_t *ps_node;		/* Our node (pointer) */
    pin_node_id_t ps_last_atom;	/* Last child we appended (atom) */
    pin_node_t *ps_last_node;	/* Last child we appended (pointer) */
    pin_action_type_t ps_action;	/* Action being taken (PIA_*) */
    pin_rstate_t *ps_statep;	/* Current parser state */
    pin_name_id_t ps_old_name;	/* Old (original) name atom; for use-tag="x" */
    pin_op_id_t ps_close_ops;	/* Op sequence to run when this element closes */
    int ps_context_retain;	/* Non-zero: children are kept as op context */
    unsigned ps_emit;		/* Output state of this frame (PSE_*) */
    pin_depth_t ps_idepth;	/* Output frame: input depth when opened */
    pin_node_id_t ps_prev_atom;	/* Parent's last child before us (atom) */
    pin_node_t *ps_prev_node;	/* Parent's last child before us (pointer) */
    pin_boolean_t ps_hold;	/* Read after it closes; freed as a whole */
    pin_boolean_t ps_keep_below; /* Something below us stays in the store */
    pin_boolean_t ps_scratch;	/* Not part of the result, even with no output */
} pin_istack_t;

/* Flags for ps_emit */
#define PSE_CONTENT	(1<<0)	/* Content of this frame goes to the output */
#define PSE_OPEN	(1<<1)	/* The output has an open element for us */

/*
 * An insertion point is all the information we need to add a node to
 * some sort of output tree.
 *
 * pin_stack holds one frame per open input element, so pin_depth is
 * the input depth.  pin_ostack holds the open output elements, which
 * have no node in the store.  The two nest strictly, so the innermost
 * open element is the top output frame if it was opened at the
 * current input depth, and the top input frame otherwise.
 */
typedef struct pin_insert_s {
    pin_tree_t *pin_tree;		/* Tree we are inserted into */
    pin_depth_t pin_depth;	/* Current input depth */
    pin_depth_t pin_odepth;	/* Current output depth */
    pin_depth_t pin_maxdepth;	/* Maximum depth seen */
    unsigned pin_relation;	/* How to handle the next insertion */
    pin_node_id_t pin_prev_atom; /* Last child before the newest (atom) */
    pin_node_t *pin_prev_node;	/* Last child before the newest (pointer) */
    uint32_t pin_live;		/* Nodes now in the store */
    uint32_t pin_live_max;	/* Most nodes seen in the store at once */
    pin_istack_t pin_stack[PIN_DEPTH_MAX]; /* Input elements */
    pin_istack_t pin_ostack[PIN_DEPTH_MAX]; /* Output elements */
    pin_body_exec_t pin_body;	/* Body FSM execution state */
} pin_insert_t;

/* Values for pin_relation */
#define PIR_SIBLING	1	/* Insert as sibling */
#define PIR_CHILD	2	/* Insert as child */

/*
 * True if the innermost open element is an output element.
 */
static inline pin_boolean_t
pin_insert_top_is_output (pin_insert_t *pip)
{
    return (pip->pin_odepth > 0
	    && pip->pin_ostack[pip->pin_odepth].ps_idepth == pip->pin_depth);
}

/*
 * The frame of the innermost open element, input or output.  Content
 * (text, attributes, child elements) is placed relative to it.
 */
static inline pin_istack_t *
pin_insert_top_frame (pin_insert_t *pip)
{
    if (pin_insert_top_is_output(pip))
	return &pip->pin_ostack[pip->pin_odepth];

    return &pip->pin_stack[pip->pin_depth];
}

/*
 * The frame of the innermost open input element.  New store nodes go
 * under it.
 */
static inline pin_istack_t *
pin_insert_input_frame (pin_insert_t *pip)
{
    return &pip->pin_stack[pip->pin_depth];
}

/*
 * The name of the innermost open element: its node's name, or, for an
 * output element, the name it was opened with.  A discarded input
 * element has neither.
 */
static inline pin_name_id_t
pin_insert_top_name (pin_insert_t *pip)
{
    if (pin_insert_top_is_output(pip))
	return pip->pin_ostack[pip->pin_odepth].ps_old_name;

    pin_istack_t *psp = &pip->pin_stack[pip->pin_depth];
    if (psp->ps_node)
	return psp->ps_node->pn_name;

    return pin_name_id_null_atom();
}

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
pin_tree_append_child (pin_workspace_t *pwp,
		      pin_node_id_t parent_atom, pin_node_id_t last_hint,
		      pin_node_id_t new_node_atom);

#endif /* LIBSLAX_PIN_TREE_H */
