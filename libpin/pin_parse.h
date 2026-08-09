/*
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

typedef unsigned pin_parse_flags_t;

/*
 * The state of the parser, meant to be both a handle to parsing
 * functionality as well as a means of restarting parsing.
 */
typedef struct pin_parse_s {
    pin_parse_flags_t xp_flags;  /* Flags for this parser */
    pin_source_t *xp_srcp;	/* Source of incoming tokens */
    pin_rulebook_t *xp_rulebook;	/* Current set of rules */
    pin_rule_t xp_default_rule;	/* Default rule for parsing */
    pin_insert_t *xp_insert;	/* Insertion point */
} pin_parse_t;

/* Flags for xp_flags: */
#define XI_PF_DEBUG		(1<<0) /* Make some debug output */

#define XI_STATE_EOL		0 /* Indicates end-of-list/invalid state */
#define XI_STATE_INITIAL	1 /* Initial parser state */

typedef int (*pin_parse_emit_fn)(pin_parse_t *, pin_node_type_t,
				pin_node_id_t node_atom, pin_node_t *,
				const char *, void *);

static inline pin_parse_flags_t
pin_parse_flags_get (pin_parse_t *pxp)
{
    return pxp->xp_flags;
}

static inline int
pin_parse_flags_isset (pin_parse_t *pxp, pin_parse_flags_t flags)
{
    return (pxp->xp_flags & flags) ? 1 : 0;
}

static inline void
pin_parse_flags_set (pin_parse_t *pxp, pin_parse_flags_t flags)
{
    pxp->xp_flags |= flags;
}

static inline void
pin_parse_flags_clear (pin_parse_t *pxp, pin_parse_flags_t flags)
{
    pxp->xp_flags &= ~flags;
}

static inline pin_rstate_t *
pin_parse_stack_state (pin_parse_t *parsep)
{
    pin_insert_t *xip = parsep->xp_insert;
    return xip->pin_stack[xip->pin_depth].xs_statep;
}

pin_parse_t *
pin_parse_open (pa_mmap_t *pmap, pin_workspace_t *xwp, const char *name,
	       const char *filename, pin_source_flags_t flags);

void
pin_parse_destroy (pin_parse_t *parsep);

int
pin_parse (pin_parse_t *parsep);

void
pin_parse_dump (pin_parse_t *parsep);

void
pin_parse_emit (pin_parse_t *parsep, pin_parse_emit_fn func, void *opaque);

void
pin_parse_emit_xml (pin_parse_t *parsep, FILE *out);

void
pin_parse_set_rulebook (pin_parse_t *parsep, pin_rulebook_t *rulebook);

void
pin_parse_set_default_rule (pin_parse_t *parsep, pin_action_type_t type);

static inline pin_workspace_t *
pin_parse_workspace (pin_parse_t *parsep)
{
    return parsep->xp_insert->pin_tree->xt_workspace;
}

pa_atom_t
pin_parse_namepool_atom (pin_parse_t *parsep, const char *name);

const char *
pin_parse_namepool_string (pin_parse_t *parsep, pa_atom_t atom);

void
pin_parse_set_rulebook (pin_parse_t *parsep, pin_rulebook_t *rulebook);

void
pin_node_dump (pin_workspace_t *xwp, pin_node_type_t op,
	      pin_node_t *nodep, pin_node_id_t atom);

#endif /* LIBSLAX_XI_PARSE_H */
