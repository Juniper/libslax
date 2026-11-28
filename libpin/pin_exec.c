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
#include <stdbool.h>
#include <math.h>

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
#include <libpin/pin_sort.h>
#include <libpin/pin_filter.h>
#include <libpin/pin_output.h>
#include <libpin/pin_output_store.h>
#include <libxo/xo.h>
#include <libxo/xo_filter.h>

/*
 * Helpers used by multiple op functions
 */

/*
 * Append the text children of nodep to buf, in document order.  With
 * "deep", text in descendant elements is included too.
 */
static void
pin_exec_text_collect (pin_workspace_t *pwp, pin_node_t *nodep,
                       xo_buffer_t *bufp, pin_boolean_t deep)
{
    pin_depth_t depth = nodep->pn_depth;

    for (pin_node_id_t cid = pin_node_child(nodep); !pin_node_id_is_null(cid); ) {
        pin_node_t *child = pin_node_addr(pwp, cid);
        if (child == NULL || child->pn_depth <= depth)
            break;

        if (child->pn_type == PIN_TYPE_TEXT || child->pn_type == PIN_TYPE_UNESC) {
            const char *text = pin_textpool_string(pwp,
                    pa_arb_atom_of(pin_node_text(child)));
            if (text)
                xo_buf_append(bufp, text, (ssize_t) strlen(text));
        } else if (deep && child->pn_type == PIN_TYPE_ELT) {
            pin_exec_text_collect(pwp, child, bufp, deep);
        }

        cid = child->pn_next;
    }
}

/*
 * Collect the string value of the node at nid (all its descendant
 * text), intern it in the namepool, and return the atom.  Returns the
 * null atom when there is no text.
 */
pin_name_id_t
pin_exec_text_of (pin_workspace_t *pwp, pin_node_id_t nid)
{
    pin_node_t *nodep = pin_node_addr(pwp, nid);
    if (nodep == NULL)
        return pin_name_id_null_atom();

    xo_buffer_t buf;
    xo_buf_init(&buf);

    pin_exec_text_collect(pwp, nodep, &buf, TRUE);

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
 * Variable bindings are a stack.  Each frame records the count when
 * its scope began, and the count goes back there when the scope ends
 * (a call returns, or a loop moves to its next item).  A lookup finds
 * the newest binding, so an inner declaration hides an outer one.
 */
pin_var_binding_t *
pin_exec_var_find (pin_exec_state_t *esp, uint32_t name_atom)
{
    for (uint32_t i = esp->pes_var_count; i > 0; i--) {
        if (esp->pes_vars[i - 1].pvb_name == name_atom)
            return &esp->pes_vars[i - 1];
    }
    return NULL;
}

static pin_var_binding_t *
pin_exec_var_new (pin_exec_state_t *esp, uint32_t name_atom)
{
    pin_var_binding_t *vp;

    if (esp->pes_var_count >= esp->pes_var_size) {
        int newsize = esp->pes_var_size ? esp->pes_var_size * 2 : 8;
        vp = realloc(esp->pes_vars, newsize * sizeof(*vp));
        if (vp == NULL)
            return NULL;

        esp->pes_vars = vp;
        esp->pes_var_size = newsize;
    }

    vp = &esp->pes_vars[esp->pes_var_count];
    vp->pvb_name = name_atom;
    vp->pvb_value = pin_value_null();
    esp->pes_var_count += 1;
    return vp;
}

/* The visible binding for an assignment, made if there is none */
static pin_var_binding_t *
pin_exec_var_alloc (pin_exec_state_t *esp, uint32_t name_atom)
{
    pin_var_binding_t *vp = pin_exec_var_find(esp, name_atom);
    if (vp)
        return vp;

    return pin_exec_var_new(esp, name_atom);
}

/*
 * The binding for a declaration in the current scope.  A binding
 * from an outer scope is hidden, not changed.
 */
static pin_var_binding_t *
pin_exec_var_bind (pin_exec_state_t *esp, uint32_t name_atom)
{
    uint32_t base = (esp->pes_seq_top > 0)
        ? esp->pes_seq[esp->pes_seq_top - 1].psf_saved_var_count : 0;

    for (uint32_t i = esp->pes_var_count; i > base; i--) {
        if (esp->pes_vars[i - 1].pvb_name == name_atom)
            return &esp->pes_vars[i - 1];
    }

    return pin_exec_var_new(esp, name_atom);
}

/*
 * Op functions
 */

/* Stub for ops not yet implemented: logs the op name */
static pin_value_t
pin_op_stub (PIN_OP_FUNC_ARGS)
{
    if (opp->po_type < PIN_OP_MAX)
        psu_log("pin_exec: stub: %s", pin_op_table[opp->po_type].pod_name);

    return pin_value_null();
}

static pin_value_t
pin_op_emit_open (PIN_OP_FUNC_ARGS)
{
    pin_workspace_t *pwp = pin_parse_workspace(parsep);
    const char *tag = pin_namepool_string(pwp, opp->po_name);
    const char *astr = pin_namepool_string(pwp, opp->po_name2);
    char *attribs = astr ? strdup(astr) : NULL;
    pin_insert_t *pip = parsep->pp_insert;
    pin_action_type_t act = pin_insert_top_frame(pip)->ps_action;
    if (attribs || opp->po_count)
	act = PIA_SAVE_ATTRIB;
    pin_insert_open(parsep, opp->po_name, NULL, tag, attribs, act);
    free(attribs);
    return pin_value_null();
}

static pin_value_t
pin_op_emit_close (PIN_OP_FUNC_ARGS)
{
    pin_workspace_t *pwp = pin_parse_workspace(parsep);
    const char *tag = pin_namepool_string(pwp, opp->po_name);
    pin_insert_close(parsep, NULL, tag);
    return pin_value_null();
}

static pin_value_t
pin_op_emit_attrib (PIN_OP_FUNC_ARGS)
{
    pin_workspace_t *pwp = pin_parse_workspace(parsep);
    const char *aname = pin_namepool_string(pwp, opp->po_name);
    if (!aname)
        return pin_value_null();
    pin_value_t v = pin_exec_pop(esp);
    char valbuf[512];
    valbuf[0] = '\0';
    if (v.pv_type == PVT_STRING) {
        const char *str = pin_namepool_string(pwp, pin_name_id(v.pv_atom));
        if (str)
            snprintf(valbuf, sizeof(valbuf), "%s", str);
    }
    pin_insert_attrib_pair(parsep, aname, valbuf);
    return pin_value_null();
}

static pin_value_t
pin_op_element_open (PIN_OP_FUNC_ARGS)
{
    pin_workspace_t *pwp = pin_parse_workspace(parsep);
    pin_insert_t *pip = parsep->pp_insert;
    const char *tag;
    pin_name_id_t tag_id;

    if (!pin_name_id_is_null(opp->po_name)) {
        /* Static element name */
        tag = pin_namepool_string(pwp, opp->po_name);
        tag_id = opp->po_name;
    } else {
        /* Computed element name: pop string from value stack */
        pin_value_t v = pin_exec_pop(esp);
        if (v.pv_type != PVT_STRING)
            return pin_value_null();
        tag = pin_namepool_string(pwp, pin_name_id(v.pv_atom));
        if (!tag)
            return pin_value_null();
        tag_id = pin_name_id(v.pv_atom);
    }

    const char *astr = pin_namepool_string(pwp, opp->po_name2);
    char *attribs = astr ? strdup(astr) : NULL;
    pin_action_type_t act = pin_insert_top_frame(pip)->ps_action;
    if (attribs)
        act = PIA_SAVE_ATTRIB;
    pin_insert_open(parsep, tag_id, NULL, tag, attribs, act);
    free(attribs);
    return pin_value_null();
}

static pin_value_t
pin_op_element_close (PIN_OP_FUNC_ARGS)
{
    pin_insert_t *pip = parsep->pp_insert;
    pin_workspace_t *pwp = pin_parse_workspace(parsep);
    pin_name_id_t top_name = pin_insert_top_name(pip);
    if (!pin_name_id_is_null(top_name)) {
        const char *tag = pin_namepool_string(pwp, top_name);
        if (tag)
            pin_insert_close(parsep, NULL, tag);
    }
    return pin_value_null();
}

static pin_value_t
pin_op_copy_open (PIN_OP_FUNC_ARGS)
{
    pin_workspace_t *pwp = pin_parse_workspace(parsep);
    int top = esp->pes_seq_top;
    if (top <= 0)
        return pin_value_null();
    pin_node_id_t ctx = esp->pes_seq[top - 1].psf_context;
    pin_node_t *ctx_node = pin_node_addr(pwp, ctx);
    if (ctx_node == NULL)
        return pin_value_null();

    const char *tag = pin_namepool_string(pwp, ctx_node->pn_name);
    if (tag == NULL)
        return pin_value_null();

    pin_insert_t *pip = parsep->pp_insert;
    pin_action_type_t act = pin_insert_top_frame(pip)->ps_action;
    pin_insert_open(parsep, ctx_node->pn_name, NULL, tag, NULL, act);
    return pin_value_null();
}

static pin_value_t
pin_op_message_open (PIN_OP_FUNC_ARGS)
{
    pin_capture_start(parsep, 1, 0);
    return pin_value_null();
}

static pin_value_t
pin_op_message_close (PIN_OP_FUNC_ARGS)
{
    pin_capture_flush(parsep);
    if (!pin_name_id_is_null(opp->po_name))
        exit(1);
    return pin_value_null();
}

static pin_value_t
pin_op_comment_open (PIN_OP_FUNC_ARGS)
{
    pin_capture_start(parsep, 2, 0);
    return pin_value_null();
}

static pin_value_t
pin_op_comment_close (PIN_OP_FUNC_ARGS)
{
    pin_capture_flush(parsep);
    return pin_value_null();
}

static pin_value_t
pin_op_pi_open (PIN_OP_FUNC_ARGS)
{
    unsigned name_atom = (unsigned) pin_name_id_atom_of(opp->po_name);
    pin_capture_start(parsep, 3, name_atom);
    return pin_value_null();
}

static pin_value_t
pin_op_pi_close (PIN_OP_FUNC_ARGS)
{
    pin_capture_flush(parsep);
    return pin_value_null();
}

static pin_value_t
pin_op_number (PIN_OP_FUNC_ARGS)
{
    pin_workspace_t *pwp = pin_parse_workspace(parsep);
    int top = esp->pes_seq_top;
    pin_node_id_t ctx = (top > 0) ? esp->pes_seq[top - 1].psf_context
                                   : pin_node_id_null_atom();

    const char *expr = pin_namepool_string(pwp, opp->po_name);
    if (expr == NULL || expr[0] == '\0')
        return pin_value_null();

    char vbuf[64];
    pin_exec_eval_expr_string(pwp, ctx, expr, strlen(expr), vbuf, sizeof(vbuf));
    if (vbuf[0] == '\0')
        return pin_value_null();

    /* Format: "1" (default) = decimal integer; just emit the string value */
    const char *fmt = pin_namepool_string(pwp, opp->po_name2);
    if (fmt == NULL || strcmp(fmt, "1") == 0) {
        /* Convert to integer and back to strip ".0" suffix from floats */
        long n = strtol(vbuf, NULL, 10);
        char nbuf[32];
        snprintf(nbuf, sizeof(nbuf), "%ld", n);
        pin_insert_text(parsep, nbuf, strlen(nbuf), PIN_TYPE_TEXT);
    } else {
        pin_insert_text(parsep, vbuf, strlen(vbuf), PIN_TYPE_TEXT);
    }
    return pin_value_null();
}

static pin_value_t
pin_op_emit (PIN_OP_FUNC_ARGS)
{
    pin_workspace_t *pwp = pin_parse_workspace(parsep);
    pin_value_t v = pin_exec_pop(esp);

    if (v.pv_type == PVT_NODESET) {
        /* Emit string value of first node in the nodeset */
        if (v.pv_atom < (uint32_t) esp->pes_nodeset_count) {
            pin_ns_entry_t *nsp = &esp->pes_nodesets[v.pv_atom];
            if (nsp->pne_count > 0) {
                pin_node_id_t nid = pin_node_id(nsp->pne_nodes[0]);
                pin_name_id_t tid = pin_exec_text_of(pwp, nid);
                const char *str = pin_namepool_string(pwp, tid);
                if (str)
                    pin_insert_text(parsep, str, strlen(str), PIN_TYPE_TEXT);
            }
        }
    } else if (v.pv_type == PVT_STRING) {
        const char *str = pin_namepool_string(pwp, pin_name_id(v.pv_atom));
        if (str && *str)
            pin_insert_text(parsep, str, strlen(str), PIN_TYPE_TEXT);
    } else if (v.pv_type == PVT_BOOLEAN) {
        const char *str = (v.pv_flags & PVF_TRUE) ? "true" : "false";
        pin_insert_text(parsep, str, strlen(str), PIN_TYPE_TEXT);
    } else if (v.pv_type == PVT_NODE) {
        /* A node's string value is the text of its whole subtree */
        pin_node_t *nodep = pin_node_addr(pwp, pin_node_id(v.pv_atom));
        if (nodep) {
            xo_buffer_t buf;
            xo_buf_init(&buf);
            pin_exec_text_collect(pwp, nodep, &buf, TRUE);
            if (xo_buf_offset(&buf) > 0)
                pin_insert_text(parsep, xo_buf_data(&buf, 0),
                                xo_buf_offset(&buf), PIN_TYPE_TEXT);
            xo_buf_cleanup(&buf);
        }
    }
    return pin_value_null();
}

static pin_value_t
pin_op_push_string (PIN_OP_FUNC_ARGS)
{
    pin_exec_push(esp, pin_value_string(pin_name_id_atom_of(opp->po_name)));
    return pin_value_null();
}

static pin_value_t
pin_op_push_attr (PIN_OP_FUNC_ARGS)
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

/*
 * Walk a slash-separated element path from start, returning the node at
 * the end of the path.  Each step descends to the first direct child
 * element with that name; a "." step leaves the current node unchanged,
 * and ".." moves to its parent.  Returns null_atom if any non-trivial
 * step is not found.
 */
pin_node_id_t
pin_exec_node_at_path (pin_workspace_t *pwp, pin_node_id_t start,
                       const char *path, size_t plen)
{
    pin_node_id_t target = start;
    const char *p = path;
    size_t remaining = plen;

    while (remaining > 0) {
        const char *slash = memchr(p, '/', remaining);
        size_t step_len = slash ? (size_t)(slash - p) : remaining;

        if (step_len == 2 && p[0] == '.' && p[1] == '.') {
            pin_node_t *node = pin_node_addr(pwp, target);
            if (node == NULL || pin_node_id_is_null(node->pn_parent))
                return pin_node_id_null_atom();
            target = node->pn_parent;
        } else if (step_len > 0 && !(step_len == 1 && p[0] == '.')) {
            char stepbuf[step_len + 1];
            memcpy(stepbuf, p, step_len);
            stepbuf[step_len] = '\0';

            pin_name_id_t step_name = pin_namepool_atom(pwp, stepbuf, FALSE);
            pin_node_t *node = pin_node_addr(pwp, target);
            if (node == NULL || pin_name_id_is_null(step_name))
                return pin_node_id_null_atom();

            pin_depth_t depth = node->pn_depth;
            pin_node_id_t found = pin_node_id_null_atom();
            for (pin_node_id_t kid = pin_node_child(node);
                    !pin_node_id_is_null(kid); ) {
                pin_node_t *child = pin_node_addr(pwp, kid);
                if (child == NULL || child->pn_depth <= depth)
                    break;
                if (child->pn_type == PIN_TYPE_ELT
                        && pin_name_id_equal(child->pn_name, step_name)) {
                    found = kid;
                    break;
                }
                kid = child->pn_next;
            }
            if (pin_node_id_is_null(found))
                return pin_node_id_null_atom();
            target = found;
        }

        if (!slash)
            break;
        remaining -= step_len + 1;
        p = slash + 1;
    }

    return target;
}

static pin_value_t
pin_op_push_text (PIN_OP_FUNC_ARGS)
{
    pin_workspace_t *pwp = pin_parse_workspace(parsep);
    pin_node_id_t ctx = esp->pes_seq[esp->pes_seq_top - 1].psf_context;

    const char *sel = pin_namepool_string(pwp, opp->po_name);
    size_t slen = sel ? strlen(sel) : 0;
    pin_node_id_t target = pin_exec_node_at_path(pwp, ctx, sel ? sel : ".",
                                                 slen ? slen : 1);
    pin_name_id_t result = pin_node_id_is_null(target)
        ? pin_name_id_null_atom() : pin_exec_text_of(pwp, target);
    pin_exec_push(esp, pin_value_string(pin_name_id_atom_of(result)));
    return pin_value_null();
}

/*
 * Collect all nodes reachable from 'start' by descending the slash-separated
 * 'path', appending each terminal node to nodeset 'ns_idx'.
 * Only direct children are visited at each step (child:: axis).
 * A "." step leaves the current node unchanged; an empty step is a no-op.
 */
static void
pin_exec_collect_path (pin_workspace_t *pwp, pin_exec_state_t *esp,
                       pin_node_id_t start, const char *path, size_t plen,
                       uint32_t ns_idx)
{
    const char *slash = memchr(path, '/', plen);
    size_t step_len = slash ? (size_t)(slash - path) : plen;
    int last_step = (slash == NULL);
    const char *rest = slash ? slash + 1 : NULL;
    size_t rest_len = slash ? plen - step_len - 1 : 0;

    /* "." or empty step: current node is the anchor */
    if (step_len == 0 || (step_len == 1 && path[0] == '.')) {
        if (last_step)
            pin_exec_nodeset_append(esp, ns_idx,
                    pa_fixed_atom_of(pin_node_id_atom_of(start)));
        else
            pin_exec_collect_path(pwp, esp, start, rest, rest_len, ns_idx);
        return;
    }

    /* Named step: intern the name, then walk direct children only */
    char stepbuf[step_len + 1];
    memcpy(stepbuf, path, step_len);
    stepbuf[step_len] = '\0';

    pin_name_id_t step_name = pin_namepool_atom(pwp, stepbuf, FALSE);
    if (pin_name_id_is_null(step_name))
        return;  /* name not in pool -> no such elements exist */

    pin_node_t *node = pin_node_addr(pwp, start);
    if (node == NULL)
        return;

    pin_depth_t target_depth = node->pn_depth + 1;
    for (pin_node_id_t cid = pin_node_child(node);
            !pin_node_id_is_null(cid); ) {
        pin_node_t *child = pin_node_addr(pwp, cid);
        if (child == NULL || child->pn_depth < target_depth)
            break;
        if (child->pn_depth == target_depth
                && child->pn_type == PIN_TYPE_ELT
                && pin_name_id_equal(child->pn_name, step_name)) {
            if (last_step)
                pin_exec_nodeset_append(esp, ns_idx,
                        pa_fixed_atom_of(pin_node_id_atom_of(cid)));
            else
                pin_exec_collect_path(pwp, esp, cid, rest, rest_len, ns_idx);
        }
        cid = child->pn_next;
    }
}

static pin_value_t
pin_op_push_nodes (PIN_OP_FUNC_ARGS)
{
    pin_workspace_t *pwp = pin_parse_workspace(parsep);
    pin_node_id_t ctx = esp->pes_seq[esp->pes_seq_top - 1].psf_context;

    uint32_t ns_idx = pin_exec_nodeset_alloc(esp);
    if (ns_idx == UINT32_MAX) {
        pin_exec_push(esp, pin_value_null());
        return pin_value_null();
    }

    const char *path = pin_namepool_string(pwp, opp->po_name);
    if (path && path[0] != '\0')
        pin_exec_collect_path(pwp, esp, ctx, path, strlen(path), ns_idx);

    pin_exec_push(esp, pin_value_nodeset(ns_idx));
    return pin_value_null();
}

/*
 * XPath boolean() of any value type (XPath section 4.3).
 * For strings: "a string is true if and only if its length is non-zero."
 * Requires pwp to resolve the namepool atom for PVT_STRING.
 */
static int
pin_exec_value_is_true (pin_exec_state_t *esp, pin_value_t v,
			pin_workspace_t *pwp)
{
    if (v.pv_type == PVT_NODESET)
	return pin_exec_nodeset_is_true(esp, v);
    if (v.pv_type == PVT_STRING) {
	if (v.pv_atom == 0)
	    return 0;
	const char *s = pin_namepool_string(pwp, pin_name_id(v.pv_atom));
	return s != NULL && s[0] != '\0';
    }
    return pin_value_is_true(v);
}

static pin_value_t
pin_op_push_bool (PIN_OP_FUNC_ARGS)
{
    pin_workspace_t *pwp = pin_parse_workspace(parsep);
    pin_value_t v = pin_exec_pop(esp);
    pin_exec_push(esp, pin_value_bool(pin_exec_value_is_true(esp, v, pwp)));
    return pin_value_null();
}

static pin_value_t
pin_op_push_node (PIN_OP_FUNC_ARGS)
{
    pin_node_id_t ctx = esp->pes_seq[esp->pes_seq_top - 1].psf_context;
    pin_exec_push(esp, pin_value_node(pa_fixed_atom_of(pin_node_id_atom_of(ctx))));
    return pin_value_null();
}

/* Stub for complex test expressions not yet supported; always evaluates false */
static pin_value_t
pin_op_complex_expr (PIN_OP_FUNC_ARGS)
{
    pin_exec_push(esp, pin_value_bool(0));
    return pin_value_null();
}

static pin_value_t
pin_op_if (PIN_OP_FUNC_ARGS)
{
    pin_workspace_t *pwp = pin_parse_workspace(parsep);
    pin_value_t v = pin_exec_pop(esp);
    int truth = pin_exec_value_is_true(esp, v, pwp);

    if (!truth && esp->pes_seq_top > 0)
        esp->pes_seq[esp->pes_seq_top - 1].psf_pc = opp->po_alt;

    return pin_value_null();
}

static pin_value_t
pin_op_goto (PIN_OP_FUNC_ARGS)
{
    if (esp->pes_seq_top > 0)
        esp->pes_seq[esp->pes_seq_top - 1].psf_pc = opp->po_alt;
    return pin_value_null();
}

static pin_value_t
pin_op_jump (PIN_OP_FUNC_ARGS)
{
    return pin_value_null();
}

static pin_value_t
pin_op_discard (PIN_OP_FUNC_ARGS)
{
    pin_exec_pop(esp);
    return pin_value_null();
}

static pin_value_t
pin_op_store_var (PIN_OP_FUNC_ARGS)
{
    pin_value_t v = pin_exec_pop(esp);
    pin_var_binding_t *vp = pin_exec_var_bind(esp,
            pin_name_id_atom_of(opp->po_name));
    if (vp)
        vp->pvb_value = v;
    return pin_value_null();
}

static pin_value_t
pin_op_set_var (PIN_OP_FUNC_ARGS)
{
    pin_value_t v = pin_exec_pop(esp);
    pin_var_binding_t *vp = pin_exec_var_alloc(esp,
            pin_name_id_atom_of(opp->po_name));
    if (vp)
        vp->pvb_value = v;
    return pin_value_null();
}

static pin_value_t
pin_op_load_var (PIN_OP_FUNC_ARGS)
{
    pin_var_binding_t *vp = pin_exec_var_find(esp,
            pin_name_id_atom_of(opp->po_name));
    if (vp) {
        pin_exec_push(esp, vp->pvb_value);
    } else {
        /* Fall back to global variables registered in the rulebook */
        pin_workspace_t *pwp = pin_parse_workspace(parsep);
        pin_rulebook_t *rb = parsep->pp_rulebook;
        pin_name_id_t gval = pin_rulebook_global_find(rb, opp->po_name);
        if (!pin_name_id_is_null(gval)) {
            pin_exec_push(esp, pin_value_string(pin_name_id_atom_of(gval)));
        } else {
            /*
             * No static value: check for a deferred expression (e.g. "$other").
             * Only $varname references are resolved; other expressions stay null.
             */
            pin_name_id_t expr = pin_rulebook_global_expr_find(rb, opp->po_name);
            pin_value_t result = pin_value_null();
            if (!pin_name_id_is_null(expr)) {
                const char *estr = pin_namepool_string(pwp, expr);
                if (estr && estr[0] == '$') {
                    pin_name_id_t ref = pin_namepool_atom(pwp, estr + 1, FALSE);
                    pin_name_id_t rval = pin_rulebook_global_find(rb, ref);
                    if (!pin_name_id_is_null(rval))
                        result = pin_value_string(pin_name_id_atom_of(rval));
                }
            }
            pin_exec_push(esp, result);
        }
    }
    return pin_value_null();
}

/*
 * Evaluate an AVT (Attribute Value Template) string stored in po_name2.
 * Text outside braces is copied literally; {$var} and {@attr} are expanded.
 * {{ and }} are escapes for literal braces.  The result is pushed as a string.
 */
static pin_value_t
pin_op_push_avt (PIN_OP_FUNC_ARGS)
{
    pin_workspace_t *pwp = pin_parse_workspace(parsep);
    const char *tmpl = pin_namepool_string(pwp, opp->po_name2);
    if (tmpl == NULL) {
        pin_exec_push(esp, pin_value_null());
        return pin_value_null();
    }

    int top = esp->pes_seq_top;
    pin_node_id_t ctx = (top > 0) ? esp->pes_seq[top - 1].psf_context
                                   : pin_node_id_null_atom();
    xo_buffer_t buf;
    xo_buf_init(&buf);

    for (const char *p = tmpl; *p; ) {
        if (p[0] == '{' && p[1] == '{') {
            xo_buf_append(&buf, "{", 1);
            p += 2;
        } else if (p[0] == '}' && p[1] == '}') {
            xo_buf_append(&buf, "}", 1);
            p += 2;
        } else if (p[0] == '{') {
            const char *end = strchr(p + 1, '}');
            if (end == NULL)
                break;
            size_t elen = (size_t)(end - (p + 1));
            char expr[elen + 1];
            memcpy(expr, p + 1, elen);
            expr[elen] = '\0';

            char vbuf[512];
            vbuf[0] = '\0';
            if (expr[0] == '$') {
                /* Variable reference: look up in exec vars then globals */
                pin_name_id_t nid = pin_namepool_atom(pwp, expr + 1, FALSE);
                if (!pin_name_id_is_null(nid)) {
                    pin_var_binding_t *vp = pin_exec_var_find(esp,
                            pin_name_id_atom_of(nid));
                    if (vp) {
                        const char *vs = pin_namepool_string(pwp,
                                pin_name_id(vp->pvb_value.pv_atom));
                        if (vs)
                            snprintf(vbuf, sizeof(vbuf), "%s", vs);
                    } else {
                        pin_rulebook_t *rb = parsep->pp_rulebook;
                        pin_name_id_t gval = pin_rulebook_global_find(rb, nid);
                        if (!pin_name_id_is_null(gval)) {
                            const char *gs = pin_namepool_string(pwp, gval);
                            if (gs)
                                snprintf(vbuf, sizeof(vbuf), "%s", gs);
                        }
                    }
                }
            } else {
                pin_exec_eval_expr_string(pwp, ctx, expr, elen,
                                          vbuf, sizeof(vbuf));
            }
            if (vbuf[0])
                xo_buf_append_str(&buf, vbuf);
            p = end + 1;
        } else {
            xo_buf_append(&buf, p, 1);
            p += 1;
        }
    }

    pin_name_id_t result = pin_name_id_null_atom();
    if (xo_buf_offset(&buf) > 0) {
        xo_buf_append(&buf, "", 1);
        result = pin_namepool_atom(pwp, xo_buf_data(&buf, 0), TRUE);
    }
    xo_buf_cleanup(&buf);

    pin_exec_push(esp, !pin_name_id_is_null(result)
                       ? pin_value_string(pin_name_id_atom_of(result))
                       : pin_value_null());
    return pin_value_null();
}

/* Forward declaration -- defined after the dispatch table */
static int pin_exec_seq_grow (pin_exec_state_t *esp);

/*
 * Push a new frame onto the op-sequence stack.
 * Returns 0 on success, -1 if realloc fails.
 */
int
pin_exec_push_seq_frame (pin_exec_state_t *esp, pin_op_id_t pc,
			 pin_node_id_t ctx)
{
    if (pin_exec_seq_grow(esp) < 0)
        return -1;

    int top = esp->pes_seq_top;
    esp->pes_seq[top].psf_pc = pc;
    esp->pes_seq[top].psf_context = ctx;
    esp->pes_seq[top].psf_is_call = 0;
    esp->pes_seq[top].psf_saved_var_count = esp->pes_var_count;
    esp->pes_seq[top].psf_saved_nodeset_count = esp->pes_nodeset_count;
    esp->pes_seq[top].psf_saved_frag_count = esp->pes_frag_count;
    esp->pes_seq[top].psf_param_base = 0;
    esp->pes_seq[top].psf_param_count = 0;
    esp->pes_seq[top].psf_iter = NULL;
    esp->pes_seq_top += 1;
    return 0;
}

/*
 * Evaluate a simple arithmetic binary expression of the form
 * "lhs OP rhs" where OP is + - * / surrounded by single spaces.
 * Hyphens within element names (like "life-span") do NOT contain spaces
 * so they are never mistaken for minus operators.
 * Returns 1 on success with result in *outp; 0 if not arithmetic.
 */
static int
pin_eval_arith (pin_workspace_t *pwp, pin_node_id_t cid,
                const char *expr, size_t elen, long *outp)
{
    /* Scan for " OP " pattern (spaces required around operator) */
    for (size_t i = 1; i + 2 < elen; i++) {
        if (expr[i] != ' ')
            continue;

        char op = expr[i + 1];
        if ((op != '+' && op != '-' && op != '*' && op != '/')
	    	|| expr[i + 2] != ' ')
            continue;

        /* Found: lhs is expr[0..i-1], rhs is expr[i+3..] */
        size_t llen = i;
        const char *rhs = expr + i + 3;
        size_t rlen = elen - i - 3;
        if (llen == 0 || rlen == 0)
            continue;

        /* Evaluate both sides: first try arithmetic (recursive), then path */
        long lval = 0, rval = 0;
        char lbuf[64] = "", rbuf[64] = "";

        /* Left side */
        long tmp;
        if (pin_eval_arith(pwp, cid, expr, llen, &tmp))
            lval = tmp;
        else {
            pin_node_id_t tgt = pin_exec_node_at_path(pwp, cid, expr, llen);
            if (pin_node_id_is_null(tgt))
                return 0;
            pin_name_id_t tid = pin_exec_text_of(pwp, tgt);
            const char *tx = pin_namepool_string(pwp, tid);
            if (!tx || !tx[0])
                return 0;
            snprintf(lbuf, sizeof(lbuf), "%s", tx);
            char *endp;
            lval = strtol(lbuf, &endp, 10);
            if (endp == lbuf)
                return 0;
        }

        /* Right side */
        if (pin_eval_arith(pwp, cid, rhs, rlen, &tmp))
            rval = tmp;
        else {
            pin_node_id_t tgt = pin_exec_node_at_path(pwp, cid, rhs, rlen);
            if (pin_node_id_is_null(tgt))
                return 0;
            pin_name_id_t tid = pin_exec_text_of(pwp, tgt);
            const char *tx = pin_namepool_string(pwp, tid);
            if (!tx || !tx[0])
                return 0;
            snprintf(rbuf, sizeof(rbuf), "%s", tx);
            char *endp;
            rval = strtol(rbuf, &endp, 10);
            if (endp == rbuf)
                return 0;
        }

        switch (op) {
        case '+': *outp = lval + rval; break;
        case '-': *outp = lval - rval; break;
        case '*': *outp = lval * rval; break;
        case '/': *outp = rval ? lval / rval : 0; break;
        }
        return 1;
    }
    return 0;
}

/*
 * Evaluate a single sort-key expression against child_id, writing the
 * raw value into valbuf[vcap].  Supported forms:
 *   @attr            -- attribute value
 *   .                -- text content of context node itself
 *   a/b/c/...        -- text content of node reached by walking the path steps
 *   path1 OP path2   -- arithmetic on text values (OP = + - * /)
 * Arithmetic results are zero-padded to 6 digits for lexicographic sort.
 * Unknown forms write an empty string.
 */
static void
pin_for_each_eval_expr (pin_workspace_t *pwp, pin_node_id_t cid,
                        const char *expr, size_t elen,
                        char *valbuf, size_t vcap)
{
    valbuf[0] = '\0';
    if (elen == 0 || vcap == 0)
        return;

    /* Arithmetic expression: format with leading zeros for sort correctness */
    long arith_result;
    if (pin_eval_arith(pwp, cid, expr, elen, &arith_result)) {
        snprintf(valbuf, vcap, "%06ld", arith_result);
        return;
    }

    if (expr[0] == '@') {
        size_t n = elen - 1;
        char attrname[elen];
        memcpy(attrname, expr + 1, n);
        attrname[n] = '\0';

        pin_name_id_t aid = pin_namepool_atom(pwp, attrname, FALSE);
        pin_node_t *nodep = pin_node_addr(pwp, cid);
        if (nodep && !pin_name_id_is_null(aid)) {
            const char *val = pin_get_attrib_string(pwp, nodep, aid);
            if (val)
                snprintf(valbuf, vcap, "%s", val);
        }
        return;
    }

    pin_node_id_t target = pin_exec_node_at_path(pwp, cid, expr, elen);
    if (pin_node_id_is_null(target))
        return;

    pin_name_id_t text_id = pin_exec_text_of(pwp, target);
    const char *text = pin_namepool_string(pwp, text_id);
    if (text)
        snprintf(valbuf, vcap, "%s", text);
}

/*
 * Evaluate a general expression (path or arithmetic) against ctx_node.
 * Writes the result as a natural string (no zero-padding) into buf[bufsz].
 * Used for xsl:variable and attribute-value expressions.
 */
void
pin_exec_eval_expr_string (pin_workspace_t *pwp, pin_node_id_t ctx_node,
                           const char *expr, size_t elen,
                           char *buf, size_t bufsz)
{
    buf[0] = '\0';
    if (elen == 0 || bufsz == 0 || pin_node_id_is_null(ctx_node))
        return;

    long arith_result;
    if (pin_eval_arith(pwp, ctx_node, expr, elen, &arith_result)) {
        snprintf(buf, bufsz, "%ld", arith_result);
        return;
    }

    if (elen > 1 && expr[0] == '@') {
        pin_node_t *nodep = pin_node_addr(pwp, ctx_node);
        if (nodep) {
            char aname[elen];
            memcpy(aname, expr + 1, elen - 1);
            aname[elen - 1] = '\0';
            pin_name_id_t anid = pin_namepool_atom(pwp, aname, FALSE);
            if (!pin_name_id_is_null(anid)) {
                const char *val = pin_get_attrib_string(pwp, nodep, anid);
                if (val)
                    snprintf(buf, bufsz, "%s", val);
            }
        }
        return;
    }

    /* "path/@name": the attribute of the node at path */
    const char *at = NULL;
    for (const char *cp = expr + elen; cp > expr; cp--) {
        if (cp[-1] == '/') {
            if (cp[0] == '@')
                at = cp;
            break;
        }
    }
    if (at != NULL) {
        pin_node_id_t owner = pin_exec_node_at_path(pwp, ctx_node, expr,
                                                    (size_t) (at - expr - 1));
        pin_node_t *nodep = pin_node_addr(pwp, owner);
        size_t alen = elen - (size_t) (at - expr) - 1;
        if (nodep && alen > 0) {
            char aname[alen + 1];
            memcpy(aname, at + 1, alen);
            aname[alen] = '\0';
            pin_name_id_t anid = pin_namepool_atom(pwp, aname, FALSE);
            if (!pin_name_id_is_null(anid)) {
                const char *val = pin_get_attrib_string(pwp, nodep, anid);
                if (val)
                    snprintf(buf, bufsz, "%s", val);
            }
        }
        return;
    }

    pin_node_id_t target = pin_exec_node_at_path(pwp, ctx_node, expr, elen);
    if (!pin_node_id_is_null(target)) {
        pin_name_id_t tid = pin_exec_text_of(pwp, target);
        const char *text = pin_namepool_string(pwp, tid);
        if (text)
            snprintf(buf, bufsz, "%s", text);
    }
}

/*
 * Parse val as an XPath number (optional whitespace, optional '-',
 * digits with an optional '.' fraction, and, as libxslt allows, an
 * optional exponent).  Returns false for anything else, which
 * XPath's number() turns into NaN.
 */
static bool
pin_sort_parse_number (const char *val, double *dp)
{
    const char *p = val;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p += 1;

    const char *start = p;
    if (*p == '-')
        p += 1;

    int digits = 0;
    while (*p >= '0' && *p <= '9') {
        digits += 1;
        p += 1;
    }
    if (*p == '.') {
        p += 1;
        while (*p >= '0' && *p <= '9') {
            digits += 1;
            p += 1;
        }
    }
    if (digits == 0)
        return false;

    /* libxslt also accepts an exponent ("1e3"); match it */
    if (*p == 'e' || *p == 'E') {
        const char *q = p + 1;
        if (*q == '+' || *q == '-')
            q += 1;
        if (*q >= '0' && *q <= '9') {
            while (*q >= '0' && *q <= '9')
                q += 1;
            p = q;
        }
    }

    const char *end = p;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p += 1;
    if (*p != '\0')
        return false;

    char nbuf[end - start + 1];
    memcpy(nbuf, start, end - start);
    nbuf[end - start] = '\0';
    *dp = strtod(nbuf, NULL);
    return true;
}

/*
 * Encode a numeric sort field so that byte order equals numeric order:
 * map the IEEE double to a uint64 (flip all bits if negative, else
 * flip the sign bit) and write it as 16 uppercase hex digits.  A
 * non-number (NaN) is written as an empty field, so it sorts before
 * every number, as XSLT 1.0 requires for ascending order.
 */
static size_t
pin_sort_encode_number (char *keybuf, size_t keycap, size_t pos,
                        const char *val)
{
    double d;
    if (!pin_sort_parse_number(val, &d))
        return pos;

    if (d == 0)
        d = 0;                  /* Fold -0 into +0 */

    uint64_t bits;
    memcpy(&bits, &d, sizeof(bits));
    if (bits & (1ULL << 63))
        bits = ~bits;
    else
        bits ^= (1ULL << 63);

    static const char hex[] = "0123456789ABCDEF";
    for (int shift = 60; shift >= 0 && pos < keycap; shift -= 4)
        keybuf[pos++] = hex[(bits >> shift) & 0xf];

    return pos;
}

/*
 * The ops apply-templates runs for "child" in "mode", or the null atom
 * if it has none.  A child with no matching rule is discarded, like the
 * live PBMODE_APPLY dispatch does.
 *
 * pr_close_ops alone isn't enough: a rule that fell back to BIA_
 * (pr_body) or a for-each state (pr_new_state) can still have a stale
 * pr_close_ops from an abandoned op compile, so this makes the same
 * three-way check as pin_parse_handle_rule.  The op engine runs against
 * an already-retained subtree and has no live event stream to feed a
 * BIA_ body, so such a child gets a warning and is skipped.
 */
static pin_op_id_t
pin_exec_apply_ops (pin_workspace_t *pwp, pin_rulebook_t *rb,
                    pin_name_id_t mode, pin_node_t *child)
{
    pin_rule_t *rule = pin_rulebook_apply_find(rb, mode, child->pn_name);
    if (rule == NULL)
        return pin_op_id_null_atom();

    if (!pin_op_id_is_null(rule->pr_close_ops)
            && pin_body_instr_id_is_null(rule->pr_body)
            && pin_rstate_id_is_null(rule->pr_new_state))
        return rule->pr_close_ops;

    psu_log("pin_exec: apply-templates: matched template for \"%s\" "
            "needs apply-templates/copy-of/for-each, which the "
            "retained-subtree op engine does not support yet; skipping",
            pin_namepool_string(pwp, child->pn_name));
    return pin_op_id_null_atom();
}

/*
 * The next item of a loop, or the null atom when there are no more.
 * *pcp is set to the ops to run for it.
 */
/*
 * Does a child belong to a PEIK_CHILD or PEIK_APPLY loop?  An apply
 * loop takes the children that have a template, and *pcp gets its ops.
 */
static pin_boolean_t
pin_exec_iter_wants (pin_workspace_t *pwp, struct pin_parse_s *parsep,
                     pin_exec_iter_t *iterp, pin_node_t *child,
                     pin_op_id_t *pcp)
{
    if (child->pn_type != PIN_TYPE_ELT)
        return FALSE;

    if (iterp->pei_kind == PEIK_CHILD)
        return pin_name_id_equal(child->pn_name, iterp->pei_name);

    *pcp = pin_exec_apply_ops(pwp, parsep->pp_rulebook, iterp->pei_name,
                              child);
    return !pin_op_id_is_null(*pcp);
}

static pin_node_id_t
pin_exec_iter_next_item (pin_exec_state_t *esp, struct pin_parse_s *parsep,
                         pin_exec_iter_t *iterp, pin_op_id_t *pcp)
{
    pin_workspace_t *pwp = pin_parse_workspace(parsep);

    *pcp = iterp->pei_body;

    switch (iterp->pei_kind) {
    case PEIK_NODESET:;
        pin_ns_entry_t *nsp = &esp->pes_nodesets[iterp->pei_ns];
        if (iterp->pei_pos >= nsp->pne_count)
            break;

        uint32_t pos = iterp->pei_pos;
        iterp->pei_pos += 1;
        return pin_node_id(nsp->pne_nodes[pos]);

    case PEIK_SORTED:;
        pin_stree_entry_t *ep = iterp->pei_entry;
        if (ep == NULL)
            break;

        iterp->pei_entry = iterp->pei_desc
            ? pin_stree_prev(iterp->pei_tree, ep)
            : pin_stree_next(iterp->pei_tree, ep);
        return ep->pse_node;

    case PEIK_CHILD:
    case PEIK_APPLY:
        /*
         * The last child's pn_next points back at the parent, so the
         * walk ends on depth, not on a null link.
         */
        while (!pin_node_id_is_null(iterp->pei_next)) {
            pin_node_id_t cid = iterp->pei_next;
            pin_node_t *child = pin_node_addr(pwp, cid);
            if (child == NULL || child->pn_depth <= iterp->pei_depth)
                break;

            iterp->pei_next = child->pn_next;
            if (pin_exec_iter_wants(pwp, parsep, iterp, child, pcp))
                return cid;
        }
        iterp->pei_next = pin_node_id_null_atom();
        break;
    }

    return pin_node_id_null_atom();
}

static pin_node_id_t
pin_exec_iter_next (pin_exec_state_t *esp, struct pin_parse_s *parsep,
                    pin_exec_iter_t *iterp, pin_op_id_t *pcp)
{
    pin_node_id_t item = pin_exec_iter_next_item(esp, parsep, iterp, pcp);
    if (!pin_node_id_is_null(item))
        iterp->pei_count += 1;

    return item;
}

/*
 * The loop's size, for last().  A child or apply loop counts what's
 * left of its walk the first time it's asked.
 */
static uint32_t
pin_exec_iter_size (pin_exec_state_t *esp, struct pin_parse_s *parsep,
                    pin_exec_iter_t *iterp)
{
    if (iterp->pei_size != 0)
        return iterp->pei_size;

    pin_workspace_t *pwp = pin_parse_workspace(parsep);
    uint32_t size = iterp->pei_count;

    switch (iterp->pei_kind) {
    case PEIK_NODESET:
        size = esp->pes_nodesets[iterp->pei_ns].pne_count;
        break;

    case PEIK_CHILD:
    case PEIK_APPLY:
        for (pin_node_id_t cid = iterp->pei_next; !pin_node_id_is_null(cid); ) {
            pin_node_t *child = pin_node_addr(pwp, cid);
            if (child == NULL || child->pn_depth <= iterp->pei_depth)
                break;

            pin_op_id_t pc;
            if (pin_exec_iter_wants(pwp, parsep, iterp, child, &pc))
                size += 1;
            cid = child->pn_next;
        }
        break;
    }

    iterp->pei_size = size;
    return size;
}

/*
 * The XPath context for an expression run in the top frame: its
 * context node, and the position and size from the loop that gave
 * that node.  A call keeps its caller's context, so the walk passes
 * through call frames.  Outside any loop, the node is alone: 1 of 1.
 */
static void
pin_exec_eval_ctx (pin_exec_state_t *esp, struct pin_parse_s *parsep,
                   xo_eval_ctx_t *ctxp)
{
    int top = esp->pes_seq_top - 1;
    pin_node_id_t ctx = esp->pes_seq[top].psf_context;

    ctxp->xec_node = pin_filter_eval_node(ctx);
    ctxp->xec_position = 1;
    ctxp->xec_size = 1;

    for (int i = top; i >= 0; i--) {
        pin_exec_seq_frame_t *frame = &esp->pes_seq[i];
        pin_exec_iter_t *iterp = frame->psf_iter;

        if (iterp != NULL) {
            if (pin_node_id_equal(frame->psf_context, ctx)) {
                ctxp->xec_position = iterp->pei_count;
                ctxp->xec_size = pin_exec_iter_size(esp, parsep, iterp);
            }
            break;
        }

        if (!frame->psf_is_call)
            break;
    }
}

static void
pin_exec_iter_free (pin_exec_state_t *esp, pin_exec_iter_t *iterp)
{
    if (iterp->pei_kind == PEIK_NODESET)
        pin_exec_nodeset_free(esp, iterp->pei_ns);
    else if (iterp->pei_kind == PEIK_SORTED)
        pin_stree_free(iterp->pei_tree);

    free(iterp);
}

/*
 * Start a loop: push one frame for its first item, which keeps
 * "iterp" and takes each next item from it.  The frame frees it when
 * the loop ends.
 */
static void
pin_exec_push_iter (pin_exec_state_t *esp, struct pin_parse_s *parsep,
                    pin_exec_iter_t *iterp)
{
    pin_op_id_t pc;
    pin_node_id_t item = pin_exec_iter_next(esp, parsep, iterp, &pc);

    if (pin_node_id_is_null(item)
            || pin_exec_push_seq_frame(esp, pc, item) < 0) {
        pin_exec_iter_free(esp, iterp);
        return;
    }

    esp->pes_seq[esp->pes_seq_top - 1].psf_iter = iterp;
}

/*
 * Build the sort key for child element child_id using the sort-key spec
 * string spec.  Each newline-separated key may carry leading flags:
 * '-' (descending), '^' (upper-first) and '#' (data-type="number").
 * Writes the encoded, NUL-separated key into keybuf[keycap], followed
 * by the four-byte big-endian ordinal seq, so nodes with equal keys
 * stay distinct in the tree and keep document order.  When the
 * primary key is descending the tree is walked backwards, so seq is
 * inverted to keep ties in document order.  Returns the total key
 * length in bytes.
 */
uint16_t
pin_for_each_build_key (pin_workspace_t *pwp, pin_node_id_t child_id,
                        const char *spec, uint32_t seq,
                        char *keybuf, size_t keycap)
{
    size_t pos = 0;
    const char *p = spec;
    bool primary_desc = (spec && *spec == '-');

    while (p && *p) {
        bool upper = false;
        bool number = false;
        while (*p == '-' || *p == '^' || *p == '#') {
            if (*p == '^')
                upper = true;
            else if (*p == '#')
                number = true;
            p += 1;
        }

        const char *end = strchr(p, '\n');
        size_t elen = end ? (size_t)(end - p) : strlen(p);

        char valbuf[256];
        pin_for_each_eval_expr(pwp, child_id, p, elen, valbuf, sizeof(valbuf));

        if (number)
            pos = pin_sort_encode_number(keybuf, keycap, pos, valbuf);
        else
            pos = pin_stree_encode_key(keybuf, keycap, pos, valbuf, upper);

        if (pos < keycap)
            keybuf[pos++] = '\0';

        p = end ? end + 1 : NULL;
    }

    if (primary_desc)
        seq = ~seq;
    for (int shift = 24; shift >= 0 && pos < keycap; shift -= 8)
        keybuf[pos++] = (char) ((seq >> shift) & 0xff);

    return (uint16_t) pos;
}

static pin_value_t
pin_op_for_each (PIN_OP_FUNC_ARGS)
{
    /*
     * A stacked selection is a fresh slot from PIN_OP_EVAL_VALUE,
     * which the loop owns.  Selecting anything but nodes is an error
     * in XSLT; here it selects nothing.
     */
    uint32_t sel_ns = UINT32_MAX;
    if (opp->po_count) {
        pin_value_t v = pin_exec_pop(esp);
        if (v.pv_type == PVT_NODESET)
            sel_ns = v.pv_atom;
        else if (v.pv_type != PVT_NULL)
            psu_log("pin_exec: for-each: select is not a node-set");
    }

    pin_workspace_t *pwp = pin_parse_workspace(parsep);
    pin_node_id_t parent_id = esp->pes_seq[esp->pes_seq_top - 1].psf_context;
    pin_node_t *parent = pin_node_addr(pwp, parent_id);
    if (pin_op_id_is_null(opp->po_alt) || parent == NULL
            || (opp->po_count && sel_ns == UINT32_MAX)) {
        if (sel_ns != UINT32_MAX)
            pin_exec_nodeset_free(esp, sel_ns);
        return pin_value_null();
    }

    const char *spec = pin_namepool_string(pwp, opp->po_name2);
    bool has_sort = (spec && *spec);

    bool primary_desc = false;
    if (has_sort) {
        const char *sp = spec;
        while (*sp == '-' || *sp == '^' || *sp == '#') {
            if (*sp == '-') primary_desc = true;
            sp += 1;
        }
    }

    pin_exec_iter_t *iterp = calloc(1, sizeof(*iterp));
    if (iterp == NULL)
        return pin_value_null();

    iterp->pei_body = opp->po_alt;
    iterp->pei_name = opp->po_name;

    /*
     * A single step is matched against the context node's children as
     * the loop goes.  A multi-step path ("a/b/c") is collected in
     * document order by pin_exec_collect_path(), and a sorted loop
     * needs all its items before the first one runs.
     */
    const char *path = pin_namepool_string(pwp, opp->po_name);
    if (sel_ns != UINT32_MAX) {
        iterp->pei_ns = sel_ns;
        iterp->pei_kind = PEIK_NODESET;
    } else if (path && strchr(path, '/') != NULL) {
        iterp->pei_ns = pin_exec_nodeset_alloc(esp);
        if (iterp->pei_ns == UINT32_MAX) {
            free(iterp);
            return pin_value_null();
        }
        pin_exec_collect_path(pwp, esp, parent_id, path, strlen(path),
                              iterp->pei_ns);
        iterp->pei_kind = PEIK_NODESET;
    } else {
        iterp->pei_kind = PEIK_CHILD;
        iterp->pei_depth = parent->pn_depth;
        iterp->pei_next = pin_node_child(parent);
    }

    if (has_sort) {
        pin_stree_t *tree = pin_stree_create();
        if (tree == NULL) {
            pin_exec_iter_free(esp, iterp);
            return pin_value_null();
        }

        uint32_t key_seq = 0;
        for (;;) {
            pin_op_id_t pc;
            pin_node_id_t match_id = pin_exec_iter_next(esp, parsep, iterp,
                                                        &pc);
            if (pin_node_id_is_null(match_id))
                break;

            char keybuf[512];
            uint16_t klen = pin_for_each_build_key(pwp, match_id, spec,
                                                   key_seq, keybuf,
                                                   sizeof(keybuf));
            if (pin_stree_insert(tree, keybuf, klen, match_id) < 0) {
                psu_log("pin_exec: for-each: can't sort more than %u items",
                        key_seq);
                break;
            }
            key_seq += 1;
        }

        if (iterp->pei_kind == PEIK_NODESET)
            pin_exec_nodeset_free(esp, iterp->pei_ns);

        iterp->pei_count = 0;
        iterp->pei_size = key_seq;
        iterp->pei_kind = PEIK_SORTED;
        iterp->pei_tree = tree;
        iterp->pei_desc = primary_desc;
        iterp->pei_entry = primary_desc ? pin_stree_last(tree)
            : pin_stree_first(tree);
    }

    pin_exec_push_iter(esp, parsep, iterp);
    return pin_value_null();
}

/*
 * Apply templates to every element child of the context node, in
 * document order.  Each child runs its own rule's ops, found by
 * pin_exec_apply_ops.
 *
 * Not yet handled: select= on the apply-templates itself, xsl:sort,
 * and the XSLT built-in template fallback (recursing into an unmatched
 * element's own children), so an unmatched element's descendants are
 * never visited.
 */
static pin_value_t
pin_op_apply (PIN_OP_FUNC_ARGS)
{
    pin_workspace_t *pwp = pin_parse_workspace(parsep);
    pin_node_id_t parent_id = esp->pes_seq[esp->pes_seq_top - 1].psf_context;
    pin_node_t *parent = pin_node_addr(pwp, parent_id);
    if (parent == NULL)
        return pin_value_null();

    pin_exec_iter_t *iterp = calloc(1, sizeof(*iterp));
    if (iterp == NULL)
        return pin_value_null();

    iterp->pei_kind = PEIK_APPLY;
    iterp->pei_name = opp->po_name;
    iterp->pei_depth = parent->pn_depth;
    iterp->pei_next = pin_node_child(parent);

    pin_exec_push_iter(esp, parsep, iterp);
    return pin_value_null();
}

/*
 * Recursively emit one node and its full subtree to the output stream.  Children are walked via pin_node_child + pn_next
 * (sibling chain), never pn_next of the root, which links to siblings.
 */
void
pin_exec_emit_node (pin_parse_t *parsep, pin_workspace_t *pwp,
                    pin_node_id_t nid)
{
    pin_node_t *nodep = pin_node_addr(pwp, nid);
    if (nodep == NULL)
        return;

    pin_insert_t *pip = parsep->pp_insert;

    if (nodep->pn_type == PIN_TYPE_ELT) {
        char abuf[4096];
        size_t apos = 0;
        pin_depth_t elt_depth = nodep->pn_depth;
        const char *tag = pin_namepool_string(pwp, nodep->pn_name);

        for (pin_node_id_t aid = pin_node_child(nodep);
                !pin_node_id_is_null(aid); ) {
            pin_node_t *ap = pin_node_addr(pwp, aid);
            if (ap == NULL || ap->pn_depth <= elt_depth)
                break;
            if (ap->pn_type == PIN_TYPE_ATTRIB) {
                const char *aname = pin_namepool_string(pwp, ap->pn_name);
                const char *aval = pin_textpool_string(pwp,
                        pa_arb_atom_of(pin_node_text(ap)));
                if (aname && aval) {
                    if (apos > 0 && apos < sizeof(abuf) - 1)
                        abuf[apos++] = ' ';
                    int n = snprintf(abuf + apos, sizeof(abuf) - apos,
                            "%s=\"%s\"", aname, aval);
                    if (n > 0)
                        apos += (size_t) n;
                }
            }
            aid = ap->pn_next;
        }

        char *attribs = apos > 0 ? strndup(abuf, apos) : NULL;
        pin_action_type_t act = pin_insert_top_frame(pip)->ps_action;
        if (attribs)
            act = PIA_SAVE_ATTRIB;
        pin_insert_open(parsep, nodep->pn_name, NULL, tag, attribs, act);
        free(attribs);

        for (pin_node_id_t cid = pin_node_child(nodep);
                !pin_node_id_is_null(cid); ) {
            pin_node_t *child = pin_node_addr(pwp, cid);
            if (child == NULL || child->pn_depth <= elt_depth)
                break;
            if (child->pn_type != PIN_TYPE_ATTRIB)
                pin_exec_emit_node(parsep, pwp, cid);
            cid = child->pn_next;
        }

        pin_insert_close(parsep, NULL, tag);

    } else if (nodep->pn_type == PIN_TYPE_ATTRIB) {
        /* A copied attribute goes on the open output element */
        const char *aname = pin_namepool_string(pwp, nodep->pn_name);
        const char *aval = pin_textpool_string(pwp,
                pa_arb_atom_of(pin_node_text(nodep)));
        if (aname)
            pin_insert_attrib_pair(parsep, aname, aval ? aval : "");

    } else if (nodep->pn_type == PIN_TYPE_TEXT
            || nodep->pn_type == PIN_TYPE_UNESC) {
        const char *text = pin_textpool_string(pwp,
                pa_arb_atom_of(pin_node_text(nodep)));
        if (text)
            pin_insert_text(parsep, text, strlen(text), nodep->pn_type);
    } else if (nodep->pn_type == PIN_TYPE_COMMENT
            || nodep->pn_type == PIN_TYPE_PI) {
        const char *text = pin_textpool_string(pwp, nodep->pn_contents);
        if (text)
            pin_insert_text(parsep, text, strlen(text), nodep->pn_type);
    } else if (nodep->pn_type == PIN_TYPE_ROOT) {
        /*
         * A fragment emits its children.  Stop at the child that was
         * last when we started, since a fragment can be appended to
         * itself.
         */
        pin_depth_t root_depth = nodep->pn_depth;
        pin_node_id_t last = pin_node_id_null_atom();
        for (pin_node_id_t cid = pin_node_child(nodep);
                !pin_node_id_is_null(cid); ) {
            pin_node_t *child = pin_node_addr(pwp, cid);
            if (child == NULL || child->pn_depth <= root_depth)
                break;
            last = cid;
            cid = child->pn_next;
        }

        for (pin_node_id_t cid = pin_node_child(nodep);
                !pin_node_id_is_null(last) && !pin_node_id_is_null(cid); ) {
            pin_node_t *child = pin_node_addr(pwp, cid);
            if (child == NULL || child->pn_depth <= root_depth)
                break;
            pin_node_id_t next = child->pn_next;
            pin_exec_emit_node(parsep, pwp, cid);
            if (pin_node_id_equal(cid, last))
                break;
            cid = next;
        }
    }
}

static void
pin_exec_emit_subtree (pin_parse_t *parsep, pin_node_id_t src_id,
                       pin_depth_t stop_depth UNUSED)
{
    if (pin_node_id_is_null(src_id))
        return;
    pin_workspace_t *pwp = pin_parse_workspace(parsep);
    pin_exec_emit_node(parsep, pwp, src_id);
}

/*
 * Copy a value to the output, as xsl:copy-of does: a node-set is
 * deep-copied node by node; any other value is converted to its
 * string value and inserted as a text node.
 */
static void
pin_exec_copy_value (pin_exec_state_t *esp, struct pin_parse_s *parsep,
                     pin_value_t v, pin_depth_t stop_depth)
{
    pin_workspace_t *pwp = pin_parse_workspace(parsep);
    const char *str = NULL;

    switch (v.pv_type) {
    case PVT_NODESET:
        if (v.pv_atom < (uint32_t) esp->pes_nodeset_count) {
            pin_ns_entry_t *nsp = &esp->pes_nodesets[v.pv_atom];
            for (uint32_t i = 0; i < nsp->pne_count; i++)
                pin_exec_emit_subtree(parsep, pin_node_id(nsp->pne_nodes[i]),
                                      stop_depth);
        }
        break;

    case PVT_NODE:
        pin_exec_emit_subtree(parsep, pin_node_id(v.pv_atom), stop_depth);
        break;

    case PVT_STRING:
        str = pin_namepool_string(pwp, pin_name_id(v.pv_atom));
        break;

    case PVT_BOOLEAN:
        str = (v.pv_flags & PVF_TRUE) ? "true" : "false";
        break;

    default:
        break;
    }

    if (str && *str)
        pin_insert_text(parsep, str, strlen(str), PIN_TYPE_TEXT);
}

static pin_value_t
pin_op_copy_of (PIN_OP_FUNC_ARGS)
{
    pin_workspace_t *pwp = pin_parse_workspace(parsep);
    pin_node_id_t ctx = esp->pes_seq[esp->pes_seq_top - 1].psf_context;
    pin_node_t *ctx_node = pin_node_addr(pwp, ctx);
    pin_depth_t stop_depth = ctx_node ? ctx_node->pn_depth - 1 : 0;

    /* A stacked value is a fresh slot from PIN_OP_EVAL_VALUE */
    if (opp->po_count) {
        pin_value_t v = pin_exec_pop(esp);
        pin_exec_copy_value(esp, parsep, v, stop_depth);
        if (v.pv_type == PVT_NODESET)
            pin_exec_nodeset_free(esp, v.pv_atom);
        return pin_value_null();
    }

    const char *path = pin_namepool_string(pwp, opp->po_name);
    if (path && path[0] == '$') {
        pin_name_id_t vname = pin_namepool_atom(pwp, path + 1, FALSE);
        pin_var_binding_t *vp = pin_exec_var_find(esp,
                pin_name_id_atom_of(vname));
        if (vp)
            pin_exec_copy_value(esp, parsep, vp->pvb_value, stop_depth);
        return pin_value_null();
    }

    /* Path or null (meaning "."): collect into an ephemeral nodeset */
    uint32_t ns_idx = pin_exec_nodeset_alloc(esp);
    if (ns_idx == UINT32_MAX)
        return pin_value_null();

    if (path && path[0] != '\0' && strcmp(path, ".") != 0)
        pin_exec_collect_path(pwp, esp, ctx, path, strlen(path), ns_idx);
    else
        pin_exec_nodeset_append(esp, ns_idx,
                                pa_fixed_atom_of(pin_node_id_atom_of(ctx)));

    pin_exec_copy_value(esp, parsep, pin_value_nodeset(ns_idx), stop_depth);
    pin_exec_nodeset_free(esp, ns_idx);
    return pin_value_null();
}

/*
 * Append {name_atom, v} to the pending-param staging buffer, growing it
 * as needed.  Shared by PIN_OP_WITH_PARAM and BIA_WITH_PARAM (pin_parse.c)
 * so both xsl:with-param paths stage params identically for the next
 * PIN_OP_CALL / pin_exec_run_call().  Returns 0 on success, -1 on OOM.
 */
int
pin_exec_stage_pending_param (pin_exec_state_t *esp, uint32_t name_atom,
			      pin_value_t v)
{
    if (esp->pes_pending_count >= esp->pes_pending_size) {
        uint32_t newsize = esp->pes_pending_size ? esp->pes_pending_size * 2 : 8;
        pin_pending_param_t *np = realloc(esp->pes_pending,
                                          newsize * sizeof(*np));
        if (np == NULL)
            return -1;
        esp->pes_pending = np;
        esp->pes_pending_size = newsize;
    }
    esp->pes_pending[esp->pes_pending_count].ppp_name  = name_atom;
    esp->pes_pending[esp->pes_pending_count].ppp_value = v;
    esp->pes_pending_count += 1;
    return 0;
}

static pin_value_t
pin_op_with_param (PIN_OP_FUNC_ARGS)
{
    pin_value_t v = pin_exec_pop(esp);
    uint32_t name_atom = pin_name_id_atom_of(opp->po_name);

    pin_exec_stage_pending_param(esp, name_atom, v);
    return pin_value_null();
}

/*
 * Find the nearest CALL frame on the seq stack (search backwards from top).
 * Returns NULL if no CALL frame is found.
 */
static pin_exec_seq_frame_t *
pin_exec_find_call_frame (pin_exec_state_t *esp)
{
    for (int i = esp->pes_seq_top - 1; i >= 0; i--) {
        if (esp->pes_seq[i].psf_is_call)
            return &esp->pes_seq[i];
    }
    return NULL;
}

static pin_value_t
pin_op_load_param (PIN_OP_FUNC_ARGS)
{
    uint32_t name_atom = pin_name_id_atom_of(opp->po_name);
    pin_exec_seq_frame_t *frame = pin_exec_find_call_frame(esp);

    pin_value_t val = pin_value_null();
    int found = 0;

    if (frame != NULL) {
        for (uint32_t i = frame->psf_param_base;
                i < frame->psf_param_base + frame->psf_param_count; i++) {
            if (esp->pes_pending[i].ppp_name == name_atom) {
                val = esp->pes_pending[i].ppp_value;
                found = 1;
                break;
            }
        }
    }

    pin_exec_push(esp, val);
    pin_exec_push(esp, pin_value_bool(found));
    return pin_value_null();
}

static pin_value_t
pin_op_call (PIN_OP_FUNC_ARGS)
{
    pin_rulebook_t *prbp = parsep->pp_rulebook;
    pin_op_id_t callee_ops = pin_rulebook_named_find(prbp, opp->po_name);
    if (pin_op_id_is_null(callee_ops))
        return pin_value_null();

    if (pin_exec_seq_grow(esp) < 0)
        return pin_value_null();

    uint32_t nparams = opp->po_count;
    uint32_t param_base = (esp->pes_pending_count >= nparams)
        ? esp->pes_pending_count - nparams : 0;

    int top = esp->pes_seq_top;
    pin_node_id_t ctx = esp->pes_seq[top > 0 ? top - 1 : 0].psf_context;
    esp->pes_seq[top].psf_pc = callee_ops;
    esp->pes_seq[top].psf_context = ctx;
    esp->pes_seq[top].psf_is_call = 1;
    esp->pes_seq[top].psf_saved_var_count = esp->pes_var_count;
    esp->pes_seq[top].psf_saved_nodeset_count = esp->pes_nodeset_count;
    esp->pes_seq[top].psf_saved_frag_count = esp->pes_frag_count;
    esp->pes_seq[top].psf_param_base = param_base;
    esp->pes_seq[top].psf_param_count = nparams;
    esp->pes_seq[top].psf_iter = NULL;
    esp->pes_seq_top += 1;
    return pin_value_null();
}

/*
 * Get PIN_OP_EVAL's program ready to run: point the shared filter at
 * this exec state and this program's own namespace bindings, and fill
 * in the context triple.  Returns NULL when there's nothing to run.
 */
static xo_eval_prog_t *
pin_exec_eval_setup (pin_exec_state_t *esp, struct pin_parse_s *parsep,
		     pin_op_t *opp, xo_eval_ctx_t *ctxp)
{
    pin_rulebook_t *rb = parsep->pp_rulebook;
    uint32_t idx = pin_name_id_atom_of(opp->po_name);
    xo_eval_prog_t *prog = (idx < rb->prb_eval_prog_count)
			  ? rb->prb_eval_progs[idx] : NULL;

    if (prog == NULL || rb->prb_eval_xfp == NULL)
	return NULL;

    xo_filter_t *xfp = rb->prb_eval_xfp;
    pin_filter_set_esp(xfp, esp);
    pin_exec_eval_ctx(esp, parsep, ctxp);

    /*
     * xfdo_find_function is consulted again at run time for a prefixed
     * call, and prb_eval_xfp is shared by every program in the
     * rulebook, so each run needs its own program's bindings back.
     */
    uint32_t ns_count = 0;
    const pin_ns_map_t *ns_bindings = pin_rulebook_eval_prog_ns(rb, idx,
								   &ns_count);
    pin_filter_set_compile_ns(xfp, ns_bindings, ns_count);

    return prog;
}

/*
 * Run the compiled xo_eval_prog_t that po_name indexes into
 * parsep->pp_rulebook's prb_eval_progs[] (a plain array index, not a
 * namepool atom -- see pin_rulebook_eval_prog_add) against the current
 * context node and the active call frame's local variables (resolved via
 * pin_filter_data_variable_of, through the rulebook's shared eval filter).
 * Pushes PVT_BOOLEAN for a native boolean result (comparisons, boolean
 * functions), PVT_STRING for everything else, or PVT_NULL if the
 * expression's value is missing (e.g. an unbound variable).
 */
static pin_value_t
pin_op_eval (PIN_OP_FUNC_ARGS)
{
    pin_workspace_t *pwp = pin_parse_workspace(parsep);
    xo_eval_ctx_t ctx;
    xo_eval_prog_t *prog = pin_exec_eval_setup(esp, parsep, opp, &ctx);
    pin_value_t result = pin_value_null();

    if (prog == NULL) {
	pin_exec_push(esp, result);
	return pin_value_null();
    }

    xo_filter_t *xfp = parsep->pp_rulebook->prb_eval_xfp;
    struct xo_xparse_data_s *xdp = xo_filter_xparse_data(NULL, xfp);

    int is_boolean = 0, bool_value = 0, is_missing = 0;
    char *str = xo_eval_prog_run_ctx(NULL, xfp, xdp, prog, &ctx,
				      &is_boolean, &bool_value, &is_missing);

    if (is_missing) {
	result = pin_value_null();
    } else if (is_boolean) {
	result = pin_value_bool(bool_value);
    } else if (str != NULL) {
	pin_name_id_t atom = pin_namepool_atom(pwp, str, TRUE);
	result = pin_value_string(pin_name_id_atom_of(atom));
    }
    if (str != NULL)
	free(str);

    pin_exec_push(esp, result);
    return pin_value_null();
}

/*
 * PIN_OP_EVAL for a value that's kept rather than written: a variable,
 * a parameter, or the selection of a for-each or copy-of.  A node-set
 * stays a node-set, so "$v[2]" and "count($v)" see its nodes.
 */
static pin_value_t
pin_op_eval_value (PIN_OP_FUNC_ARGS)
{
    pin_workspace_t *pwp = pin_parse_workspace(parsep);
    xo_eval_ctx_t ctx;
    xo_eval_prog_t *prog = pin_exec_eval_setup(esp, parsep, opp, &ctx);
    pin_value_t result = pin_value_null();

    if (prog == NULL) {
	pin_exec_push(esp, result);
	return pin_value_null();
    }

    xo_filter_t *xfp = parsep->pp_rulebook->prb_eval_xfp;
    struct xo_xparse_data_s *xdp = xo_filter_xparse_data(NULL, xfp);
    char *str = NULL;
    xo_eval_ext_value_t rv = xo_eval_prog_run_value(NULL, xfp, xdp, prog,
						     &ctx, &str);

    switch (rv.xee_type) {
    case XO_EVAL_EXT_NODESET:;
	uint32_t ns_idx = pin_filter_ns_take(xfp, rv.xee_ns);
	if (ns_idx != UINT32_MAX)
	    result = pin_value_nodeset(ns_idx);
	break;

    case XO_EVAL_EXT_BOOLEAN:
	result = pin_value_bool(rv.xee_bool);
	break;

    case XO_EVAL_EXT_STRING:;
	pin_name_id_t atom = pin_namepool_atom(pwp, str, TRUE);
	result = pin_value_string(pin_name_id_atom_of(atom));
	break;

    default:
	break;
    }

    free(str);
    pin_exec_push(esp, result);
    return pin_value_null();
}

/*
 * Push one prefix/uri binding onto the runtime namespace stack.
 * po_name is the prefix atom (null = default namespace), po_name2 the uri atom.
 */
static pin_value_t
pin_op_ns_push (PIN_OP_FUNC_ARGS)
{
    if (esp->pes_ns_cur >= esp->pes_ns_max) {
        uint32_t newsize = esp->pes_ns_max ? esp->pes_ns_max * 2 : 8;
        pin_ns_map_t *np = realloc(esp->pes_ns, newsize * sizeof(*np));
        if (np == NULL)
            return pin_value_null();

        esp->pes_ns = np;
        esp->pes_ns_max = newsize;
    }

    pin_ns_map_t *np = &esp->pes_ns[esp->pes_ns_cur];
    np->pnm_prefix = opp->po_name;
    np->pnm_uri = opp->po_name2;
    esp->pes_ns_cur += 1;
    return pin_value_null();
}

/*
 * Pop po_count bindings pushed by the preceding NS_PUSH ops.
 */
static pin_value_t
pin_op_ns_pop (PIN_OP_FUNC_ARGS)
{
    uint32_t count = opp->po_count;
    if (count > esp->pes_ns_cur)
        count = esp->pes_ns_cur;
    esp->pes_ns_cur -= count;
    return pin_value_null();
}

/*
 * Fragments: content built for a value.  Each is recorded so it can
 * be freed when the scope that made it ends.
 */
static void
pin_exec_frag_add (pin_exec_state_t *esp, pin_node_id_t root)
{
    if (esp->pes_frag_count >= esp->pes_frag_size) {
        uint32_t newsize = esp->pes_frag_size ? esp->pes_frag_size * 2 : 8;
        pin_node_id_t *np = realloc(esp->pes_frags, newsize * sizeof(*np));
        if (np == NULL)
            return;

        esp->pes_frags = np;
        esp->pes_frag_size = newsize;
    }

    esp->pes_frags[esp->pes_frag_count] = root;
    esp->pes_frag_count += 1;
}

static void
pin_exec_frags_free (pin_exec_state_t *esp, pin_parse_t *parsep,
                     uint32_t watermark)
{
    pin_workspace_t *pwp = pin_parse_workspace(parsep);

    while (esp->pes_frag_count > watermark) {
        esp->pes_frag_count -= 1;
        pin_output_store_free_root(pwp, esp->pes_frags[esp->pes_frag_count]);
    }
}

/*
 * The fragment a value is, or the null atom: a node set holding just
 * one fragment root.
 */
static pin_node_id_t
pin_exec_value_frag (pin_exec_state_t *esp, pin_value_t v)
{
    if (v.pv_type != PVT_NODESET || v.pv_atom >= esp->pes_nodeset_count)
        return pin_node_id_null_atom();

    pin_ns_entry_t *nsp = &esp->pes_nodesets[v.pv_atom];
    if (nsp->pne_count != 1)
        return pin_node_id_null_atom();

    pin_node_id_t nid = pin_node_id(nsp->pne_nodes[0]);
    for (uint32_t i = 0; i < esp->pes_frag_count; i++) {
        if (pin_node_id_equal(esp->pes_frags[i], nid))
            return nid;
    }

    return pin_node_id_null_atom();
}

/* A node set holding just "root" */
static pin_value_t
pin_exec_frag_value (pin_exec_state_t *esp, pin_node_id_t root)
{
    uint32_t idx = pin_exec_nodeset_alloc(esp);
    if (idx == UINT32_MAX)
        return pin_value_null();

    pin_exec_nodeset_append(esp, idx,
            pa_fixed_atom_of(pin_node_id_atom_of(root)));

    return pin_value_nodeset(idx);
}

/*
 * The string value of a scalar (a string or boolean), or NULL for
 * anything else.
 */
static const char *
pin_exec_scalar_string (pin_workspace_t *pwp, pin_value_t v)
{
    if (v.pv_type == PVT_STRING)
        return pin_namepool_string(pwp, pin_name_id(v.pv_atom)) ?: "";

    if (v.pv_type == PVT_BOOLEAN)
        return (v.pv_flags & PVF_TRUE) ? "true" : "false";

    return NULL;
}

/*
 * Send a value to the output as an mvar append does: a node set is
 * copied, and a non-empty scalar becomes a <text> element holding its
 * string value.
 */
static void
pin_exec_emit_value (pin_exec_state_t *esp, pin_parse_t *parsep,
                     pin_value_t v)
{
    pin_workspace_t *pwp = pin_parse_workspace(parsep);

    switch (v.pv_type) {
    case PVT_NODESET:
        if (v.pv_atom < esp->pes_nodeset_count) {
            pin_ns_entry_t *nsp = &esp->pes_nodesets[v.pv_atom];
            for (uint32_t i = 0; i < nsp->pne_count; i++)
                pin_exec_emit_node(parsep, pwp, pin_node_id(nsp->pne_nodes[i]));
        }
        return;

    case PVT_NODE:
        pin_exec_emit_node(parsep, pwp, pin_node_id(v.pv_atom));
        return;

    default:
        break;
    }

    const char *str = pin_exec_scalar_string(pwp, v);
    if (str == NULL || *str == '\0')
        return;

    pin_name_id_t tag = pin_namepool_atom(pwp, "text", TRUE);
    pin_insert_open(parsep, tag, NULL, "text", NULL, PIA_NONE);
    pin_insert_text(parsep, str, strlen(str), PIN_TYPE_TEXT);
    pin_insert_close(parsep, NULL, "text");
}

/*
 * Send the output that follows to a new fragment, until BUILD_CLOSE.
 */
static pin_value_t
pin_op_build_open (PIN_OP_FUNC_ARGS)
{
    pin_node_id_t root = pin_parse_build_push(parsep, pin_node_id_null_atom());
    if (!pin_node_id_is_null(root))
        pin_exec_frag_add(esp, root);

    return pin_value_null();
}

static pin_value_t
pin_op_build_close (PIN_OP_FUNC_ARGS)
{
    pin_node_id_t root = pin_parse_build_pop(parsep);

    if (pin_node_id_is_null(root))
        pin_exec_push(esp, pin_value_string(pin_name_id_atom_of(
                pin_namepool_atom(pin_parse_workspace(parsep), "", TRUE))));
    else
        pin_exec_push(esp, pin_exec_frag_value(esp, root));

    return pin_value_null();
}

/*
 * Send the output that follows to the end of the variable named by
 * name_atom, until pin_parse_build_pop.  A variable that isn't a
 * fragment becomes one, holding a copy of its old value if that's a
 * node set; libslax drops a scalar.  Other variables that hold the
 * same fragment see what's appended, as with libslax.  Returns the
 * null atom on failure.
 */
static pin_node_id_t
pin_exec_append_begin (pin_exec_state_t *esp, pin_parse_t *parsep,
                       uint32_t name_atom)
{
    pin_var_binding_t *vp = pin_exec_var_find(esp, name_atom);
    pin_value_t old = vp ? vp->pvb_value : pin_value_null();

    pin_node_id_t root = pin_exec_value_frag(esp, old);
    if (!pin_node_id_is_null(root))
        return pin_parse_build_push(parsep, root);

    root = pin_parse_build_push(parsep, pin_node_id_null_atom());
    if (pin_node_id_is_null(root))
        return root;

    pin_exec_frag_add(esp, root);
    if (old.pv_type == PVT_NODESET || old.pv_type == PVT_NODE)
        pin_exec_emit_value(esp, parsep, old);

    vp = pin_exec_var_alloc(esp, name_atom);
    if (vp)
        vp->pvb_value = pin_exec_frag_value(esp, root);

    return root;
}

/* Append the content built until APPEND_CLOSE to variable po_name */
static pin_value_t
pin_op_append_open (PIN_OP_FUNC_ARGS)
{
    pin_exec_append_begin(esp, parsep, pin_name_id_atom_of(opp->po_name));
    return pin_value_null();
}

/*
 * Append the value on the stack to variable po_name.  As in libslax,
 * a scalar appended to a scalar makes their concatenation; otherwise
 * the variable becomes a fragment and the value is added to it.
 */
static pin_value_t
pin_op_append_value (PIN_OP_FUNC_ARGS)
{
    pin_workspace_t *pwp = pin_parse_workspace(parsep);
    uint32_t name_atom = pin_name_id_atom_of(opp->po_name);
    pin_value_t v = pin_exec_pop(esp);

    if (v.pv_type == PVT_NULL)
        return pin_value_null();

    const char *str = pin_exec_scalar_string(pwp, v);
    if (str) {
        pin_var_binding_t *vp = pin_exec_var_find(esp, name_atom);
        pin_value_t old = vp ? vp->pvb_value : pin_value_null();
        const char *ostr = (old.pv_type == PVT_NULL)
            ? "" : pin_exec_scalar_string(pwp, old);

        if (ostr) {
            size_t olen = strlen(ostr);
            size_t len = olen + strlen(str) + 1;
            char *buf = alloca(len);

            memcpy(buf, ostr, olen);
            strlcpy(buf + olen, str, len - olen);

            vp = pin_exec_var_alloc(esp, name_atom);
            if (vp) {
                pin_name_id_t id = pin_namepool_atom(pwp, buf, TRUE);
                vp->pvb_value = pin_value_string(pin_name_id_atom_of(id));
            }
            return pin_value_null();
        }
    }

    if (pin_node_id_is_null(pin_exec_append_begin(esp, parsep, name_atom)))
        return pin_value_null();

    pin_exec_emit_value(esp, parsep, v);
    pin_parse_build_pop(parsep);
    return pin_value_null();
}

static pin_value_t
pin_op_append_close (PIN_OP_FUNC_ARGS)
{
    pin_parse_build_pop(parsep);
    return pin_value_null();
}

/*
 * Op dispatch table
 */

pin_op_def_t pin_op_table[PIN_OP_MAX] = {
    /* complex ops (types < PIN_OP_MAX_COMPLEX) */
    [PIN_OP_NONE]         = { "none",         pin_op_stub,         0 },
    [PIN_OP_APPLY]        = { "apply",        pin_op_apply,        0 },
    [PIN_OP_PUSH_NODE]    = { "push-node",    pin_op_push_node,    0 },
    [PIN_OP_COMPLEX_EXPR] = { "complex-expr", pin_op_complex_expr, 0 },
    /* slots 4-7: reserved for future complex ops (NULL handler -> skipped) */
    /* non-complex ops (types >= PIN_OP_MAX_COMPLEX) */
    [PIN_OP_EMIT_OPEN]    = { "emit-open",    pin_op_emit_open,    0 },
    [PIN_OP_EMIT_CLOSE]   = { "emit-close",   pin_op_emit_close,   0 },
    [PIN_OP_EMIT_ATTRIB]  = { "emit-attrib",  pin_op_emit_attrib,  0 },
    [PIN_OP_EMIT]         = { "emit",         pin_op_emit,         0 },
    [PIN_OP_PUSH_STRING]  = { "push-string",  pin_op_push_string,  0 },
    [PIN_OP_PUSH_ATTR]    = { "push-attr",    pin_op_push_attr,    0 },
    [PIN_OP_PUSH_TEXT]    = { "push-text",    pin_op_push_text,    0 },
    [PIN_OP_PUSH_NODES]   = { "push-nodes",   pin_op_push_nodes,   0 },
    [PIN_OP_PUSH_BOOL]    = { "push-bool",    pin_op_push_bool,    0 },
    [PIN_OP_CONVERT]      = { "convert",      pin_op_stub,         0 },
    [PIN_OP_IF]           = { "if",           pin_op_if,           0 },
    [PIN_OP_GOTO]         = { "goto",         pin_op_goto,         0 },
    [PIN_OP_JUMP]         = { "jump",         pin_op_jump,         0 },
    [PIN_OP_CALL]         = { "call",         pin_op_call,         0 },
    [PIN_OP_RETURN]       = { "return",       pin_op_stub,         0 },
    [PIN_OP_DISCARD]      = { "discard",      pin_op_discard,      0 },
    [PIN_OP_STORE_VAR]    = { "store-var",    pin_op_store_var,    0 },
    [PIN_OP_LOAD_VAR]     = { "load-var",     pin_op_load_var,     0 },
    [PIN_OP_FOR_EACH]     = { "for-each",     pin_op_for_each,     0 },
    [PIN_OP_COPY_OF]      = { "copy-of",      pin_op_copy_of,      0 },
    [PIN_OP_WITH_PARAM]   = { "with-param",   pin_op_with_param,   0 },
    [PIN_OP_LOAD_PARAM]   = { "load-param",   pin_op_load_param,   0 },
    [PIN_OP_ELEMENT_OPEN]  = { "element-open",  pin_op_element_open,  0 },
    [PIN_OP_ELEMENT_CLOSE] = { "element-close", pin_op_element_close, 0 },
    [PIN_OP_COPY_OPEN]     = { "copy-open",     pin_op_copy_open,     0 },
    [PIN_OP_MESSAGE_OPEN]  = { "message-open",  pin_op_message_open,  0 },
    [PIN_OP_MESSAGE_CLOSE] = { "message-close", pin_op_message_close, 0 },
    [PIN_OP_COMMENT_OPEN]  = { "comment-open",  pin_op_comment_open,  0 },
    [PIN_OP_COMMENT_CLOSE] = { "comment-close", pin_op_comment_close, 0 },
    [PIN_OP_PI_OPEN]       = { "pi-open",       pin_op_pi_open,       0 },
    [PIN_OP_PI_CLOSE]      = { "pi-close",      pin_op_pi_close,      0 },
    [PIN_OP_NUMBER]        = { "number",        pin_op_number,        0 },
    [PIN_OP_PUSH_AVT]      = { "push-avt",      pin_op_push_avt,      0 },
    [PIN_OP_EVAL]          = { "eval",          pin_op_eval,          0 },
    [PIN_OP_NS_PUSH]       = { "ns-push",       pin_op_ns_push,       0 },
    [PIN_OP_NS_POP]        = { "ns-pop",        pin_op_ns_pop,        0 },
    [PIN_OP_BUILD_OPEN]    = { "build-open",    pin_op_build_open,    0 },
    [PIN_OP_BUILD_CLOSE]   = { "build-close",   pin_op_build_close,   0 },
    [PIN_OP_APPEND_OPEN]   = { "append-open",   pin_op_append_open,   0 },
    [PIN_OP_APPEND_VALUE]  = { "append-value",  pin_op_append_value,  0 },
    [PIN_OP_APPEND_CLOSE]  = { "append-close",  pin_op_append_close,  0 },
    [PIN_OP_SET_VAR]       = { "set-var",       pin_op_set_var,       0 },
    [PIN_OP_EVAL_VALUE]    = { "eval-value",    pin_op_eval_value,    0 },
};

/*
 * Dispatch loop
 */

/*
 * Bounds call-template recursion depth: without this, a named template
 * that recurses without a terminating condition grows pes_seq[] by
 * doubling forever instead of failing with a clear error.
 */
#define PIN_EXEC_SEQ_MAX_DEPTH 4096

static int
pin_exec_seq_grow (pin_exec_state_t *esp)
{
    if (esp->pes_seq_top < esp->pes_seq_size)
        return 0;

    if (esp->pes_seq_size >= PIN_EXEC_SEQ_MAX_DEPTH) {
        psu_log("pin_exec: call-template recursion exceeded %d frames; "
                "aborting call", PIN_EXEC_SEQ_MAX_DEPTH);
        return -1;
    }

    int newsize = esp->pes_seq_size ? esp->pes_seq_size * 2 : 8;
    if (newsize > PIN_EXEC_SEQ_MAX_DEPTH)
        newsize = PIN_EXEC_SEQ_MAX_DEPTH;
    pin_exec_seq_frame_t *seq = realloc(esp->pes_seq, newsize * sizeof(*seq));
    if (seq == NULL)
        return -1;

    esp->pes_seq = seq;
    esp->pes_seq_size = newsize;
    return 0;
}

/*
 * End one item of the loop in frame "top": drop the variables its
 * body declared, and free the node sets and fragments it made, so a
 * loop runs in space that doesn't grow with its item count.  If a
 * variable that outlives the item (an mvar the body assigned) holds
 * one of them, they are all kept until the enclosing scope ends.
 */
static void
pin_exec_item_end (pin_exec_state_t *esp, pin_parse_t *parsep, int top)
{
    pin_exec_seq_frame_t *sfp = &esp->pes_seq[top];
    uint32_t snc = sfp->psf_saved_nodeset_count;

    esp->pes_var_count = sfp->psf_saved_var_count;

    for (uint32_t i = 0; i < esp->pes_var_count; i++) {
        pin_value_t v = esp->pes_vars[i].pvb_value;
        if (v.pv_type == PVT_NODESET && v.pv_atom >= snc) {
            sfp->psf_saved_nodeset_count = esp->pes_nodeset_count;
            sfp->psf_saved_frag_count = esp->pes_frag_count;
            return;
        }
    }

    for (uint32_t i = snc; i < esp->pes_nodeset_count; i++)
        pin_exec_nodeset_free(esp, i);
    esp->pes_nodeset_count = snc;
    pin_exec_frags_free(esp, parsep, sfp->psf_saved_frag_count);
}

/*
 * Shared core for pin_exec_run() / pin_exec_run_call(): push one seq frame
 * for 'start' and drain the dispatch loop until that frame (and anything
 * it calls) finishes.  is_call/param_base/param_count mirror what
 * pin_op_call() stages for an in-sequence PIN_OP_CALL, letting a fresh
 * top-level call (e.g. a BIA_CALL body instruction) hand parameters to
 * the callee's PIN_OP_LOAD_PARAM ops exactly as a nested call would.
 */
static int
pin_exec_run_frame (pin_exec_state_t *esp, struct pin_parse_s *parsep,
		     pin_op_id_t start, pin_node_id_t context_node,
		     uint8_t is_call, uint32_t param_base, uint32_t param_count,
		     pin_boolean_t keep)
{
    pin_rulebook_t *prbp = parsep->pp_rulebook;

    /*
     * Save variable and nodeset watermarks so bindings made in this
     * template invocation are discarded on return, keeping variables
     * scoped to one template body.
     */
    uint32_t saved_var_count = esp->pes_var_count;
    uint32_t saved_nodeset_count  = esp->pes_nodeset_count;
    uint32_t saved_frag_count = esp->pes_frag_count;

    if (pin_exec_seq_grow(esp) < 0)
        return -1;

    int top = esp->pes_seq_top;
    esp->pes_seq[top].psf_pc = start;
    esp->pes_seq[top].psf_context = context_node;
    esp->pes_seq[top].psf_is_call = is_call;
    esp->pes_seq[top].psf_saved_var_count = saved_var_count;
    esp->pes_seq[top].psf_saved_nodeset_count = saved_nodeset_count;
    esp->pes_seq[top].psf_saved_frag_count = saved_frag_count;
    esp->pes_seq[top].psf_param_base = param_base;
    esp->pes_seq[top].psf_param_count = param_count;
    esp->pes_seq[top].psf_iter = NULL;
    esp->pes_seq_top += 1;

    while (esp->pes_seq_top > 0) {
        top = esp->pes_seq_top - 1;

        pin_op_id_t pc = esp->pes_seq[top].psf_pc;
        if (pin_op_id_is_null(pc)) {
            pin_exec_iter_t *iterp = esp->pes_seq[top].psf_iter;
            if (iterp) {
                pin_exec_item_end(esp, parsep, top);
                pin_node_id_t item = pin_exec_iter_next(esp, parsep, iterp,
                                                        &pc);
                if (!pin_node_id_is_null(item)) {
                    esp->pes_seq[top].psf_pc = pc;
                    esp->pes_seq[top].psf_context = item;
                    continue;
                }
                pin_exec_iter_free(esp, iterp);
            }

            if (esp->pes_seq[top].psf_is_call) {
                uint32_t svc = esp->pes_seq[top].psf_saved_var_count;
                uint32_t snc = esp->pes_seq[top].psf_saved_nodeset_count;
                for (uint32_t i = snc; i < esp->pes_nodeset_count; i++)
                    pin_exec_nodeset_free(esp, i);
                esp->pes_var_count = svc;
                esp->pes_nodeset_count = snc;
                pin_exec_frags_free(esp, parsep,
                        esp->pes_seq[top].psf_saved_frag_count);
                esp->pes_pending_count = esp->pes_seq[top].psf_param_base;
            }
            esp->pes_seq_top -= 1;
            continue;
        }

        pin_op_t *opp = pin_op_addr(prbp, pc);
        if (opp == NULL) {
            esp->pes_seq_top -= 1;
            continue;
        }

        /* Advance PC before dispatch; pod_func may override for branches.
         * Do not hold a pointer into pes_seq across the pod_func call --
         * ops that push new frames (CALL, APPLY) may realloc pes_seq. */
        esp->pes_seq[top].psf_pc = opp->po_next;

        pin_op_type_t type = opp->po_type;
        if (type < PIN_OP_MAX && pin_op_table[type].pod_func)
            pin_op_table[type].pod_func(esp, parsep, opp);
    }

    if (keep)
        return 0;

    /* Free nodesets allocated during this invocation */
    for (uint32_t i = saved_nodeset_count; i < esp->pes_nodeset_count; i++) {
        pin_exec_nodeset_free(esp, i);
    }
    esp->pes_var_count = saved_var_count;
    esp->pes_nodeset_count = saved_nodeset_count;
    pin_exec_frags_free(esp, parsep, saved_frag_count);

    return 0;
}

int
pin_exec_run (pin_exec_state_t *esp, struct pin_parse_s *parsep,
	      pin_op_id_t start, pin_node_id_t context_node)
{
    return pin_exec_run_frame(esp, parsep, start, context_node,
                              0, 0, 0, FALSE);
}

int
pin_exec_run_globals (pin_exec_state_t *esp, struct pin_parse_s *parsep,
                      pin_op_id_t start)
{
    return pin_exec_run_frame(esp, parsep, start, pin_node_id_null_atom(),
                              0, 0, 0, TRUE);
}

/*
 * Top-level call-template invocation from outside the op-sequence machine
 * (BIA_CALL in the body FSM).  'nparams' is the count of BIA_WITH_PARAM
 * instructions already staged into esp->pes_pending immediately before
 * this call; param_base is derived exactly as pin_op_call() derives it,
 * so the callee's PIN_OP_LOAD_PARAM ops see the same pending-param window
 * they would from a nested PIN_OP_CALL.
 */
int
pin_exec_run_call (pin_exec_state_t *esp, struct pin_parse_s *parsep,
		   pin_op_id_t start, pin_node_id_t context_node,
		   uint32_t nparams)
{
    uint32_t param_base = (esp->pes_pending_count >= nparams)
	? esp->pes_pending_count - nparams : 0;

    return pin_exec_run_frame(esp, parsep, start, context_node,
			      1, param_base, nparams, FALSE);
}

/*
 * Rulebook stack helpers
 */

static int
pin_exec_rb_grow (pin_exec_state_t *esp)
{
    if (esp->pes_rb_top < esp->pes_rb_size)
        return 0;

    int newsize = esp->pes_rb_size ? esp->pes_rb_size * 2 : 8;
    pin_exec_rb_frame_t *rb = realloc(esp->pes_rb, newsize * sizeof(*rb));
    if (rb == NULL)
        return -1;

    esp->pes_rb = rb;
    esp->pes_rb_size = newsize;
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
        fprintf(out, "    rule %u: action %u flags %#x reach %s%s%s\n",
                (unsigned) ridn, rulep->pr_action, rulep->pr_flags,
                rulep->pr_reach ? "" : "self",
                (rulep->pr_reach & XO_REACH_SUBTREE) ? "subtree " : "",
                (rulep->pr_reach & XO_REACH_SPINE) ? "spine" : "");

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
    if (esp->pes_val_top < esp->pes_val_size)
        return 0;

    int newsize = esp->pes_val_size ? esp->pes_val_size * 2 : 16;
    pin_value_t *val = realloc(esp->pes_val, newsize * sizeof(*val));
    if (val == NULL)
        return -1;

    esp->pes_val = val;
    esp->pes_val_size = newsize;
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
    if (esp->pes_nodeset_count >= esp->pes_nodeset_size) {
	uint32_t newsize = esp->pes_nodeset_size ? esp->pes_nodeset_size * 2 : 8;
	pin_ns_entry_t *nsp = realloc(esp->pes_nodesets, newsize * sizeof(*nsp));
	if (nsp == NULL)
	    return UINT32_MAX;

	memset(nsp + esp->pes_nodeset_size, 0,
	       (newsize - esp->pes_nodeset_size) * sizeof(*nsp));
	esp->pes_nodesets = nsp;
	esp->pes_nodeset_size = newsize;
    }

    esp->pes_nodesets[esp->pes_nodeset_count].pne_freed = 0;
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
    nsp->pne_size = 0;
    nsp->pne_freed = 1;

    /*
     * Slots are used like a stack, so a temporary is usually the top
     * one when it's freed.  Give back the top slot, and any freed
     * slots it was hiding, so the next allocation reuses them.
     */
    while (esp->pes_nodeset_count > 0
	       && esp->pes_nodesets[esp->pes_nodeset_count - 1].pne_freed)
	esp->pes_nodeset_count -= 1;
}

int
pin_exec_nodeset_append (pin_exec_state_t *esp, uint32_t idx,
			 uint32_t node_atom)
{
    if (idx >= esp->pes_nodeset_count)
	return -1;

    pin_ns_entry_t *nsp = &esp->pes_nodesets[idx];
    if (nsp->pne_count >= nsp->pne_size) {
	uint32_t newsize = nsp->pne_size ? nsp->pne_size * 2 : 8;
	uint32_t *nodes = realloc(nsp->pne_nodes, newsize * sizeof(*nodes));
	if (nodes == NULL)
	    return -1;

	nsp->pne_nodes = nodes;
	nsp->pne_size = newsize;
    }

    nsp->pne_nodes[nsp->pne_count++] = node_atom;
    return 0;
}
