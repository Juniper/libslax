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

#ifndef LIBSLAX_XI_PARSE_H
#define LIBSLAX_XI_PARSE_H

typedef uint8_t xi_action_t;

/*
 * We use bitmasks to indicate which elements are affected by rules.
 * There is a special case where xb_len == 0 implies a match.
 */
typedef struct xi_bitmask_s {
    xi_name_id_t xb_base;	/* Base bit number */
    xi_name_id_t xb_len;	/* Length of bitmask */
    uint8_t *xb_mask;		/* Bitmask of element ids */
} xi_bitmask_t;

/*
 * A rule defines a behavior for an incoming token.  A token can be
 * copied, saved, or discarded.  Or a function callback can triggered.
 */
typedef struct xi_rule_s {
    struct xi_rule_s *xr_next;	/* Linked list of rules */
    xi_bitmask_t xr_bitmask;	/* Elements affected by this rule */
    xi_action_t xr_action;	/* What to do when the rule matches */
} xi_rule_t;

/* Values for xi_action */
#define XIA_NONE	0
#define XIA_DISCARD	1	/* Discard with all due haste */
#define XIA_SAVE	2	/* Add to the insertion point */
#define XIA_EMIT	3	/* Emit as output */
#define XIA_RETURN	4	/* Force return from xi_parse() */

/*
 * A rule set is an optimized set of rules
 */
typedef struct xi_ruleset_s {
    xi_rule_t *xrs_base;	/* Base set of rules */
    xi_action_t xrs_default;	/* Default action */
    xi_action_t xrs_last;	/* Last action taken */
} xi_ruleset_t;

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
    xi_node_id_t xn_next;	/* Next node (in this tree) */
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
 * The in-memory representation of a tree
 */
typedef struct xi_tree_s {
    pa_mmap_t *xt_mmap;	/* Base memory information */
    xi_namepool_t *xt_namepool;	/* Namepool used by this tree */
    pa_fixed_t *xt_tree;	/* Tree node */
    xi_tree_info_t *xt_infop;	/* Base information */
} xi_tree_t;

#define xt_root xt_infop->xti_root
#define xt_max_depth xt_infop->xti_max_depth

/*
 * An insertion point is all the information we need to add a node to
 * some sort of output tree.
 */
typedef struct xi_insertion_s {
    xi_tree_t *xi_tree;		/* Tree we are inserted into */
    xi_depth_t xi_depth;	/* Current depth in hierarchy */
    unsigned xi_relation;	/* How to handle the next insertion */
    xi_node_id_t xi_stack[XI_DEPTH_MAX]; /* Insertion point (via xi_depth) */
} xi_insertion_t;

/* Values for xi_relation */
#define XIR_SIBLING	1	/* Insert as sibling */
#define XIR_CHILD	2	/* Insert as child */

/*
 * The state of the parser, meant to be both a handle to parsing
 * functionality as well as a means of restarting parsing.
 */
typedef struct xi_parse_s {
    xi_source_t *xp_srcp;	/* Source of incoming tokens */
    xi_ruleset_t *xp_ruleset;	/* Current set of rules */
    xi_insertion_t *xp_insertion; /* Insertion point */
} xi_parse_t;

xi_parse_t *
xi_parse_open (pa_mmap_t *pmap, const char *name,
	       const char *filename, xi_source_flags_t flags);

void
xi_parse_destroy (xi_parse_t *parsep);

int
xi_parse (xi_parse_t *parsep);

#endif /* LIBSLAX_XI_PARSE_H */
