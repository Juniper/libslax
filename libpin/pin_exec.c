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
    pin_action_type_t act = pip->pin_stack[pip->pin_depth].ps_action;
    if (attribs)
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
        if (str)
            pin_insert_text(parsep, str, strlen(str), PIN_TYPE_TEXT);
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
 * element with that name; a "." step leaves the current node unchanged.
 * Returns null_atom if any non-trivial step is not found.
 */
static pin_node_id_t
pin_exec_node_at_path (pin_workspace_t *pwp, pin_node_id_t start,
                       const char *path, size_t plen)
{
    pin_node_id_t target = start;
    const char *p = path;
    size_t remaining = plen;

    while (remaining > 0) {
        const char *slash = memchr(p, '/', remaining);
        size_t step_len = slash ? (size_t)(slash - p) : remaining;

        if (step_len > 0 && !(step_len == 1 && p[0] == '.')) {
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
    pin_node_id_t target = pin_exec_node_at_path(pwp, ctx, sel ? sel : ".", slen ? slen : 1);
    if (pin_node_id_is_null(target))
        target = ctx;

    pin_name_id_t result = pin_exec_text_of(pwp, target);
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
        return;  /* name not in pool → no such elements exist */

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

static pin_value_t
pin_op_push_bool (PIN_OP_FUNC_ARGS)
{
    pin_value_t v = pin_exec_pop(esp);
    int truth = (v.pv_type == PVT_NODESET)
        ? pin_exec_nodeset_is_true(esp, v)
        : pin_value_is_true(v);
    pin_exec_push(esp, pin_value_bool(truth));
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
    pin_value_t v = pin_exec_pop(esp);
    int truth = (v.pv_type == PVT_NODESET)
        ? pin_exec_nodeset_is_true(esp, v)
        : pin_value_is_true(v);

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
    pin_exec_push(esp, vp ? vp->pvb_value : pin_value_null());
    return pin_value_null();
}

/* Forward declaration — defined after the dispatch table */
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
    esp->pes_seq[top].psf_saved_var_count = 0;
    esp->pes_seq[top].psf_saved_nodeset_count = 0;
    esp->pes_seq[top].psf_param_base = 0;
    esp->pes_seq[top].psf_param_count = 0;
    esp->pes_seq_top += 1;
    return 0;
}

/*
 * Evaluate a single sort-key expression against child_id, writing the
 * raw value into valbuf[vcap].  Supported forms:
 *   @attr     — attribute value
 *   .         — text content of context node itself
 *   a/b/c/... — text content of node reached by walking the path steps
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
 * Build the sort key for child element child_id using the sort-key spec
 * string spec.  Writes the encoded, NUL-separated key into keybuf[keycap].
 * Returns the total key length in bytes.
 */
static uint16_t
pin_for_each_build_key (pin_workspace_t *pwp, pin_node_id_t child_id,
                        const char *spec,
                        char *keybuf, size_t keycap)
{
    size_t pos = 0;
    const char *p = spec;

    while (p && *p) {
        bool upper = false;
        while (*p == '-' || *p == '^') {
            if (*p == '^')
                upper = true;
            p += 1;
        }

        const char *end = strchr(p, '\n');
        size_t elen = end ? (size_t)(end - p) : strlen(p);

        char valbuf[256];
        pin_for_each_eval_expr(pwp, child_id, p, elen, valbuf, sizeof(valbuf));

        pos = pin_stree_encode_key(keybuf, keycap, pos, valbuf, upper);

        if (pos < keycap)
            keybuf[pos++] = '\0';

        p = end ? end + 1 : NULL;
    }

    if (pos < keycap)
        keybuf[pos] = '\0';
    return (uint16_t) pos;
}

static pin_value_t
pin_op_for_each (PIN_OP_FUNC_ARGS)
{
    if (pin_op_id_is_null(opp->po_alt))
        return pin_value_null();

    pin_workspace_t *pwp = pin_parse_workspace(parsep);
    pin_node_id_t parent_id = esp->pes_seq[esp->pes_seq_top - 1].psf_context;
    pin_node_t *parent = pin_node_addr(pwp, parent_id);
    if (parent == NULL)
        return pin_value_null();

    const char *spec = pin_namepool_string(pwp, opp->po_name2);
    bool has_sort = (spec && *spec);

    bool primary_desc = false;
    if (has_sort) {
        const char *sp = spec;
        while (*sp == '-' || *sp == '^') {
            if (*sp == '-') primary_desc = true;
            sp += 1;
        }
    }

    pin_stree_t *tree = has_sort ? pin_stree_create() : NULL;

    pin_node_id_t *doc = NULL;
    int doc_count = 0, doc_cap = 0;

    pin_depth_t depth = parent->pn_depth;
    for (pin_node_id_t cid = pin_node_child(parent);
            !pin_node_id_is_null(cid); ) {
        pin_node_t *child = pin_node_addr(pwp, cid);
        if (child == NULL || child->pn_depth <= depth)
            break;
        if (child->pn_type == PIN_TYPE_ELT
                && pin_name_id_equal(child->pn_name, opp->po_name)) {
            if (has_sort && tree) {
                char keybuf[512];
                uint16_t klen = pin_for_each_build_key(pwp, cid, spec,
                                                       keybuf, sizeof(keybuf));

		/* XXX deal with failure (klen < 0?) */
                pin_stree_insert(tree, keybuf, klen, cid);
            } else {
                if (doc_count >= doc_cap) {
                    int newcap = doc_cap ? doc_cap * 2 : 8;
                    pin_node_id_t *nd = realloc(doc, newcap * sizeof(*nd));
                    if (nd == NULL)
                        break;
                    doc = nd;
                    doc_cap = newcap;
                }
                doc[doc_count++] = cid;
            }
        }
        cid = child->pn_next;
    }

    if (has_sort && tree) {
        pin_stree_entry_t *e;
        if (!primary_desc) {
            for (e = pin_stree_last(tree); e; e = pin_stree_prev(tree, e))
                pin_exec_push_seq_frame(esp, opp->po_alt, e->pse_node);
        } else {
            for (e = pin_stree_first(tree); e; e = pin_stree_next(tree, e))
                pin_exec_push_seq_frame(esp, opp->po_alt, e->pse_node);
        }
        pin_stree_free(tree);
    } else {
        for (int i = doc_count - 1; i >= 0; i -= 1)
            pin_exec_push_seq_frame(esp, opp->po_alt, doc[i]);
        free(doc);
    }

    return pin_value_null();
}

/*
 * Recursively emit one node and its full subtree to the output stream as new
 * non-transient nodes.  Children are walked via pin_node_child + pn_next
 * (sibling chain), never pn_next of the root, which links to siblings.
 */
static void
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
        pin_action_type_t act = pip->pin_stack[pip->pin_depth].ps_action;
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

    } else if (nodep->pn_type == PIN_TYPE_TEXT
            || nodep->pn_type == PIN_TYPE_UNESC) {
        const char *text = pin_textpool_string(pwp,
                pa_arb_atom_of(pin_node_text(nodep)));
        if (text)
            pin_insert_text(parsep, text, strlen(text), nodep->pn_type);
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

static pin_value_t
pin_op_copy_of (PIN_OP_FUNC_ARGS)
{
    pin_workspace_t *pwp = pin_parse_workspace(parsep);
    pin_node_id_t ctx = esp->pes_seq[esp->pes_seq_top - 1].psf_context;
    pin_node_t *ctx_node = pin_node_addr(pwp, ctx);
    pin_depth_t stop_depth = ctx_node ? ctx_node->pn_depth - 1 : 0;

    /* Build the source nodeset */
    const char *path = pin_namepool_string(pwp, opp->po_name);
    uint32_t ns_idx = UINT32_MAX;
    int free_ns = 0;

    if (path && path[0] == '$') {
        /* Variable reference: resolve from var table */
        pin_name_id_t vname = pin_namepool_atom(pwp, path + 1, FALSE);
        pin_var_binding_t *vp = pin_exec_var_find(esp,
                pin_name_id_atom_of(vname));
        if (vp && vp->pvb_value.pv_type == PVT_NODESET)
            ns_idx = vp->pvb_value.pv_atom;
    } else {
        /* Path or null (meaning "."): collect into ephemeral nodeset */
        ns_idx = pin_exec_nodeset_alloc(esp);
        if (ns_idx != UINT32_MAX) {
            free_ns = 1;
            if (path && path[0] != '\0' && strcmp(path, ".") != 0) {
                pin_exec_collect_path(pwp, esp, ctx,
                        path, strlen(path), ns_idx);
            } else {
                pin_exec_nodeset_append(esp, ns_idx,
                        pa_fixed_atom_of(pin_node_id_atom_of(ctx)));
            }
        }
    }

    if (ns_idx != UINT32_MAX && ns_idx < (uint32_t) esp->pes_nodeset_count) {
        pin_ns_entry_t *nsp = &esp->pes_nodesets[ns_idx];
        for (uint32_t i = 0; i < nsp->pne_count; i++) {
            pin_node_id_t nid = pin_node_id(nsp->pne_nodes[i]);
            pin_exec_emit_subtree(parsep, nid, stop_depth);
        }
    }

    if (free_ns && ns_idx != UINT32_MAX)
        pin_exec_nodeset_free(esp, ns_idx);

    return pin_value_null();
}

static pin_value_t
pin_op_with_param (PIN_OP_FUNC_ARGS)
{
    pin_value_t v = pin_exec_pop(esp);
    uint32_t name_atom = pin_name_id_atom_of(opp->po_name);

    if (esp->pes_pending_count >= esp->pes_pending_cap) {
        uint32_t newcap = esp->pes_pending_cap ? esp->pes_pending_cap * 2 : 8;
        pin_pending_param_t *np = realloc(esp->pes_pending,
                                          newcap * sizeof(*np));
        if (np == NULL)
            return pin_value_null();
        esp->pes_pending = np;
        esp->pes_pending_cap = newcap;
    }
    esp->pes_pending[esp->pes_pending_count].ppp_name  = name_atom;
    esp->pes_pending[esp->pes_pending_count].ppp_value = v;
    esp->pes_pending_count += 1;
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
    esp->pes_seq[top].psf_param_base = param_base;
    esp->pes_seq[top].psf_param_count = nparams;
    esp->pes_seq_top += 1;
    return pin_value_null();
}

/*
 * Op dispatch table
 */

pin_op_def_t pin_op_table[PIN_OP_MAX] = {
    /* complex ops (types < PIN_OP_MAX_COMPLEX) */
    [PIN_OP_NONE]         = { "none",         pin_op_stub,         0 },
    [PIN_OP_APPLY]        = { "apply",        pin_op_stub,         0 },
    [PIN_OP_PUSH_NODE]    = { "push-node",    pin_op_push_node,    0 },
    [PIN_OP_COMPLEX_EXPR] = { "complex-expr", pin_op_complex_expr, 0 },
    /* slots 4-7: reserved for future complex ops (NULL handler → skipped) */
    /* non-complex ops (types >= PIN_OP_MAX_COMPLEX) */
    [PIN_OP_EMIT_OPEN]    = { "emit-open",    pin_op_emit_open,    0 },
    [PIN_OP_EMIT_CLOSE]   = { "emit-close",   pin_op_emit_close,   0 },
    [PIN_OP_EMIT_ATTRIB]  = { "emit-attrib",  pin_op_stub,         0 },
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

    /*
     * Save variable and nodeset watermarks so bindings made in this
     * template invocation are discarded on return, keeping variables
     * scoped to one template body.
     */
    uint32_t saved_var_count = esp->pes_var_count;
    uint32_t saved_nodeset_count  = esp->pes_nodeset_count;

    if (pin_exec_seq_grow(esp) < 0)
        return -1;

    int top = esp->pes_seq_top;
    esp->pes_seq[top].psf_pc = start;
    esp->pes_seq[top].psf_context = context_node;
    esp->pes_seq[top].psf_is_call = 0;
    esp->pes_seq[top].psf_saved_var_count = 0;
    esp->pes_seq[top].psf_saved_nodeset_count = 0;
    esp->pes_seq[top].psf_param_base = 0;
    esp->pes_seq[top].psf_param_count = 0;
    esp->pes_seq_top += 1;

    while (esp->pes_seq_top > 0) {
        top = esp->pes_seq_top - 1;

        pin_op_id_t pc = esp->pes_seq[top].psf_pc;
        if (pin_op_id_is_null(pc)) {
            if (esp->pes_seq[top].psf_is_call) {
                uint32_t svc = esp->pes_seq[top].psf_saved_var_count;
                uint32_t snc = esp->pes_seq[top].psf_saved_nodeset_count;
                for (uint32_t i = snc; i < esp->pes_nodeset_count; i++)
                    pin_exec_nodeset_free(esp, i);
                esp->pes_var_count = svc;
                esp->pes_nodeset_count = snc;
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
         * Do not hold a pointer into pes_seq across the pod_func call —
         * ops that push new frames (CALL, APPLY) may realloc pes_seq. */
        esp->pes_seq[top].psf_pc = opp->po_next;

        pin_op_type_t type = opp->po_type;
        if (type < PIN_OP_MAX && pin_op_table[type].pod_func)
            pin_op_table[type].pod_func(esp, parsep, opp);
    }

    /* Free nodesets allocated during this invocation */
    for (uint32_t i = saved_nodeset_count; i < esp->pes_nodeset_count; i++) {
        pin_exec_nodeset_free(esp, i);
    }
    esp->pes_var_count = saved_var_count;
    esp->pes_nodeset_count = saved_nodeset_count;

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
