/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer, August 2026
 *
 * pin_value_t: the 64-bit tagged value type used on the op execution
 * stack and in variable bindings.
 *
 * PVT_NODE holds a single live parrotdb node atom.
 * PVT_NODESET holds an index into pin_exec_state_t.pes_nodesets[], which
 * is a heap-allocated array of pin_node_id_t atoms.  Nodesets are not
 * in parrotdb because they are temporary and session-scoped.  The same
 * nodeset table is reused for select results, xsl:variable bindings, and
 * mutable variables.
 */

#ifndef LIBSLAX_PIN_VALUE_H
#define LIBSLAX_PIN_VALUE_H

#include <stdint.h>

/*
 * Discriminator for pin_value_t.pv_type.
 */
typedef uint16_t pin_value_type_t;

#define PVT_NULL     0   /* No value / sentinel */
#define PVT_BOOLEAN  1   /* pv_flags has PVF_TRUE or PVF_FALSE; pv_atom unused */
#define PVT_STRING   2   /* pv_atom is a namepool (pin_name_id_t) atom */
#define PVT_NUMBER   3   /* pv_atom holds integer value or floatpool index */
#define PVT_NODE     4   /* pv_atom is a pin_node_id_t atom (live parrotdb node) */
#define PVT_NODESET  5   /* pv_atom is index into pes_nodesets[] in exec state */

/*
 * Flags for pin_value_t.pv_flags.
 */
typedef uint16_t pin_value_flags_t;

#define PVF_NONE     0
#define PVF_TRUE     (1 << 0)   /* PVT_BOOLEAN is true */
#define PVF_FALSE    (1 << 1)   /* PVT_BOOLEAN is false (explicit) */
#define PVF_OWNED    (1 << 2)   /* pv_atom memory must be freed on pop */

/*
 * The value type.  Fits in one 64-bit register.
 * pv_atom interpretation depends on pv_type (see PVT_* above).
 */
typedef struct pin_value_s {
    pin_value_type_t  pv_type;
    pin_value_flags_t pv_flags;
    uint32_t          pv_atom;
} pin_value_t;

/* Convenience constructors */
static inline pin_value_t
pin_value_null (void)
{
    return (pin_value_t){ PVT_NULL, PVF_NONE, 0 };
}

static inline pin_value_t
pin_value_bool (int truth)
{
    return (pin_value_t){ PVT_BOOLEAN,
			  truth ? PVF_TRUE : PVF_FALSE, 0 };
}

static inline pin_value_t
pin_value_string (uint32_t atom)
{
    return (pin_value_t){ PVT_STRING, PVF_NONE, atom };
}

static inline pin_value_t
pin_value_number (uint32_t n)
{
    return (pin_value_t){ PVT_NUMBER, PVF_NONE, n };
}

static inline pin_value_t
pin_value_node (uint32_t node_atom)
{
    return (pin_value_t){ PVT_NODE, PVF_NONE, node_atom };
}

static inline pin_value_t
pin_value_nodeset (uint32_t ns_idx)
{
    return (pin_value_t){ PVT_NODESET, PVF_NONE, ns_idx };
}

static inline int
pin_value_is_true (pin_value_t v)
{
    if (v.pv_type == PVT_BOOLEAN)
	return (v.pv_flags & PVF_TRUE) != 0;
    if (v.pv_type == PVT_STRING || v.pv_type == PVT_NODESET)
	return v.pv_atom != 0;
    if (v.pv_type == PVT_NUMBER)
	return v.pv_atom != 0;
    return 0;
}

/*
 * One slot in the nodeset table (pes_nodesets[] in pin_exec_state_t).
 * Holds a heap-allocated growable array of node atoms.
 */
typedef struct pin_ns_entry_s {
    uint32_t *pne_nodes;    /* heap-allocated array of pin_node_id pv_atom values */
    uint32_t  pne_count;    /* nodes used */
    uint32_t  pne_cap;      /* allocated capacity */
} pin_ns_entry_t;

/*
 * One variable binding (in pes_vars[] in pin_exec_state_t).
 * The value may be any PVT_*; if PVT_NODESET, pv_atom indexes pes_nodesets[].
 * Mutable variables (xsl:variable / SLAX append/set) update pvb_value in place.
 */
typedef struct pin_var_binding_s {
    uint32_t    pvb_name;   /* namepool pa_atom_t of the variable name */
    pin_value_t pvb_value;  /* current value */
} pin_var_binding_t;

#endif /* LIBSLAX_PIN_VALUE_H */
