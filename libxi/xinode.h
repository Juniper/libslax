/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer (phil@) June 2017
 *
 * xinode.h: all the definition needed for xi_node_t, which is the
 * representation of a node in the tree.
 */

#ifndef LIBXI_XINODE_H
#define LIBXI_XINODE_H

/*
 * Since we're using these as bitfields, we're unable to use
 * atom wrappers.  This will require extra care.
 */
typedef pa_atom_t xi_name_id_raw_t;	/* Element name identifier */
typedef pa_atom_t xi_ns_map_id_raw_t;	/* Namespace identifier */

/* Wrapper for our "name" atom */
PA_ATOM_TYPE(xi_name_atom_t, xi_name_atom_s, xna_atom,
	     xi_name_is_null, xi_name_atom, xi_name_atom_of,
	     xi_name_null_atom);

/*
 * Wrapper for our "namespace" mapping atom, which is really a "prefix
 * mapping", since we use this as an index into prefix->namespace mappings.
 */
PA_ATOM_TYPE(xi_ns_map_atom_t, xi_ns_map_atom_s, xnsm_atom,
	     xi_ns_map_is_null, xi_ns_map_atom, xi_ns_map_atom_of,
	     xi_ns_map_null_atom);

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
    xi_ns_id_raw_t xn_ns_map_t;	/* Namespace map for this node (in ns_map) */
    xi_name_id_raw_t xn_name_:20; /* Name of this node (in name db) */
    xi_node_id_t xn_parent;	/* Parent node (NULL if root) */
    xi_node_id_t xn_next;	/* Next node (or parent if last) */
    xi_node_id_t xn_contents;	/* Child node or data (in this tree or data) */
} xi_node_t;

/*
 * Each node has a prefix mapping that tells us which namespace it's
 * in.  We want to make this simple and reusable, but since prefixes
 * can be remapped within any hierarchy, it's only reusable in the
 * window where that mapping isn't changed.  But this makes finding
 * the prefix and url a simple lookup.  This means that it two nodes
 * have the same mapping (xi_ns_id_t) then they are in the same
 * namespace, but if they are different, then those two mappings'
 * xnm_uri fields must be compared to see if they have the same atom
 * number.  Since they are in a name-pool, "There can be only one!"
 * applies, so comparing the url atom number is sufficient.
 */
typedef struct xi_ns_map_s {
    pa_atom_t xnm_prefix;       /* Atom of prefix string (in namepool) */
    pa_atom_t xnm_uri;          /* Atom of URL string (in namepool) */
} xi_ns_map_t;

static inline xi_name_t
xi_node_get_name (xi_node_t *nodep)
{
    
}

#endif /* LIBXI_XINODE_H */
