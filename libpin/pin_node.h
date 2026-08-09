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
 * xinode.h: all the definition needed for pin_node_t, which is the
 * representation of a node in the tree.
 */

#ifndef LIBXI_XINODE_H
#define LIBXI_XINODE_H

#include <parrotdb/pacommon.h>
#include <parrotdb/pafixed.h>
#include <parrotdb/paarb.h>
#include <libpin/pin_common.h>

/* Generated strongly-typed atoms for nodes and namespace map entries */
#include "gen/pin_node_id_gen.h"
#include "gen/pin_ns_map_id_gen.h"

/*
 * Since we're using these as bitfields, we're unable to use
 * atom wrappers.  This will require extra care.
 */
typedef pa_atom_t pin_name_id_raw_t;	/* Element name identifier */
typedef pa_atom_t pin_ns_map_id_raw_t;	/* Namespace identifier (raw) */

/* Wrapper for our "name" atom */
PA_ATOM_TYPE(pin_name_atom_t, pin_name_atom_s, xna_atom,
	     pin_name_is_null, pin_name_atom, pin_name_atom_of,
	     pin_name_null_atom);

/*
 * Wrapper for our "namespace" mapping atom, which is really a "prefix
 * mapping", since we use this as an index into prefix->namespace mappings.
 */
PA_ATOM_TYPE(pin_ns_map_atom_t, pin_ns_map_atom_s, xnsm_atom,
	     pin_ns_map_is_null, pin_ns_map_atom, pin_ns_map_atom_of,
	     pin_ns_map_null_atom);

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
 *
 * xn_contents is overloaded based on xn_type:
 *   ELT/ROOT  -> pin_node_id_t  (first child atom)
 *   TEXT/UNESC/ATTRIB/ATSTR -> pa_arb_atom_t (text atom)
 *   NS        -> pin_ns_map_id_t (namespace map atom)
 *   NSPREF    -> pa_atom_t (prefix atom, temporary during attribute parsing)
 * Use the typed accessors below rather than xn_contents directly.
 */
typedef struct pin_node_s {
    pin_node_type_t xn_type;	/* Type of this node */
    pin_depth_t xn_depth;	/* Depth of this node (origin XI_DEPTH_MIN) */
    pin_node_flags_t xn_flags;	/* Flags (XNF_*) */
    pin_ns_map_id_raw_t xn_ns_map;	/* Namespace map for this node (raw) */
    pin_name_id_raw_t xn_name;	/* Name of this node (in name db) */
    pin_node_id_t xn_parent;	/* Parent node (NULL if root) */
    pin_node_id_t xn_next;	/* Next node (or parent if last) */
    pa_atom_t xn_contents;	/* First child, text, or ns_map atom (overloaded) */
} pin_node_t;

/*
 * Each node has a prefix mapping that tells us which namespace it's
 * in.  We want to make this simple and reusable, but since prefixes
 * can be remapped within any hierarchy, it's only reusable in the
 * window where that mapping isn't changed.  But this makes finding
 * the prefix and url a simple lookup.  This means that it two nodes
 * have the same mapping (pin_ns_id_t) then they are in the same
 * namespace, but if they are different, then those two mappings'
 * xnm_uri fields must be compared to see if they have the same atom
 * number.  Since they are in a name-pool, "There can be only one!"
 * applies, so comparing the url atom number is sufficient.
 */
typedef struct pin_ns_map_s {
    pa_atom_t xnm_prefix;       /* Atom of prefix string (in namepool) */
    pa_atom_t xnm_uri;          /* Atom of URL string (in namepool) */
} pin_ns_map_t;

/* Type-checked read accessors for xn_contents */

static inline pin_node_id_t
pin_node_child (pin_node_t *nodep)
{
    if (nodep->xn_type == XI_TYPE_ELT || nodep->xn_type == XI_TYPE_ROOT)
	return pin_node_id(nodep->xn_contents);
    return pin_node_id_null_atom();
}

static inline pa_arb_atom_t
pin_node_text (pin_node_t *nodep)
{
    if (nodep->xn_type == XI_TYPE_TEXT || nodep->xn_type == XI_TYPE_UNESC
	    || nodep->xn_type == XI_TYPE_ATTRIB || nodep->xn_type == XI_TYPE_ATSTR)
	return pa_arb_atom(nodep->xn_contents);
    return pa_arb_atom(PA_NULL_ATOM);
}

static inline pin_ns_map_id_t
pin_node_ns_contents (pin_node_t *nodep)
{
    if (nodep->xn_type == XI_TYPE_NS)
	return pin_ns_map_id(nodep->xn_contents);
    return pin_ns_map_id_null_atom();
}

/* Type-checked write accessors for xn_contents */

static inline void
pin_node_set_child (pin_node_t *nodep, pin_node_id_t id)
{
    nodep->xn_contents = pa_fixed_atom_of(pin_node_id_atom_of(id));
}

static inline void
pin_node_set_text (pin_node_t *nodep, pa_arb_atom_t atom)
{
    nodep->xn_contents = pa_arb_atom_of(atom);
}

static inline void
pin_node_set_ns_contents (pin_node_t *nodep, pin_ns_map_id_t id)
{
    nodep->xn_contents = pa_fixed_atom_of(pin_ns_map_id_atom_of(id));
}

/* Write accessor for xn_ns_map */
static inline void
pin_node_set_ns_map (pin_node_t *nodep, pin_ns_map_id_t id)
{
    nodep->xn_ns_map = pa_fixed_atom_of(pin_ns_map_id_atom_of(id));
}

/* Equality comparison for pin_node_id_t */
static inline psu_boolean_t
pin_node_id_equal (pin_node_id_t a, pin_node_id_t b)
{
    return pa_fixed_atom_of(pin_node_id_atom_of(a))
	    == pa_fixed_atom_of(pin_node_id_atom_of(b));
}

#endif /* LIBXI_XINODE_H */
