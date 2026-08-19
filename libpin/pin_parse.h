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

#ifndef LIBSLAX_PIN_PARSE_H
#define LIBSLAX_PIN_PARSE_H

typedef unsigned pin_parse_flags_t;

/*
 * Forward declaration so xo_filter_t * can appear in pin_parse_t
 * without requiring callers to pull in the full xo_filter.h header.
 */
struct xo_filter_s;
typedef struct xo_filter_s xo_filter_t;

/*
 * XSLT execution context.  Tracks the current mode and, in future,
 * variable bindings, the current input node, and a frame stack for
 * recursive xsl:apply-templates invocations.
 */
typedef struct pin_context_s {
    const char *pctx_mode;	/* Current XSLT mode (NULL = default mode) */
} pin_context_t;

/*
 * The state of the parser, meant to be both a handle to parsing
 * functionality as well as a means of restarting parsing.
 */
typedef struct pin_parse_s {
    pin_parse_flags_t pp_flags;  /* Flags for this parser */
    pin_source_t *pp_srcp;	/* Source of incoming tokens */
    pin_rulebook_t *pp_rulebook;	/* Current set of rules */
    pin_rule_t pp_default_rule;	/* Default rule for parsing */
    pin_insert_t *pp_insert;	/* Insertion point */
    xo_filter_t *pp_filter;	/* Optional XPath filter (pin_filter_create) */
    pin_context_t pp_context;	/* Execution context (mode, etc.) */
} pin_parse_t;

/* Flags for pp_flags: */
#define PIN_PF_DEBUG		(1<<0) /* Make some debug output */

#define PIN_STATE_EOL		0 /* Indicates end-of-list/invalid state */
#define PIN_STATE_INITIAL	1 /* Initial parser state */

typedef int (*pin_parse_emit_fn)(pin_parse_t *, pin_node_type_t,
				pin_node_id_t node_atom, pin_node_t *,
				const char *, void *);

static inline pin_parse_flags_t
pin_parse_flags_get (pin_parse_t *pxp)
{
    return pxp->pp_flags;
}

static inline int
pin_parse_flags_isset (pin_parse_t *pxp, pin_parse_flags_t flags)
{
    return (pxp->pp_flags & flags) ? 1 : 0;
}

static inline void
pin_parse_flags_set (pin_parse_t *pxp, pin_parse_flags_t flags)
{
    pxp->pp_flags |= flags;
}

static inline void
pin_parse_flags_clear (pin_parse_t *pxp, pin_parse_flags_t flags)
{
    pxp->pp_flags &= ~flags;
}

static inline pin_rstate_t *
pin_parse_stack_state (pin_parse_t *parsep)
{
    pin_insert_t *pip = parsep->pp_insert;
    return pip->pin_stack[pip->pin_depth].ps_statep;
}

pin_parse_t *
pin_parse_open (pa_mmap_t *pmap, pin_workspace_t *pwp, const char *name,
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

/*
 * Attach an XPath filter to the parser.  When set, the filter FSM is
 * advanced on every element open/close; elements whose subtree cannot
 * match any pattern are discarded without consulting the rulebook.
 * Pass NULL to detach.  The caller retains ownership of xfp.
 */
void
pin_parse_set_filter (pin_parse_t *parsep, xo_filter_t *xfp);

/*
 * Set the current XSLT execution mode on the parser context.
 * Dispatch will only fire rules whose compiled mode matches 'mode'.
 * Pass NULL to select default-mode (no mode= attribute) templates.
 */
void
pin_parse_set_mode (pin_parse_t *parsep, const char *mode);

static inline pin_workspace_t *
pin_parse_workspace (pin_parse_t *parsep)
{
    return parsep->pp_insert->pin_tree->pt_workspace;
}

pa_atom_t
pin_parse_namepool_atom (pin_parse_t *parsep, const char *name);

const char *
pin_parse_namepool_string (pin_parse_t *parsep, pa_atom_t atom);

void
pin_parse_set_rulebook (pin_parse_t *parsep, pin_rulebook_t *rulebook);

void
pin_node_dump (pin_workspace_t *pwp, pin_node_type_t op,
	      pin_node_t *nodep, pin_node_id_t atom);

#endif /* LIBSLAX_PIN_PARSE_H */
