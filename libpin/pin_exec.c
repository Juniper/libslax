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

/*
 * Helpers used by multiple op functions
 */

/*
 * Collect all direct text children of the node at nid, concatenate them,
 * intern the result in the namepool, and return the atom.
 * Returns the null atom when the node has no text content.
 */
static pin_name_id_t
pin_exec_text_of (pin_workspace_t *pwp, pin_node_id_t nid)
{
    pin_node_t *nodep = pin_node_addr(pwp, nid);
    if (nodep == NULL)
        return pin_name_id_null_atom();

    pin_depth_t depth = nodep->pn_depth;
    xo_buffer_t buf;
    xo_buf_init(&buf);

    for (pin_node_id_t cid = pin_node_child(nodep); !pin_node_id_is_null(cid); ) {
        pin_node_t *child = pin_node_addr(pwp, cid);
        if (child == NULL || child->pn_depth <= depth)
            break;

        if (child->pn_type == PIN_TYPE_TEXT || child->pn_type == PIN_TYPE_UNESC) {
            const char *text = pin_textpool_string(pwp,
                    pa_arb_atom_of(pin_node_text(child)));
            if (text)
                xo_buf_append(&buf, text, (ssize_t) strlen(text));
        }

        cid = child->pn_next;
    }

    pin_name_id_t result = pin_name_id_null_atom();
    if (xo_buf_offset(&buf) > 0) {
        xo_buf_append(&buf, "", 1);
        result = pin_namepool_atom(pwp, xo_buf_data(&buf, 0), TRUE);
    }
    xo_buf_cleanup(&buf);
    return result;
}

/*
 * Check whether a node is truly a boolean-true nodeset value
 * (non-null index AND that slot has at least one node).
 */
static int
pin_exec_nodeset_is_true (pin_exec_state_t *esp, pin_value_t v)
{
    return v.pv_type == PVT_NODESET
        && v.pv_atom < (uint32_t) esp->pes_nodeset_count
        && esp->pes_nodesets[v.pv_atom].pne_count > 0;
}

/*
 * Variable bindings: find an existing slot by name atom, or allocate a new one.
 */
static pin_var_binding_t *
pin_exec_var_find (pin_exec_state_t *esp, uint32_t name_atom)
{
    for (uint32_t i = 0; i < esp->pes_var_count; i++) {
        if (esp->pes_vars[i].pvb_name == name_atom)
            return &esp->pes_vars[i];
    }
    return NULL;
}

static pin_var_binding_t *
pin_exec_var_alloc (pin_exec_state_t *esp, uint32_t name_atom)
{
    pin_var_binding_t *vp = pin_exec_var_find(esp, name_atom);
    if (vp)
        return vp;

    if (esp->pes_var_count >= esp->pes_var_cap) {
        int newcap = esp->pes_var_cap ? esp->pes_var_cap * 2 : 8;
        vp = realloc(esp->pes_vars, newcap * sizeof(*vp));
        if (vp == NULL)
            return NULL;

        esp->pes_vars = vp;
        esp->pes_var_cap = newcap;
    }

    vp = &esp->pes_vars[esp->pes_var_count];
    vp->pvb_name = name_atom;
    vp->pvb_value = pin_value_null();
    esp->pes_var_count += 1;
    return vp;
}

/*
 * Op functions
 */

/* Stub for ops not yet implemented: logs the op name */
static pin_value_t
pin_op_stub (pin_exec_state_t *esp UNUSED, struct pin_parse_s *parsep UNUSED,
	     pin_op_t *opp)
{
    if (opp->po_type < PIN_OP_MAX)
        psu_log("pin_exec: stub: %s", pin_op_table[opp->po_type].pod_name);

    return pin_value_null();
}

static pin_value_t
pin_op_emit_open (pin_exec_state_t *esp UNUSED, struct pin_parse_s *parsep,
		  pin_op_t *opp)
{
    pin_workspace_t *pwp = pin_parse_workspace(parsep);
    const char *tag = pin_namepool_string(pwp, opp->po_name);
    pin_insert_t *pip = parsep->pp_insert;
    pin_action_type_t act = pip->pin_stack[pip->pin_depth].ps_action;
    pin_insert_open(parsep, opp->po_name, NULL, tag, NULL, act);
    return pin_value_null();
}

static pin_value_t
pin_op_emit_close (pin_exec_state_t *esp UNUSED, struct pin_parse_s *parsep,
		   pin_op_t *opp)
{
    pin_workspace_t *pwp = pin_parse_workspace(parsep);
    const char *tag = pin_namepool_string(pwp, opp->po_name);
    pin_insert_close(parsep, NULL, tag);
    return pin_value_null();
}

static pin_value_t
pin_op_emit (pin_exec_state_t *esp, struct pin_parse_s *parsep,
	     pin_op_t *opp UNUSED)
{
    pin_value_t v = pin_exec_pop(esp);
    if (v.pv_type == PVT_STRING) {
        pin_workspace_t *pwp = pin_parse_workspace(parsep);
        const char *str = pin_namepool_string(pwp, pin_name_id(v.pv_atom));
        if (str)
            pin_insert_text(parsep, str, strlen(str), PIN_TYPE_TEXT);
    }
    return pin_value_null();
}

static pin_value_t
pin_op_push_string (pin_exec_state_t *esp, struct pin_parse_s *parsep UNUSED,
		    pin_op_t *opp)
{
    pin_exec_push(esp, pin_value_string(pin_name_id_atom_of(opp->po_name)));
    return pin_value_null();
}

static pin_value_t
pin_op_push_attr (pin_exec_state_t *esp, struct pin_parse_s *parsep, pin_op_t *opp)
{
    pin_workspace_t *pwp = pin_parse_workspace(parsep);
    pin_node_t *nodep = pin_node_addr(pwp,
            esp->pes_seq[esp->pes_seq_top - 1].psf_context);

    pin_name_id_t result = pin_name_id_null_atom();
    if (nodep) {
        const char *str = pin_get_attrib_string(pwp, nodep, opp->po_name);
        if (str)
            result = pin_namepool_atom(pwp, str, TRUE);
    }

    pin_exec_push(esp, pin_value_string(pin_name_id_atom_of(result)));
    return pin_value_null();
}

static pin_value_t
pin_op_push_text (pin_exec_state_t *esp, struct pin_parse_s *parsep, pin_op_t *opp)
{
    pin_workspace_t *pwp = pin_parse_workspace(parsep);
    pin_node_id_t target = esp->pes_seq[esp->pes_seq_top - 1].psf_context;

    /* po_name == "." → text of context node; any other name → first matching child */
    const char *sel = pin_namepool_string(pwp, opp->po_name);
    if (sel && strcmp(sel, ".") != 0) {
        pin_node_t *parent = pin_node_addr(pwp, target);
        if (parent) {
            pin_depth_t depth = parent->pn_depth;
            for (pin_node_id_t cid = pin_node_child(parent);
                    !pin_node_id_is_null(cid); ) {
                pin_node_t *child = pin_node_addr(pwp, cid);
                if (child == NULL || child->pn_depth <= depth)
                    break;
                if (child->pn_type == PIN_TYPE_ELT
                        && pin_name_id_equal(child->pn_name, opp->po_name)) {
                    target = cid;
                    break;
                }
                cid = child->pn_next;
            }
        }
    }

    pin_name_id_t result = pin_exec_text_of(pwp, target);
    pin_exec_push(esp, pin_value_string(pin_name_id_atom_of(result)));
    return pin_value_null();
}

static pin_value_t
pin_op_push_nodes (pin_exec_state_t *esp, struct pin_parse_s *parsep, pin_op_t *opp)
{
    pin_workspace_t *pwp = pin_parse_workspace(parsep);
    pin_node_t *parent = pin_node_addr(pwp,
            esp->pes_seq[esp->pes_seq_top - 1].psf_context);

    uint32_t ns_idx = pin_exec_nodeset_alloc(esp);
    if (ns_idx == UINT32_MAX) {
        pin_exec_push(esp, pin_value_null());
        return pin_value_null();
    }

    if (parent) {
        pin_depth_t depth = parent->pn_depth;
        for (pin_node_id_t cid = pin_node_child(parent);
                !pin_node_id_is_null(cid); ) {
            pin_node_t *child = pin_node_addr(pwp, cid);
            if (child == NULL || child->pn_depth <= depth)
                break;
            if (child->pn_type == PIN_TYPE_ELT
                    && pin_name_id_equal(child->pn_name, opp->po_name))
                pin_exec_nodeset_append(esp, ns_idx,
                        pa_fixed_atom_of(pin_node_id_atom_of(cid)));
            cid = child->pn_next;
        }
    }

    pin_exec_push(esp, pin_value_nodeset(ns_idx));
    return pin_value_null();
}

static pin_value_t
pin_op_push_bool (pin_exec_state_t *esp, struct pin_parse_s *parsep UNUSED,
		  pin_op_t *opp UNUSED)
{
    pin_value_t v = pin_exec_pop(esp);
    int truth = (v.pv_type == PVT_NODESET)
        ? pin_exec_nodeset_is_true(esp, v)
        : pin_value_is_true(v);
    pin_exec_push(esp, pin_value_bool(truth));
    return pin_value_null();
}

static pin_value_t
pin_op_push_node (pin_exec_state_t *esp, struct pin_parse_s *parsep UNUSED,
		  pin_op_t *opp UNUSED)
{
    pin_node_id_t ctx = esp->pes_seq[esp->pes_seq_top - 1].psf_context;
    pin_exec_push(esp, pin_value_node(pa_fixed_atom_of(pin_node_id_atom_of(ctx))));
    return pin_value_null();
}

static pin_value_t
pin_op_if (pin_exec_state_t *esp, struct pin_parse_s *parsep UNUSED, pin_op_t *opp)
{
    pin_value_t v = pin_exec_pop(esp);
    int truth = (v.pv_type == PVT_NODESET)
        ? pin_exec_nodeset_is_true(esp, v)
        : pin_value_is_true(v);

    if (!truth && esp->pes_seq_top > 0)
        esp->pes_seq[esp->pes_seq_top - 1].psf_pc = opp->po_alt;

    return pin_value_null();
}

static pin_value_t
pin_op_goto (pin_exec_state_t *esp, struct pin_parse_s *parsep UNUSED, pin_op_t *opp)
{
    if (esp->pes_seq_top > 0)
        esp->pes_seq[esp->pes_seq_top - 1].psf_pc = opp->po_alt;
    return pin_value_null();
}

static pin_value_t
pin_op_jump (pin_exec_state_t *esp UNUSED, struct pin_parse_s *parsep UNUSED,
	     pin_op_t *opp UNUSED)
{
    return pin_value_null();
}

static pin_value_t
pin_op_discard (pin_exec_state_t *esp, struct pin_parse_s *parsep UNUSED,
		pin_op_t *opp UNUSED)
{
    pin_exec_pop(esp);
    return pin_value_null();
}

static pin_value_t
pin_op_store_var (pin_exec_state_t *esp, struct pin_parse_s *parsep UNUSED,
		  pin_op_t *opp)
{
    pin_value_t v = pin_exec_pop(esp);
    pin_var_binding_t *vp = pin_exec_var_alloc(esp,
            pin_name_id_atom_of(opp->po_name));
    if (vp)
        vp->pvb_value = v;
    return pin_value_null();
}

static pin_value_t
pin_op_load_var (pin_exec_state_t *esp, struct pin_parse_s *parsep UNUSED,
		 pin_op_t *opp)
{
    pin_var_binding_t *vp = pin_exec_var_find(esp,
            pin_name_id_atom_of(opp->po_name));
    pin_exec_push(esp, vp ? vp->pvb_value : pin_value_null());
    return pin_value_null();
}

/*
 * Op dispatch table
 */

pin_op_def_t pin_op_table[PIN_OP_MAX] = {
    [PIN_OP_NONE]        = { "none",        pin_op_stub,        0 },
    [PIN_OP_EMIT_OPEN]   = { "emit-open",   pin_op_emit_open,   0 },
    [PIN_OP_EMIT_CLOSE]  = { "emit-close",  pin_op_emit_close,  0 },
    [PIN_OP_EMIT_ATTRIB] = { "emit-attrib", pin_op_stub,        0 },
    [PIN_OP_EMIT]        = { "emit",        pin_op_emit,        0 },
    [PIN_OP_PUSH_STRING] = { "push-string", pin_op_push_string, 0 },
    [PIN_OP_PUSH_ATTR]   = { "push-attr",   pin_op_push_attr,   0 },
    [PIN_OP_PUSH_TEXT]   = { "push-text",   pin_op_push_text,   0 },
    [PIN_OP_PUSH_NODES]  = { "push-nodes",  pin_op_push_nodes,  0 },
    [PIN_OP_PUSH_BOOL]   = { "push-bool",   pin_op_push_bool,   0 },
    [PIN_OP_PUSH_NODE]   = { "push-node",   pin_op_push_node,   0 },
    [PIN_OP_CONVERT]     = { "convert",     pin_op_stub,        0 },
    [PIN_OP_IF]          = { "if",          pin_op_if,          0 },
    [PIN_OP_GOTO]        = { "goto",        pin_op_goto,        0 },
    [PIN_OP_JUMP]        = { "jump",        pin_op_jump,        0 },
    [PIN_OP_APPLY]       = { "apply",       pin_op_stub,        0 },
    [PIN_OP_CALL]        = { "call",        pin_op_stub,        0 },
    [PIN_OP_RETURN]      = { "return",      pin_op_stub,        0 },
    [PIN_OP_DISCARD]     = { "discard",     pin_op_discard,     0 },
    [PIN_OP_STORE_VAR]   = { "store-var",   pin_op_store_var,   0 },
    [PIN_OP_LOAD_VAR]    = { "load-var",    pin_op_load_var,    0 },
};

/*
 * Dispatch loop
 */

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

/*
 * Rulebook stack helpers
 */

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

/*
 * Dump helpers
 */

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
