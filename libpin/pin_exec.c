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
pin_op_stub (pin_exec_state_t *esp UNUSED, struct pin_parse_s *parsep UNUSED,
	     pin_op_t *opp)
{
    if (opp->po_type < PIN_OP_MAX)
        psu_log("pin_exec: stub: %s", pin_op_table[opp->po_type].pod_name);

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
/* Dispatch loop                                                       */
/* ------------------------------------------------------------------ */

static int
pin_exec_seq_grow (pin_exec_state_t *esp)
{
    if (esp->pes_seq_top < esp->pes_seq_cap)
        return 0;

    int newcap = esp->pes_seq_cap ? esp->pes_seq_cap * 2 : 8;
    pin_exec_seq_frame_t *seq = realloc(esp->pes_seq, newcap * sizeof(*seq));
    if (seq == NULL)
        return -1;

    esp->pes_seq = seq;
    esp->pes_seq_cap = newcap;
    return 0;
}

int
pin_exec_run (pin_exec_state_t *esp, struct pin_parse_s *parsep,
	      pin_op_id_t start, pin_node_id_t context_node)
{
    pin_rulebook_t *prbp = parsep->pp_rulebook;

    if (pin_exec_seq_grow(esp) < 0)
        return -1;

    int top = esp->pes_seq_top;
    esp->pes_seq[top].psf_pc = start;
    esp->pes_seq[top].psf_context = context_node;
    esp->pes_seq_top += 1;

    while (esp->pes_seq_top > 0) {
        top = esp->pes_seq_top - 1;

        pin_op_id_t pc = esp->pes_seq[top].psf_pc;
        if (pin_op_id_is_null(pc)) {
            esp->pes_seq_top -= 1;
            break;
        }

        pin_op_t *opp = pin_op_addr(prbp, pc);
        if (opp == NULL) {
            esp->pes_seq_top -= 1;
            break;
        }

        /* Advance PC before dispatch; pod_func may override for branches.
         * Do not hold a pointer into pes_seq across the pod_func call —
         * ops that push new frames (CALL, APPLY) may realloc pes_seq. */
        esp->pes_seq[top].psf_pc = opp->po_next;

        pin_op_type_t type = opp->po_type;
        if (type < PIN_OP_MAX && pin_op_table[type].pod_func)
            pin_op_table[type].pod_func(esp, parsep, opp);
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Rulebook stack helpers                                              */
/* ------------------------------------------------------------------ */

static int
pin_exec_rb_grow (pin_exec_state_t *esp)
{
    if (esp->pes_rb_top < esp->pes_rb_cap)
        return 0;

    int newcap = esp->pes_rb_cap ? esp->pes_rb_cap * 2 : 8;
    pin_exec_rb_frame_t *rb = realloc(esp->pes_rb, newcap * sizeof(*rb));
    if (rb == NULL)
        return -1;

    esp->pes_rb = rb;
    esp->pes_rb_cap = newcap;
    return 0;
}

int
pin_exec_rb_push (pin_exec_state_t *esp, pin_rstate_id_t sid, pin_depth_t depth)
{
    if (pin_exec_rb_grow(esp) < 0)
        return -1;

    esp->pes_rb[esp->pes_rb_top].prf_sid = sid;
    esp->pes_rb[esp->pes_rb_top].prf_depth = depth;
    esp->pes_rb_top += 1;
    return 0;
}

void
pin_exec_rb_pop (pin_exec_state_t *esp)
{
    if (esp->pes_rb_top > 0)
        esp->pes_rb_top -= 1;
}

/* ------------------------------------------------------------------ */
/* Dump helpers                                                        */
/* ------------------------------------------------------------------ */

static void
pin_exec_dump_ops (pin_rulebook_t *prbp, pin_op_id_t oid, FILE *out)
{
    while (!pin_op_id_is_null(oid)) {
        pin_op_t *opp = pin_op_addr(prbp, oid);
        if (opp == NULL)
            break;

        pa_atom_t oidn = pa_fixed_atom_of(pin_op_id_atom_of(oid));
        const char *opname = (opp->po_type < PIN_OP_MAX)
            ? pin_op_table[opp->po_type].pod_name : "?";
        const char *name = pin_namepool_string(prbp->prb_workspace, opp->po_name);
        const char *name2 = pin_namepool_string(prbp->prb_workspace, opp->po_name2);

        fprintf(out, "      op %u: %s", (unsigned) oidn, opname);
        if (name)
            fprintf(out, " '%s'", name);
        if (name2)
            fprintf(out, " '%s'", name2);

        pa_atom_t alt = pa_fixed_atom_of(pin_op_id_atom_of(opp->po_alt));
        if (alt != 0)
            fprintf(out, " alt=%u", (unsigned) alt);

        const char *src = pin_namepool_string(prbp->prb_workspace, opp->po_src_file);
        if (src && opp->po_src_line)
            fprintf(out, " [%s:%u]", src, opp->po_src_line);

        fprintf(out, "\n");
        oid = opp->po_next;
    }
}

static void
pin_exec_dump_rules (pin_rulebook_t *prbp, pin_rule_id_t rid, FILE *out)
{
    while (!pin_rule_id_is_null(rid)) {
        pin_rule_t *rulep = pin_rulebook_rule(prbp, rid);
        if (rulep == NULL)
            break;

        pa_atom_t ridn = pa_fixed_atom_of(pin_rule_id_atom_of(rid));
        fprintf(out, "    rule %u: action %u flags %#x\n",
                (unsigned) ridn, rulep->pr_action, rulep->pr_flags);

        if (!pin_op_id_is_null(rulep->pr_close_ops))
            pin_exec_dump_ops(prbp, rulep->pr_close_ops, out);

        rid = rulep->pr_next;
    }
}

void
pin_exec_dump (struct pin_rulebook_s *rbp, struct pin_parse_s *parsep UNUSED,
	       FILE *out)
{
    pin_rulebook_t *prbp = rbp;

    if (prbp == NULL || prbp->prb_infop == NULL || out == NULL)
        return;

    pa_atom_t max_sid = pa_fixed_atom_of(
            pin_rstate_id_atom_of(prbp->prb_infop->prsi_max_state));
    fprintf(out, "rulebook: %u states\n", (unsigned) max_sid);

    for (pa_atom_t sid = 1; sid <= max_sid; sid++) {
        pin_rstate_t *statep =
            (pin_rstate_t *) pa_fixed_element(prbp->prb_states, sid);
        if (statep == NULL)
            continue;

        fprintf(out, "  state %u: flags %#x\n",
                (unsigned) sid, statep->prbs_flags);

        pin_exec_dump_rules(prbp, statep->prbs_first_rule, out);

        if (!pin_rule_id_is_null(statep->prbs_default_rule)) {
            fprintf(out, "    (default):\n");
            pin_exec_dump_rules(prbp, statep->prbs_default_rule, out);
        }
    }
}

/* Value stack helpers */

static int
pin_exec_val_grow (pin_exec_state_t *esp)
{
    if (esp->pes_val_top < esp->pes_val_cap)
        return 0;

    int newcap = esp->pes_val_cap ? esp->pes_val_cap * 2 : 16;
    pin_value_t *val = realloc(esp->pes_val, newcap * sizeof(*val));
    if (val == NULL)
        return -1;

    esp->pes_val = val;
    esp->pes_val_cap = newcap;
    return 0;
}

int
pin_exec_push (pin_exec_state_t *esp, pin_value_t val)
{
    if (pin_exec_val_grow(esp) < 0)
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
