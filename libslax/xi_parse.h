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

/*
 * The state of the parser, meant to be both a handle to parsing
 * functionality as well as a means of restarting parsing.
 */
typedef struct xi_parse_s {
    xi_source_t *xp_srcp;	/* Source of incoming tokens */
    xi_rulebook_t *xp_rulebook;	/* Current set of rules */
    xi_rule_t xp_default_rule;	/* Default rule for parsing */
    xi_insert_t *xp_insert;	/* Insertion point */
} xi_parse_t;

typedef int (*xi_parse_emit_fn)(xi_parse_t *, xi_node_type_t, xi_node_t *,
				const char *, void *);

xi_parse_t *
xi_parse_open (pa_mmap_t *pmap, const char *name,
	       const char *filename, xi_source_flags_t flags);

void
xi_parse_destroy (xi_parse_t *parsep);

int
xi_parse (xi_parse_t *parsep);

void
xi_parse_dump (xi_parse_t *parsep);

void
xi_parse_emit (xi_parse_t *parsep, xi_parse_emit_fn func, void *opaque);

void
xi_parse_emit_xml (xi_parse_t *parsep, FILE *out);

void
xi_parse_set_rulebook (xi_parse_t *parsep, xi_rulebook_t *rulebook);

void
xi_parse_set_default_rule (xi_parse_t *parsep, xi_action_type_t type);

pa_atom_t
xi_parse_atom (xi_parse_t *parsep, const char *name);

void
xi_parse_set_rulebook (xi_parse_t *parsep, xi_rulebook_t *rulebook);

#endif /* LIBSLAX_XI_PARSE_H */
