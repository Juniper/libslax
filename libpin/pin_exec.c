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
 * Op-dispatch execution engine for libpin template bodies.
 * Phase 2+: real implementations replace these stubs.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include "slaxconfig.h"
#include <libpsu/psulog.h>
#include <parrotdb/pacommon.h>
#include <parrotdb/paconfig.h>
#include <parrotdb/pammap.h>
#include <parrotdb/pafixed.h>
#include <parrotdb/paarb.h>
#include <parrotdb/paistr.h>
#include <parrotdb/papat.h>
#include <parrotdb/pabitmap.h>

#include <libpin/pin_common.h>
#include <libpin/pin_rules.h>
#include <libpin/pin_tree.h>
#include <libpin/pin_workspace.h>
#include <libpin/pin_parse.h>

/* ------------------------------------------------------------------ */
/* Op dispatch table                                                   */
/* ------------------------------------------------------------------ */

static pin_value_t
pin_op_stub (pin_exec_state_t *esp, struct pin_parse_s *parsep, pin_op_t *opp)
{
    (void) esp; (void) parsep; (void) opp;
    return pin_value_null();
}

pin_op_def_t pin_op_table[PIN_OP_MAX] = {
    [PIN_OP_NONE]        = { "none",        pin_op_stub, 0 },
    [PIN_OP_EMIT_OPEN]   = { "emit-open",   pin_op_stub, 0 },
    [PIN_OP_EMIT_CLOSE]  = { "emit-close",  pin_op_stub, 0 },
    [PIN_OP_EMIT_ATTRIB] = { "emit-attrib", pin_op_stub, 0 },
    [PIN_OP_EMIT]        = { "emit",        pin_op_stub, 0 },
    [PIN_OP_PUSH_STRING] = { "push-string", pin_op_stub, 0 },
    [PIN_OP_PUSH_ATTR]   = { "push-attr",   pin_op_stub, 0 },
    [PIN_OP_PUSH_TEXT]   = { "push-text",   pin_op_stub, 0 },
    [PIN_OP_PUSH_NODES]  = { "push-nodes",  pin_op_stub, 0 },
    [PIN_OP_PUSH_BOOL]   = { "push-bool",   pin_op_stub, 0 },
    [PIN_OP_PUSH_NODE]   = { "push-node",   pin_op_stub, 0 },
    [PIN_OP_CONVERT]     = { "convert",     pin_op_stub, 0 },
    [PIN_OP_IF]          = { "if",          pin_op_stub, 0 },
    [PIN_OP_GOTO]        = { "goto",        pin_op_stub, 0 },
    [PIN_OP_JUMP]        = { "jump",        pin_op_stub, 0 },
    [PIN_OP_APPLY]       = { "apply",       pin_op_stub, 0 },
    [PIN_OP_CALL]        = { "call",        pin_op_stub, 0 },
    [PIN_OP_RETURN]      = { "return",      pin_op_stub, 0 },
    [PIN_OP_DISCARD]     = { "discard",     pin_op_stub, 0 },
    [PIN_OP_STORE_VAR]   = { "store-var",   pin_op_stub, 0 },
    [PIN_OP_LOAD_VAR]    = { "load-var",    pin_op_stub, 0 },
};

/* ------------------------------------------------------------------ */
/* Public API stubs (Phase 2+ will replace these)                     */
/* ------------------------------------------------------------------ */

int
pin_exec_run (pin_exec_state_t *esp, struct pin_parse_s *parsep,
	      pin_op_id_t start, pin_node_id_t context_node)
{
    (void) esp; (void) parsep; (void) start; (void) context_node;
    return 0;
}

void
pin_exec_dump (struct pin_rulebook_s *rbp, struct pin_parse_s *parsep,
	       FILE *out)
{
    (void) rbp; (void) parsep; (void) out;
}

/* Value stack helpers */

int
pin_exec_push (pin_exec_state_t *esp, pin_value_t val)
{
    if (esp->pes_val_top >= PIN_EXEC_VAL_MAX)
	return -1;
    esp->pes_val[esp->pes_val_top++] = val;
    return 0;
}

pin_value_t
pin_exec_pop (pin_exec_state_t *esp)
{
    if (esp->pes_val_top <= 0)
	return pin_value_null();
    return esp->pes_val[--esp->pes_val_top];
}

pin_value_t
pin_exec_peek (pin_exec_state_t *esp)
{
    if (esp->pes_val_top <= 0)
	return pin_value_null();
    return esp->pes_val[esp->pes_val_top - 1];
}

/* Nodeset table helpers */

uint32_t
pin_exec_nodeset_alloc (pin_exec_state_t *esp)
{
    if (esp->pes_nodeset_count >= esp->pes_nodeset_cap) {
	uint32_t newcap = esp->pes_nodeset_cap ? esp->pes_nodeset_cap * 2 : 8;
	pin_ns_entry_t *nsp = realloc(esp->pes_nodesets, newcap * sizeof(*nsp));
	if (nsp == NULL)
	    return UINT32_MAX;

	memset(nsp + esp->pes_nodeset_cap, 0,
	       (newcap - esp->pes_nodeset_cap) * sizeof(*nsp));
	esp->pes_nodesets = nsp;
	esp->pes_nodeset_cap = newcap;
    }

    return esp->pes_nodeset_count++;
}

void
pin_exec_nodeset_free (pin_exec_state_t *esp, uint32_t idx)
{
    if (idx >= esp->pes_nodeset_count)
	return;

    pin_ns_entry_t *nsp = &esp->pes_nodesets[idx];
    free(nsp->pne_nodes);

    nsp->pne_nodes = NULL;
    nsp->pne_count = 0;
    nsp->pne_cap = 0;
}

int
pin_exec_nodeset_append (pin_exec_state_t *esp, uint32_t idx,
			 uint32_t node_atom)
{
    if (idx >= esp->pes_nodeset_count)
	return -1;

    pin_ns_entry_t *nsp = &esp->pes_nodesets[idx];
    if (nsp->pne_count >= nsp->pne_cap) {
	uint32_t newcap = nsp->pne_cap ? nsp->pne_cap * 2 : 8;
	uint32_t *nodes = realloc(nsp->pne_nodes, newcap * sizeof(*nodes));
	if (nodes == NULL)
	    return -1;

	nsp->pne_nodes = nodes;
	nsp->pne_cap = newcap;
    }

    nsp->pne_nodes[nsp->pne_count++] = node_atom;
    return 0;
}
