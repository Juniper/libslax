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
 * libpin data backend for xo_filter's trie/FSM.
 *
 * Defines the real layout of struct xo_filter_data_s for this backend
 * (the filter core only sees the forward declaration).  Names in the
 * compiled trie are stored as pin paistr atoms; matching compares atom
 * integers rather than strings.
 *
 * The trie node array is heap-allocated (realloc/free) in this phase.
 * A future phase can route it through pa_arb for mmap persistence once
 * the node array grow/shrink pattern is clear.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>

#include "slaxconfig.h"
#include <libpsu/psulog.h>
#include <parrotdb/pacommon.h>
#include <parrotdb/paconfig.h>
#include <parrotdb/pammap.h>
#include <parrotdb/pafixed.h>

#include <libxo/xo.h>
#include <libxo/xo_buf.h>
#include <libxo/xo_filter.h>

#include <libpin/pin_common.h>
#include <libpin/pin_workspace.h>
#include <libpin/pin_exec.h>
#include <libpin/pin_filter.h>
#include <libpin/pin_dyn.h>

/*
 * Real definition of xo_filter_data_t for the pin backend.
 * Only this translation unit sees the layout; the filter core in
 * xo_filter.so sees only the forward declaration from xo_filter.h.
 *
 * xfd_node_stack / xfd_node_depth track, for each depth of the live
 * xo_filter frame stack, the retained-tree node (if any) that corresponds
 * to the element currently open at that depth.  xfdo_event_open/close
 * push/pop a slot in lock-step with the filter's own frame stack;
 * pin_filter_set_cur_node fills in the real node id once pin_parse has
 * created (or reused) it, which happens slightly after the filter's own
 * open event fires.  Every input element has a node, discarded or not;
 * a slot left null_atom (an element opened only in the output) simply
 * yields "not found" lookups.
 */
struct xo_filter_data_s {
    pin_workspace_t *xfd_workspace; /* namepool and mmap backing */
    const char *xfd_attribs;        /* raw attr string for current token */
    char *xfd_value_buf;            /* scratch space for xfdo_value_of result */
    size_t xfd_value_buf_max;       /* allocated size of xfd_value_buf */
    pin_node_id_t *xfd_node_stack;  /* retained node per depth */
    size_t xfd_node_stack_max;      /* allocated slot count of xfd_node_stack */
    int xfd_node_depth;             /* index of the current (top) slot */
    pin_exec_state_t *xfd_esp;      /* current exec state, for xfdo_variable_of */
    const pin_ns_map_t *xfd_compile_ns; /* in-scope prefix/uri bindings at compile site */
    size_t xfd_compile_ns_count;    /* number of entries in xfd_compile_ns */
    struct pin_filter_ns_s *xfd_sets; /* node-sets handed to libxo */
    uint32_t xfd_sets_count;        /* slots in use or freed */
    uint32_t xfd_sets_max;          /* allocated slot count of xfd_sets */
    xo_buffer_t xfd_string_buf;     /* xfdo_string_value result */
    struct pin_rulebook_s *xfd_rulebook; /* global variables, for $name */
};

/*
 * A node-set for libxo's evaluator.  The handle is the slot index + 1.
 * libxo never changes a set after it's built, so a copy shares the
 * slot and counts a reference; a slot with no references is free.
 */
typedef struct pin_filter_ns_s {
    pin_node_id_t *pfn_nodes;	/* Nodes, in the order appended */
    uint32_t pfn_count;		/* Nodes in use */
    uint32_t pfn_max;		/* Allocated size of pfn_nodes */
    uint32_t pfn_refs;		/* References; 0 means the slot is free */
} pin_filter_ns_t;

/*
 * Data vtable implementation
 */

static void *
pin_filter_data_realloc (xo_filter_data_t *dp UNUSED, void *ptr, size_t sz)
{
    return realloc(ptr, sz);
}

static void
pin_filter_data_free (xo_filter_data_t *dp UNUSED, void *ptr)
{
    free(ptr);
}

/*
 * Called once by xo_filter_destroy_standalone/"destroy" when the owning
 * filter is torn down, after the trie and all filter-owned state have
 * already been freed.  We allocated dp (in pin_filter_create), so we're
 * the one who frees it, along with the heap buffers it grew into.
 */
static void
pin_filter_data_destroy (xo_filter_data_t *dp)
{
    if (dp == NULL)
	return;

    for (uint32_t i = 0; i < dp->xfd_sets_count; i++)
	free(dp->xfd_sets[i].pfn_nodes);
    free(dp->xfd_sets);
    xo_buf_cleanup(&dp->xfd_string_buf);
    free(dp->xfd_value_buf);
    free(dp->xfd_node_stack);
    free(dp);
}

/*
 * Grow xfd_value_buf (doubling) so it can hold at least 'need' bytes.
 * Returns 0 on success, -1 if realloc fails (buffer left unchanged).
 */
static int
pin_filter_data_value_buf_grow (xo_filter_data_t *dp, size_t need)
{
    if (need <= dp->xfd_value_buf_max)
        return 0;

    size_t newsize = dp->xfd_value_buf_max ? dp->xfd_value_buf_max * 2 : 512;
    while (newsize < need)
        newsize *= 2;

    char *newp = realloc(dp->xfd_value_buf, newsize);
    if (newp == NULL)
        return -1;

    dp->xfd_value_buf = newp;
    dp->xfd_value_buf_max = newsize;
    return 0;
}

/*
 * Grow xfd_node_stack (doubling) so it can hold at least 'need' slots.
 * Newly added slots are zero-filled, which is null_atom (PA_NULL_ATOM is 0),
 * matching the "no node yet" meaning event_open gives a freshly pushed slot.
 * Returns 0 on success, -1 if realloc fails (stack left unchanged).
 */
static int
pin_filter_data_node_stack_grow (xo_filter_data_t *dp, size_t need)
{
    if (need <= dp->xfd_node_stack_max)
        return 0;

    size_t newsize = dp->xfd_node_stack_max ? dp->xfd_node_stack_max * 2 : 64;
    while (newsize < need)
        newsize *= 2;

    pin_node_id_t *newp = realloc(dp->xfd_node_stack, newsize * sizeof(*newp));
    if (newp == NULL)
        return -1;

    memset(newp + dp->xfd_node_stack_max, 0,
           (newsize - dp->xfd_node_stack_max) * sizeof(*newp));
    dp->xfd_node_stack = newp;
    dp->xfd_node_stack_max = newsize;
    return 0;
}

/*
 * Intern a name string into the pin namepool at trie-compile time.
 * Returns the paistr atom wrapped in xo_name_id_t.
 */
static xo_name_id_t
pin_filter_data_name_intern (xo_filter_data_t *dp,
			     const char *name, ssize_t len UNUSED)
{
    xo_name_id_t nid = { .ni_id = PA_NULL_ATOM };

    if (name == NULL)
	return nid;

    pin_name_id_t name_id = pin_namepool_atom(dp->xfd_workspace, name, TRUE);
    nid.ni_id = pin_name_id_atom_of(name_id);
    return nid;
}

/*
 * Compare a stored paistr atom against an incoming NUL-terminated tag.
 *
 * We do a lookup-without-create: if the tag has never been interned,
 * it can't match any compiled pattern step, so we return 0 immediately.
 * If it has been interned, we compare atoms -- an integer equality check.
 *
 * Assumes tag is NUL-terminated (guaranteed for pin_source element tokens).
 */
static int
pin_filter_data_name_eq (xo_filter_data_t *dp, xo_name_id_t id,
			 const char *tag, ssize_t len UNUSED)
{
    if (tag == NULL || id.ni_id == PA_NULL_ATOM)
	return 0;

    pin_name_id_t tag_id = pin_namepool_atom(dp->xfd_workspace, tag, FALSE);
    return (!pin_name_id_is_null(tag_id) && pin_name_id_atom_of(tag_id) == id.ni_id);
}

/*
 * Push/pop a retained-node slot in lock-step with the filter's own frame
 * stack.  The slot starts out null_atom (unknown); pin_filter_set_cur_node
 * fills it in once the caller knows the real node id for this depth.
 */
static void
pin_filter_data_event_open (xo_filter_data_t *dp,
			     const char *tag UNUSED, ssize_t tlen UNUSED)
{
    if (pin_filter_data_node_stack_grow(dp, (size_t) dp->xfd_node_depth + 2) < 0)
	return;

    dp->xfd_node_depth += 1;
    dp->xfd_node_stack[dp->xfd_node_depth] = pin_node_id_null_atom();
}

static void
pin_filter_data_event_close (xo_filter_data_t *dp,
			      const char *tag UNUSED, ssize_t tlen UNUSED)
{
    if (dp->xfd_node_depth > 0)
	dp->xfd_node_depth -= 1;
}

/*
 * Resolve a '/'-separated element path from the retained-tree node at the
 * current filter depth, returning its text content copied into
 * xfd_value_buf.  Handles a trailing attribute step (e.g. "b/@x") by
 * resolving the element portion of the path and then looking up the
 * attribute on the resulting node.  Returns NULL if the current depth has
 * no associated node yet, or the path does not resolve.
 */
static const char *
pin_filter_data_value_of_tree (xo_filter_data_t *dp,
				const char *name, ssize_t nlen)
{
    if (dp->xfd_node_depth < 0)
	return NULL;

    pin_node_id_t cur = dp->xfd_node_stack[dp->xfd_node_depth];
    if (pin_node_id_is_null(cur))
	return NULL;

    pin_workspace_t *pwp = dp->xfd_workspace;
    const char *result = NULL;

    /* Trailing attribute step ("a/b/@x" or bare "@x"): split at the '@'. */
    const char *at = memchr(name, '@', (size_t) nlen);
    if (at != NULL) {
	size_t elt_len = (size_t) (at - name);
	if (elt_len > 0 && name[elt_len - 1] == '/')
	    elt_len -= 1;

	pin_node_id_t target = cur;
	if (elt_len > 0)
	    target = pin_exec_node_at_path(pwp, cur, name, elt_len);
	if (pin_node_id_is_null(target))
	    return NULL;

	size_t alen = (size_t) nlen - (size_t) (at - name) - 1;
	char abuf[128];
	if (alen >= sizeof(abuf))
	    return NULL;
	memcpy(abuf, at + 1, alen);
	abuf[alen] = '\0';

	pin_node_t *tnodep = pin_node_addr(pwp, target);
	if (tnodep == NULL)
	    return NULL;
	pin_name_id_t aname = pin_namepool_atom(pwp, abuf, FALSE);
	if (pin_name_id_is_null(aname))
	    return NULL;
	result = pin_get_attrib_string(pwp, tnodep, aname);
	if (result == NULL)
	    return NULL;

    } else {
	pin_node_id_t target = pin_exec_node_at_path(pwp, cur, name,
						      (size_t) nlen);
	if (pin_node_id_is_null(target))
	    return NULL;

	pin_name_id_t text_id = pin_exec_text_of(pwp, target);
	if (pin_name_id_is_null(text_id))
	    return NULL;
	result = pin_namepool_string(pwp, text_id);
	if (result == NULL)
	    return NULL;
    }

    size_t rlen = strlen(result);
    if (pin_filter_data_value_buf_grow(dp, rlen + 1) < 0)
	return NULL;
    memcpy(dp->xfd_value_buf, result, rlen);
    dp->xfd_value_buf[rlen] = '\0';
    return dp->xfd_value_buf;
}

/*
 * Look up the value of a named attribute in the current element's raw
 * attribute string (xfd_attribs), set by pin_filter_set_attribs before
 * each xo_filter_walk_open call.
 *
 * The raw string is NUL-terminated and has the form:
 *   name="value" name2='value2' ...
 * Quotes may be double or single.  Values are copied into xfd_value_buf
 * (NUL-terminated) so the caller gets a stable pointer without allocation.
 *
 * 'name' may also be a '/'-separated compound path (e.g. "life-span/born")
 * naming a descendant element (or its attribute, as a trailing "/@x" step)
 * of the current element.  Such paths can never match a raw XML attribute
 * name (attribute names cannot contain '/' or '@'), so they fall straight
 * through to the retained-tree lookup below.
 */
static const char *
pin_filter_data_value_of (xo_filter_data_t *dp, const char *name, ssize_t nlen)
{
    if (name == NULL)
	return NULL;
    if (nlen < 0)
	nlen = (ssize_t) strlen(name);

    /*
     * Callers (e.g. xo_eval_attribute) mark a bare attribute reference with
     * a leading '@', per pin_filter_data_value_of_tree's convention below.
     * dp->xfd_attribs holds bare (un-prefixed) "name=value" keys, so strip
     * the marker before matching against it; the tree fallback still gets
     * the original (possibly '@'-prefixed) name.
     */
    const char *bare = name;
    ssize_t blen = nlen;
    if (blen > 0 && *bare == '@') {
	bare++;
	blen--;
    }

    const char *cp = dp->xfd_attribs;
    while (cp && *cp) {
	/* skip whitespace */
	while (*cp == ' ' || *cp == '\t' || *cp == '\r' || *cp == '\n')
	    cp++;
	if (!*cp)
	    break;

	/* locate '=' */
	const char *eq = strchr(cp, '=');
	if (eq == NULL)
	    break;

	/* key length, trimming trailing whitespace */
	ssize_t klen = eq - cp;
	while (klen > 0 && (cp[klen - 1] == ' ' || cp[klen - 1] == '\t'))
	    klen--;

	/* parse quoted value */
	const char *vp = eq + 1;
	char quote = *vp;
	if (quote != '"' && quote != '\'') {
	    /* malformed; skip to next space-delimited token */
	    cp = strchr(eq, ' ');
	    continue;
	}
	vp++;
	const char *vend = strchr(vp, quote);
	if (vend == NULL)
	    break;

	if (klen == blen && strncmp(cp, bare, (size_t) blen) == 0) {
	    /* copy value to scratch buffer so caller gets NUL-terminated str */
	    size_t vlen = (size_t)(vend - vp);
	    if (pin_filter_data_value_buf_grow(dp, vlen + 1) < 0)
		return NULL;
	    memcpy(dp->xfd_value_buf, vp, vlen);
	    dp->xfd_value_buf[vlen] = '\0';
	    return dp->xfd_value_buf;
	}

	cp = vend + 1;  /* advance past closing quote */
    }

    /* Not a raw XML attribute of the current element; try the retained tree. */
    return pin_filter_data_value_of_tree(dp, name, nlen);
}

/*
 * Resolve a $name variable reference to its current string value, for use
 * by compiled PIN_OP_EVAL expressions (xo_eval_variable, via xfdo_variable_of).
 * Only the current call frame's local bindings (esp->pes_vars[], set by
 * pin_filter_set_esp before each eval) are consulted; globals are
 * pin_filter_global_of's job.
 *
 * 'name' is not guaranteed to be NUL-terminated, so it is copied into a
 * bounded stack buffer before interning.  Returns NULL if the name is too
 * long, unbound, or has never been interned (and so cannot match any
 * binding), or if no exec state has been set.
 */
static pin_var_binding_t *
pin_filter_data_variable_find (xo_filter_data_t *dp, const char *name,
			       ssize_t nlen)
{
    if (name == NULL || dp->xfd_esp == NULL)
	return NULL;

    if (nlen < 0)
	nlen = (ssize_t) strlen(name);

    char buf[128];
    if (nlen < 0 || (size_t) nlen >= sizeof(buf))
	return NULL;
    memcpy(buf, name, (size_t) nlen);
    buf[nlen] = '\0';

    pin_name_id_t var_name = pin_namepool_atom(dp->xfd_workspace, buf, FALSE);
    if (pin_name_id_is_null(var_name))
	return NULL;

    return pin_exec_var_find(dp->xfd_esp, pin_name_id_atom_of(var_name));
}

static const char *
pin_filter_data_variable_of (xo_filter_data_t *dp, const char *name, ssize_t nlen)
{
    pin_var_binding_t *vp = pin_filter_data_variable_find(dp, name, nlen);
    if (vp == NULL)
	return NULL;

    pin_workspace_t *pwp = dp->xfd_workspace;
    pin_exec_state_t *esp = dp->xfd_esp;
    pin_value_t v = vp->pvb_value;
    const char *result = NULL;

    /*
     * Every branch below returns storage that outlives this call (an
     * interned namepool string, or a static string literal) rather than a
     * buffer shared across calls.  xo_eval's multi-argument functions
     * (e.g. concat()) evaluate all of their arguments -- each a separate
     * call into this function -- before combining the results, so a
     * single shared scratch buffer (as xfd_value_buf is, for the
     * single-value-at-a-time @attr lookups in pin_filter_data_value_of)
     * would have later arguments silently overwrite earlier ones.
     */
    switch (v.pv_type) {
    case PVT_STRING:
	result = pin_namepool_string(pwp, pin_name_id(v.pv_atom));
	break;

    case PVT_BOOLEAN:
	result = (v.pv_flags & PVF_TRUE) ? "true" : "false";
	break;

    case PVT_NUMBER: {
	char numbuf[32];
	snprintf(numbuf, sizeof(numbuf), "%u", v.pv_atom);
	pin_name_id_t id = pin_namepool_atom(pwp, numbuf, TRUE);
	if (!pin_name_id_is_null(id))
	    result = pin_namepool_string(pwp, id);
	break;
    }

    case PVT_NODE: {
	pin_name_id_t text_id = pin_exec_text_of(pwp, pin_node_id(v.pv_atom));
	if (!pin_name_id_is_null(text_id))
	    result = pin_namepool_string(pwp, text_id);
	break;
    }

    case PVT_NODESET: {
	if (v.pv_atom < esp->pes_nodeset_count) {
	    pin_ns_entry_t *ns = &esp->pes_nodesets[v.pv_atom];
	    if (ns->pne_count > 0) {
		pin_name_id_t text_id
		    = pin_exec_text_of(pwp, pin_node_id(ns->pne_nodes[0]));
		if (!pin_name_id_is_null(text_id))
		    result = pin_namepool_string(pwp, text_id);
	    }
	}
	break;
    }

    case PVT_NULL:
    default:
	result = NULL;
	break;
    }

    return result;
}

/*
 * Node-set ops for the eval filter.  libxo's evaluator walks the
 * retained tree through these: a node is a pin node atom and a
 * node-set is a slot in dp->xfd_sets.
 */
static inline xo_eval_node_t
pin_filter_node_handle (pin_node_id_t id)
{
    return pa_fixed_atom_of(pin_node_id_atom_of(id));
}

static inline pin_node_t *
pin_filter_node_addr (xo_filter_data_t *dp, xo_eval_node_t node)
{
    if (node == 0)
	return NULL;

    return pin_node_addr(dp->xfd_workspace, pin_node_id(node));
}

static pin_filter_ns_t *
pin_filter_ns_addr (xo_filter_data_t *dp, xo_eval_ns_t ns)
{
    if (ns == 0 || ns > dp->xfd_sets_count)
	return NULL;

    pin_filter_ns_t *nsp = &dp->xfd_sets[ns - 1];
    return nsp->pfn_refs ? nsp : NULL;
}

static xo_eval_ns_t
pin_filter_ns_alloc (xo_filter_data_t *dp)
{
    uint32_t slot;

    for (slot = 0; slot < dp->xfd_sets_count; slot++)
	if (dp->xfd_sets[slot].pfn_refs == 0)
	    break;

    if (slot == dp->xfd_sets_count) {
	if (dp->xfd_sets_count == dp->xfd_sets_max) {
	    uint32_t newmax = dp->xfd_sets_max ? dp->xfd_sets_max * 2 : 8;
	    pin_filter_ns_t *newp = realloc(dp->xfd_sets,
					     newmax * sizeof(*newp));
	    if (newp == NULL)
		return 0;

	    dp->xfd_sets = newp;
	    dp->xfd_sets_max = newmax;
	}

	memset(&dp->xfd_sets[slot], 0, sizeof(dp->xfd_sets[slot]));
	dp->xfd_sets_count += 1;
    }

    dp->xfd_sets[slot].pfn_count = 0;
    dp->xfd_sets[slot].pfn_refs = 1;
    return slot + 1;
}

static void
pin_filter_ns_free (xo_filter_data_t *dp, xo_eval_ns_t ns)
{
    pin_filter_ns_t *nsp = pin_filter_ns_addr(dp, ns);
    if (nsp == NULL)
	return;

    nsp->pfn_refs -= 1;
    if (nsp->pfn_refs == 0)
	nsp->pfn_count = 0;
}

static int
pin_filter_ns_append_id (pin_filter_ns_t *nsp, pin_node_id_t id)
{
    if (nsp->pfn_count == nsp->pfn_max) {
	uint32_t newmax = nsp->pfn_max ? nsp->pfn_max * 2 : 8;
	pin_node_id_t *newp = realloc(nsp->pfn_nodes, newmax * sizeof(*newp));
	if (newp == NULL)
	    return -1;

	nsp->pfn_nodes = newp;
	nsp->pfn_max = newmax;
    }

    nsp->pfn_nodes[nsp->pfn_count] = id;
    nsp->pfn_count += 1;
    return 0;
}

static int
pin_filter_ns_append (xo_filter_data_t *dp, xo_eval_ns_t ns,
		      xo_eval_node_t node)
{
    pin_filter_ns_t *nsp = pin_filter_ns_addr(dp, ns);
    if (nsp == NULL || node == 0)
	return -1;

    return pin_filter_ns_append_id(nsp, pin_node_id(node));
}

static uint32_t
pin_filter_ns_count (xo_filter_data_t *dp, xo_eval_ns_t ns)
{
    pin_filter_ns_t *nsp = pin_filter_ns_addr(dp, ns);
    return nsp ? nsp->pfn_count : 0;
}

static xo_eval_node_t
pin_filter_ns_get (xo_filter_data_t *dp, xo_eval_ns_t ns, uint32_t idx)
{
    pin_filter_ns_t *nsp = pin_filter_ns_addr(dp, ns);
    if (nsp == NULL || idx >= nsp->pfn_count)
	return 0;

    return pin_filter_node_handle(nsp->pfn_nodes[idx]);
}

static xo_eval_ns_t
pin_filter_ns_copy (xo_filter_data_t *dp, xo_eval_ns_t ns)
{
    pin_filter_ns_t *nsp = pin_filter_ns_addr(dp, ns);
    if (nsp == NULL)
	return 0;

    nsp->pfn_refs += 1;
    return ns;
}

/*
 * The node test for one step.  The principal node type is attribute
 * on the attribute axis and element everywhere else, so "*" and a
 * name only match that type.
 */
typedef struct pin_filter_test_s {
    xo_eval_ntest_t pft_ntest;	/* XO_NTEST_* */
    pin_node_type_t pft_principal; /* PIN_TYPE_ELT or PIN_TYPE_ATTRIB */
    pin_name_id_t pft_name;	/* XO_NTEST_NAME: the name */
} pin_filter_test_t;

static int
pin_filter_test_node (const pin_filter_test_t *ptp, pin_node_t *np)
{
    switch (ptp->pft_ntest) {
    case XO_NTEST_NODE:
	return 1;

    case XO_NTEST_TEXT:
	return np->pn_type == PIN_TYPE_TEXT || np->pn_type == PIN_TYPE_UNESC;

    case XO_NTEST_STAR:
	return np->pn_type == ptp->pft_principal;

    case XO_NTEST_NAME:
	return np->pn_type == ptp->pft_principal
	    && pin_name_id_equal(np->pn_name, ptp->pft_name);
    }

    return 0;
}

/*
 * Attributes and namespace nodes are children in the store, but they
 * aren't on the child or descendant axes.
 */
static inline int
pin_filter_is_child_type (pin_node_type_t type)
{
    return type != PIN_TYPE_ATTRIB && type != PIN_TYPE_NS
	&& type != PIN_TYPE_ATSTR && type != PIN_TYPE_NSPREF;
}

static int
pin_filter_step_add (pin_filter_ns_t *nsp, const pin_filter_test_t *ptp,
		     pin_node_id_t id, pin_node_t *np)
{
    if (!pin_filter_test_node(ptp, np))
	return 0;

    return pin_filter_ns_append_id(nsp, id);
}

static int
pin_filter_step_descend (pin_workspace_t *pwp, pin_filter_ns_t *nsp,
			 const pin_filter_test_t *ptp, pin_node_t *np)
{
    pin_node_id_t cid = pin_node_child(np);

    while (!pin_node_id_is_null(cid)) {
	pin_node_t *child = pin_node_addr(pwp, cid);
	if (child == NULL || child->pn_depth <= np->pn_depth)
	    break;

	if (pin_filter_is_child_type(child->pn_type)) {
	    if (pin_filter_step_add(nsp, ptp, cid, child) < 0)
		return -1;
	    if (pin_filter_step_descend(pwp, nsp, ptp, child) < 0)
		return -1;
	}

	cid = child->pn_next;
    }

    return 0;
}

static int
pin_filter_step (xo_filter_data_t *dp, xo_eval_node_t node,
		 xo_eval_axis_t axis, xo_eval_ntest_t ntest,
		 const char *name, ssize_t len, xo_eval_ns_t ns)
{
    pin_workspace_t *pwp = dp->xfd_workspace;
    pin_filter_ns_t *nsp = pin_filter_ns_addr(dp, ns);
    pin_node_t *np = pin_filter_node_addr(dp, node);
    if (nsp == NULL || np == NULL)
	return -1;

    pin_node_id_t id = pin_node_id(node);
    pin_filter_test_t test = {
	.pft_ntest = ntest,
	.pft_principal = (axis == XO_AXIS_ATTRIBUTE)
	    ? PIN_TYPE_ATTRIB : PIN_TYPE_ELT,
	.pft_name = pin_name_id_null_atom(),
    };

    if (ntest == XO_NTEST_NAME) {
	if (name == NULL || len < 0 || len >= 1024)
	    return -1;

	char nbuf[len + 1];
	memcpy(nbuf, name, (size_t) len);
	nbuf[len] = '\0';

	/* A name that was never interned can't match any node */
	test.pft_name = pin_namepool_atom(pwp, nbuf, FALSE);
	if (pin_name_id_is_null(test.pft_name))
	    return 0;
    }

    switch (axis) {
    case XO_AXIS_SELF:
	return pin_filter_step_add(nsp, &test, id, np);

    case XO_AXIS_CHILD:
    case XO_AXIS_ATTRIBUTE:;
	pin_boolean_t want_attr = (axis == XO_AXIS_ATTRIBUTE);
	pin_node_id_t cid = pin_node_child(np);

	while (!pin_node_id_is_null(cid)) {
	    pin_node_t *child = pin_node_addr(pwp, cid);
	    if (child == NULL || child->pn_depth <= np->pn_depth)
		break;

	    if (want_attr ? child->pn_type == PIN_TYPE_ATTRIB
		    : pin_filter_is_child_type(child->pn_type)) {
		if (pin_filter_step_add(nsp, &test, cid, child) < 0)
		    return -1;
	    }

	    cid = child->pn_next;
	}
	return 0;

    case XO_AXIS_PARENT:
	if (pin_node_id_is_null(np->pn_parent))
	    return 0;

	return pin_filter_step_add(nsp, &test, np->pn_parent,
				   pin_node_addr(pwp, np->pn_parent));

    case XO_AXIS_ANCESTOR:
    case XO_AXIS_ANCESTOR_OR_SELF:
	if (axis == XO_AXIS_ANCESTOR_OR_SELF
		&& pin_filter_step_add(nsp, &test, id, np) < 0)
	    return -1;

	for (pin_node_id_t pid = np->pn_parent; !pin_node_id_is_null(pid); ) {
	    pin_node_t *pnp = pin_node_addr(pwp, pid);
	    if (pnp == NULL)
		break;
	    if (pin_filter_step_add(nsp, &test, pid, pnp) < 0)
		return -1;
	    pid = pnp->pn_parent;
	}
	return 0;

    case XO_AXIS_DESCENDANT:
    case XO_AXIS_DESCENDANT_OR_SELF:
	if (axis == XO_AXIS_DESCENDANT_OR_SELF
		&& pin_filter_step_add(nsp, &test, id, np) < 0)
	    return -1;

	return pin_filter_step_descend(pwp, nsp, &test, np);
    }

    return -1;
}

/*
 * The string value: an element or root gives the text of all its
 * descendants, in document order; text and attributes give their own.
 */
static void
pin_filter_text_collect (pin_workspace_t *pwp, pin_node_t *np,
			 xo_buffer_t *xbp)
{
    pin_node_id_t cid = pin_node_child(np);

    while (!pin_node_id_is_null(cid)) {
	pin_node_t *child = pin_node_addr(pwp, cid);
	if (child == NULL || child->pn_depth <= np->pn_depth)
	    break;

	if (child->pn_type == PIN_TYPE_TEXT
		|| child->pn_type == PIN_TYPE_UNESC) {
	    const char *text = pin_textpool_string(pwp,
				    pa_arb_atom_of(pin_node_text(child)));
	    if (text)
		xo_buf_append(xbp, text, (ssize_t) strlen(text));
	} else if (child->pn_type == PIN_TYPE_ELT) {
	    pin_filter_text_collect(pwp, child, xbp);
	}

	cid = child->pn_next;
    }
}

static const char *
pin_filter_string_value (xo_filter_data_t *dp, xo_eval_node_t node)
{
    pin_workspace_t *pwp = dp->xfd_workspace;
    pin_node_t *np = pin_filter_node_addr(dp, node);
    if (np == NULL)
	return "";

    switch (np->pn_type) {
    case PIN_TYPE_TEXT:
    case PIN_TYPE_UNESC:
    case PIN_TYPE_ATTRIB:;
	const char *text = pin_textpool_string(pwp,
				pa_arb_atom_of(pin_node_text(np)));
	return text ? text : "";

    case PIN_TYPE_ELT:
    case PIN_TYPE_ROOT:;
	xo_buffer_t *xbp = &dp->xfd_string_buf;
	xo_buf_reset(xbp);
	pin_filter_text_collect(pwp, np, xbp);
	xo_buf_append(xbp, "", 1);
	return xo_buf_data(xbp, 0);
    }

    return "";
}

static const char *
pin_filter_node_name (xo_filter_data_t *dp, xo_eval_node_t node)
{
    pin_node_t *np = pin_filter_node_addr(dp, node);
    if (np == NULL || pin_name_id_is_null(np->pn_name))
	return "";

    if (np->pn_type != PIN_TYPE_ELT && np->pn_type != PIN_TYPE_ATTRIB
	    && np->pn_type != PIN_TYPE_PI)
	return "";

    const char *name = pin_namepool_string(dp->xfd_workspace, np->pn_name);
    return name ? name : "";
}

static uint32_t
pin_filter_node_height (pin_workspace_t *pwp, pin_node_id_t id)
{
    uint32_t height = 0;

    for (;;) {
	pin_node_t *np = pin_node_addr(pwp, id);
	if (np == NULL || pin_node_id_is_null(np->pn_parent))
	    break;
	id = np->pn_parent;
	height += 1;
    }

    return height;
}

static inline pin_node_id_t
pin_filter_node_parent (pin_workspace_t *pwp, pin_node_id_t id)
{
    pin_node_t *np = pin_node_addr(pwp, id);
    return np ? np->pn_parent : pin_node_id_null_atom();
}

/*
 * Document order: lift the deeper node to the other's height, then
 * both together until they share a parent, and see which of those two
 * siblings comes first.  An ancestor comes before its descendants.
 * Attributes are the first children in the store, so they come before
 * the element's other children, as XPath wants.  Nodes in different
 * trees are ordered by their roots.
 */
static int
pin_filter_doc_compare (xo_filter_data_t *dp, xo_eval_node_t node1,
			xo_eval_node_t node2)
{
    pin_workspace_t *pwp = dp->xfd_workspace;
    pin_node_id_t id1 = pin_node_id(node1);
    pin_node_id_t id2 = pin_node_id(node2);

    if (node1 == node2)
	return 0;

    uint32_t height1 = pin_filter_node_height(pwp, id1);
    uint32_t height2 = pin_filter_node_height(pwp, id2);

    for ( ; height1 > height2; height1--)
	id1 = pin_filter_node_parent(pwp, id1);
    for ( ; height2 > height1; height2--)
	id2 = pin_filter_node_parent(pwp, id2);

    if (pin_node_id_equal(id1, id2))
	return (node1 == pin_filter_node_handle(id1)) ? -1 : 1;

    for (;;) {
	pin_node_id_t parent1 = pin_filter_node_parent(pwp, id1);
	pin_node_id_t parent2 = pin_filter_node_parent(pwp, id2);
	if (pin_node_id_equal(parent1, parent2))
	    break;
	id1 = parent1;
	id2 = parent2;
    }

    pin_node_t *pnp = pin_node_addr(pwp, pin_filter_node_parent(pwp, id1));
    if (pnp == NULL) {
	xo_eval_node_t root1 = pin_filter_node_handle(id1);
	xo_eval_node_t root2 = pin_filter_node_handle(id2);
	return (root1 < root2) ? -1 : 1;
    }

    pin_node_id_t cid = pin_node_child(pnp);
    while (!pin_node_id_is_null(cid)) {
	if (pin_node_id_equal(cid, id1))
	    return -1;
	if (pin_node_id_equal(cid, id2))
	    return 1;

	pin_node_t *child = pin_node_addr(pwp, cid);
	if (child == NULL || child->pn_depth <= pnp->pn_depth)
	    break;
	cid = child->pn_next;
    }

    return 0;
}

/*
 * The value of the global variable "name", or NULL.  A global whose
 * value is a reference to another global ("$other") resolves one
 * level, as PIN_OP_LOAD_VAR does.
 */
static const char *
pin_filter_global_of (xo_filter_data_t *dp, const char *name, ssize_t nlen)
{
    pin_rulebook_t *prbp = dp->xfd_rulebook;
    pin_workspace_t *pwp = dp->xfd_workspace;

    if (prbp == NULL || name == NULL)
	return NULL;
    if (nlen < 0)
	nlen = (ssize_t) strlen(name);

    char buf[128];
    if ((size_t) nlen >= sizeof(buf))
	return NULL;
    memcpy(buf, name, (size_t) nlen);
    buf[nlen] = '\0';

    pin_name_id_t nid = pin_namepool_atom(pwp, buf, FALSE);
    if (pin_name_id_is_null(nid))
	return NULL;

    pin_name_id_t gval = pin_rulebook_global_find(prbp, nid);
    if (!pin_name_id_is_null(gval))
	return pin_namepool_string(pwp, gval);

    pin_name_id_t expr = pin_rulebook_global_expr_find(prbp, nid);
    const char *estr = pin_namepool_string(pwp, expr);
    if (estr == NULL || estr[0] != '$')
	return NULL;

    nid = pin_namepool_atom(pwp, estr + 1, FALSE);
    gval = pin_rulebook_global_find(prbp, nid);

    return pin_name_id_is_null(gval) ? NULL : pin_namepool_string(pwp, gval);
}

/*
 * A variable for the eval filter: a node or node-set variable gives
 * libxo a node-set, so paths can start from it ("$dot/@location").
 * Other values go through pin_filter_data_variable_of as strings.
 */
static xo_eval_ext_value_t
pin_filter_variable_value (xo_filter_data_t *dp, const char *name,
			   ssize_t nlen)
{
    xo_eval_ext_value_t rv = { .xee_type = XO_EVAL_EXT_MISSING };
    pin_var_binding_t *vp = pin_filter_data_variable_find(dp, name, nlen);
    if (vp == NULL) {
	rv.xee_str = pin_filter_global_of(dp, name, nlen);
	if (rv.xee_str)
	    rv.xee_type = XO_EVAL_EXT_STRING;
	return rv;
    }

    pin_value_t v = vp->pvb_value;
    pin_exec_state_t *esp = dp->xfd_esp;

    if (v.pv_type == PVT_NODE || v.pv_type == PVT_NODESET) {
	xo_eval_ns_t ns = pin_filter_ns_alloc(dp);
	pin_filter_ns_t *nsp = pin_filter_ns_addr(dp, ns);
	if (nsp == NULL)
	    return rv;

	if (v.pv_type == PVT_NODE) {
	    pin_filter_ns_append_id(nsp, pin_node_id(v.pv_atom));
	} else if (v.pv_atom < esp->pes_nodeset_count) {
	    pin_ns_entry_t *nep = &esp->pes_nodesets[v.pv_atom];
	    for (uint32_t i = 0; i < nep->pne_count; i++)
		pin_filter_ns_append_id(nsp, pin_node_id(nep->pne_nodes[i]));
	}

	rv.xee_type = XO_EVAL_EXT_NODESET;
	rv.xee_ns = ns;
	return rv;
    }

    const char *str = pin_filter_data_variable_of(dp, name, nlen);
    if (str != NULL) {
	rv.xee_type = XO_EVAL_EXT_STRING;
	rv.xee_str = str;
    }

    return rv;
}

/*
 * The dynamic-loading provider's callbacks (libslax, via
 * pin_set_dyn_ops(), typically called once from slaxEnable()), or NULL
 * if none has been registered (e.g. libslax was never linked/
 * initialized -- the normal, fully-supported state for a libpin-only
 * application).
 */
static const pin_dyn_ops_t *pin_dyn_ops;

void
pin_set_dyn_ops (const pin_dyn_ops_t *ops)
{
    pin_dyn_ops = ops;
}

/*
 * Split a possibly-prefixed XPath function name ("slax:printf") into
 * its prefix and local parts. Pure string work -- no namespace lookup.
 * Returns FALSE if 'name' has no ':', meaning it isn't a namespace-
 * prefixed call at all and so isn't ours to resolve, or if either part
 * doesn't fit in the caller's buffer.
 */
static int
pin_split_qname (const char *name, char *prefix, size_t prefix_max,
		  char *local, size_t local_max)
{
    const char *colon = strchr(name, ':');
    if (colon == NULL)
	return FALSE;

    size_t plen = (size_t) (colon - name);
    size_t llen = strlen(colon + 1);
    if (plen == 0 || plen >= prefix_max || llen == 0 || llen >= local_max)
	return FALSE;

    memcpy(prefix, name, plen);
    prefix[plen] = '\0';
    memcpy(local, colon + 1, llen);
    local[llen] = '\0';
    return TRUE;
}

/*
 * xfdo_find_function hook: resolve a namespace-prefixed function name
 * (e.g. "slax:printf") that the compiled evaluator's built-in table
 * doesn't recognize. A thin forwarder to the pin_dyn_ops_t provider, if
 * any -- no libxml2-XPath/libxslt/libslax symbol is touched here; see
 * build/plans/slax-funcs.rst, Part 2 point 4.
 *
 * Resolution order: first, search the in-scope prefix/uri bindings
 * recorded via pin_filter_set_compile_ns, searched from the end
 * (innermost/most-recent) backward so the nearest declaration wins,
 * exactly like ordinary XML prefix-shadowing. Only when that misses --
 * no bindings were recorded, or none match 'prefix' -- fall back to the
 * pin_dyn_ops_t provider's pdo_resolve_prefix. This matches Concrete
 * step 5 / Design step 2a in the plan.
 *
 * This hook runs twice per call site: once at compile time (to learn
 * the arity) and again at runtime (xo_eval_func_external re-resolves
 * rather than caching -- see its doc comment in xo_filter.c), both
 * against the shared rulebook-wide eval filter. The bindings in scope
 * therefore can't be the compiler's own transient namespace stack --
 * it's freed once compilation finishes -- so pin_op_push_eval snapshots
 * it into the rulebook (pin_rulebook_eval_prog_add's ns_bindings/
 * ns_count) and pin_op_eval (pin_exec.c) reinstates that program's own
 * snapshot via pin_filter_set_compile_ns before every run.
 *
 * Note: this in-scope lookup only runs for call sites that record
 * bindings (currently pin_op_push_eval, for "real" XPath expressions
 * -- select=/test=/etc.). The match-pattern predicate compile path
 * (xo_filter_pred_prog, lazily compiling predicates found in
 * xsl:template match= patterns) does not yet thread a namespace stack
 * through to its compile call, so prefixed function calls inside
 * pattern predicates always fall straight through to
 * pdo_resolve_prefix -- a known, flagged gap, not an oversight.
 */
/*
 * Name/desc/arghelp of the most recent successful pdo_find_function
 * resolution, stashed here because pin_filter_find_function's own
 * signature is fixed by xo_filter.h's xfdo_find_function vtable member
 * (nargsp/min_argsp only -- no room for a hint).  pin_op_push_eval
 * reads these back, via pin_filter_last_function_help() below, to build
 * a "consider: ..." hint when a compile fails on an arity mismatch for
 * this function.  Only meaningful immediately after a failed compile;
 * a later, unrelated runtime re-resolution (see this function's own
 * doc comment) may silently overwrite it, but by then any diagnostic
 * for the earlier compile failure has already been reported.
 */
static char pin_filter_last_func_name[128];
static const char *pin_filter_last_func_desc;
static const char *pin_filter_last_func_arghelp;
static int pin_filter_last_func_set;

void
pin_filter_last_function_help (const char **namep, const char **descp,
				const char **arghelpp)
{
    if (!pin_filter_last_func_set) {
	if (namep)
	    *namep = NULL;
	if (descp)
	    *descp = NULL;
	if (arghelpp)
	    *arghelpp = NULL;
	return;
    }

    if (namep)
	*namep = pin_filter_last_func_name;
    if (descp)
	*descp = pin_filter_last_func_desc;
    if (arghelpp)
	*arghelpp = pin_filter_last_func_arghelp;
}

static xo_eval_ext_fn_t
pin_filter_find_function (xo_filter_data_t *dp, const char *name,
			   int *nargsp, int *min_argsp)
{
    char prefix[64], local[64], uri[256];

    if (!pin_split_qname(name, prefix, sizeof(prefix), local, sizeof(local)))
	return NULL;			/* no colon; not ours */

    const char *found = NULL;

    if (dp != NULL && dp->xfd_compile_ns != NULL) {
	for (size_t i = dp->xfd_compile_ns_count; i > 0; i--) {
	    const pin_ns_map_t *nsp = &dp->xfd_compile_ns[i - 1];
	    const char *pfx = pin_namepool_string(dp->xfd_workspace,
						   nsp->pnm_prefix);
	    if (pfx != NULL && strcmp(pfx, prefix) == 0) {
		found = pin_namepool_string(dp->xfd_workspace, nsp->pnm_uri);
		break;
	    }
	}
    }

    if (found == NULL) {
	if (pin_dyn_ops == NULL || pin_dyn_ops->pdo_resolve_prefix == NULL)
	    return NULL;		/* no provider registered */

	found = pin_dyn_ops->pdo_resolve_prefix(prefix);
	if (found == NULL)
	    return NULL;		/* unknown prefix */
    }

    size_t ulen = strlen(found);
    if (ulen >= sizeof(uri))
	return NULL;
    memcpy(uri, found, ulen + 1);

    if (pin_dyn_ops == NULL || pin_dyn_ops->pdo_find_function == NULL)
	return NULL;

    const char *desc = NULL, *arghelp = NULL;
    xo_eval_ext_fn_t fn = pin_dyn_ops->pdo_find_function(local, uri, nargsp,
							  min_argsp, &desc,
							  &arghelp);
    if (fn != NULL) {
	snprintf(pin_filter_last_func_name, sizeof(pin_filter_last_func_name),
		 "%s", name);
	pin_filter_last_func_desc = desc;
	pin_filter_last_func_arghelp = arghelp;
	pin_filter_last_func_set = 1;
    } else {
	pin_filter_last_func_set = 0;
    }

    return fn;
}

/*
 * Formatted message from the most recent xo_eval_compile()/evaluation
 * failure reported through this backend, stashed here because this is
 * the wiring that finally gives pin's xo_filter_data_ops_t an
 * xfdo_error -- without it, xo_filter_data_verror() finds ops->
 * xfdo_error NULL and drops the message (see xo_filter.c). Cleared by
 * pin_filter_clear_compile_error(); callers should clear before a
 * compile and check pin_filter_last_compile_error() immediately after
 * a failure, since any later compile or evaluation overwrites it.
 */
static char pin_filter_last_error[2048];
static int pin_filter_last_error_set;

static void
pin_filter_data_error (xo_filter_data_t *dp UNUSED, const char *fmt,
			va_list vap)
{
    vsnprintf(pin_filter_last_error, sizeof(pin_filter_last_error), fmt, vap);
    pin_filter_last_error_set = 1;
}

const char *
pin_filter_last_compile_error (void)
{
    return pin_filter_last_error_set ? pin_filter_last_error : NULL;
}

void
pin_filter_clear_compile_error (void)
{
    pin_filter_last_error_set = 0;
}

static xo_filter_data_ops_t pin_filter_data_ops = {
    .xfdo_version      = XO_FILTER_DATA_OPS_VERSION,
    .xfdo_realloc      = pin_filter_data_realloc,
    .xfdo_free         = pin_filter_data_free,
    .xfdo_name_intern  = pin_filter_data_name_intern,
    .xfdo_name_eq      = pin_filter_data_name_eq,
    .xfdo_value_of     = pin_filter_data_value_of,
    .xfdo_variable_of  = pin_filter_data_variable_of,
    .xfdo_event_open   = pin_filter_data_event_open,
    .xfdo_event_close  = pin_filter_data_event_close,
    .xfdo_destroy      = pin_filter_data_destroy,
    .xfdo_find_function = pin_filter_find_function,
    .xfdo_error        = pin_filter_data_error,
};

/*
 * The eval filter's ops add node-sets.  Match, mode and if filters
 * run their predicates without a context node, so they keep the
 * plain ops and its value_of lookups.
 */
static xo_filter_data_ops_t pin_filter_eval_ops = {
    .xfdo_version      = XO_FILTER_DATA_OPS_VERSION,
    .xfdo_realloc      = pin_filter_data_realloc,
    .xfdo_free         = pin_filter_data_free,
    .xfdo_name_intern  = pin_filter_data_name_intern,
    .xfdo_name_eq      = pin_filter_data_name_eq,
    .xfdo_value_of     = pin_filter_data_value_of,
    .xfdo_variable_of  = pin_filter_data_variable_of,
    .xfdo_event_open   = pin_filter_data_event_open,
    .xfdo_event_close  = pin_filter_data_event_close,
    .xfdo_destroy      = pin_filter_data_destroy,
    .xfdo_find_function = pin_filter_find_function,
    .xfdo_error        = pin_filter_data_error,
    .xfdo_ns_alloc     = pin_filter_ns_alloc,
    .xfdo_ns_free      = pin_filter_ns_free,
    .xfdo_ns_append    = pin_filter_ns_append,
    .xfdo_ns_count     = pin_filter_ns_count,
    .xfdo_ns_get       = pin_filter_ns_get,
    .xfdo_ns_copy      = pin_filter_ns_copy,
    .xfdo_step         = pin_filter_step,
    .xfdo_string_value = pin_filter_string_value,
    .xfdo_node_name    = pin_filter_node_name,
    .xfdo_doc_compare  = pin_filter_doc_compare,
    .xfdo_variable_value = pin_filter_variable_value,
};

/*
 * Public API
 */

int
pin_filter_add (xo_filter_t *xfp, const char *xpath)
{
    return xo_filter_walk_add(NULL, xfp, xpath);
}

int
pin_filter_add_with_action (xo_filter_t *xfp, const char *xpath,
			     pin_rule_id_t rid, double priority,
			     int16_t import_prec)
{
    uint32_t action = pa_fixed_atom_of(pin_rule_id_atom_of(rid));
    return xo_filter_walk_add_with_action_priority(NULL, xfp, xpath, action,
						    priority, import_prec);
}

static xo_filter_t *
pin_filter_create_ops (xo_handle_t *xop, pin_workspace_t *pwp,
		       xo_filter_data_ops_t *ops)
{
    xo_filter_data_t *dp = calloc(1, sizeof(*dp));
    if (dp == NULL)
	return NULL;

    dp->xfd_workspace = pwp;
    xo_buf_init(&dp->xfd_string_buf);

    if (pin_filter_data_node_stack_grow(dp, 1) < 0) {
	pin_filter_data_destroy(dp);
	return NULL;
    }

    xo_filter_t *xfp = xo_filter_create_with_data(xop, dp, ops);
    if (xfp == NULL) {
	pin_filter_data_destroy(dp);
	return NULL;
    }

    return xfp;
}

xo_filter_t *
pin_filter_create (xo_handle_t *xop, pin_workspace_t *pwp)
{
    return pin_filter_create_ops(xop, pwp, &pin_filter_data_ops);
}

xo_filter_t *
pin_filter_create_eval (xo_handle_t *xop, pin_workspace_t *pwp)
{
    return pin_filter_create_ops(xop, pwp, &pin_filter_eval_ops);
}

/*
 * The node id libxo's evaluator uses for a retained-tree node
 */
uint32_t
pin_filter_eval_node (pin_node_id_t id)
{
    return pin_filter_node_handle(id);
}

/*
 * Set the raw attribute string for the element about to be opened.
 * Call this immediately before xo_filter_walk_open so that predicate
 * evaluation via xfdo_value_of can resolve attribute values.
 * 'attribs' may be NULL when the element has no attributes.
 */
void
pin_filter_set_attribs (xo_filter_t *xfp, const char *attribs)
{
    xo_filter_data_t *dp = xo_filter_get_data_ptr(xfp);
    if (dp)
	dp->xfd_attribs = attribs;
}

/*
 * Record the retained-tree node id for the element currently open at the
 * filter's current depth, so that multi-element predicate paths (e.g.
 * "life-span/born") can be resolved against the mmap tree via
 * pin_filter_data_value_of.  Call this once pin_parse has created (or
 * reused) the node for the element most recently passed to
 * xo_filter_walk_open/pin_filter_walk_open; there is no requirement to
 * call it before that open, since xfdo_event_open already reserves the
 * slot with a null node id (yielding "not found" predicate lookups until
 * this is called).
 */
void
pin_filter_set_cur_node (xo_filter_t *xfp, pin_node_id_t node)
{
    xo_filter_data_t *dp = xo_filter_get_data_ptr(xfp);
    if (dp == NULL || dp->xfd_node_depth < 0)
	return;

    if ((size_t) dp->xfd_node_depth < dp->xfd_node_stack_max)
	dp->xfd_node_stack[dp->xfd_node_depth] = node;
}

/*
 * Set the rulebook whose global variables back $name references that
 * no local binding answers.
 */
void
pin_filter_set_rulebook (xo_filter_t *xfp, struct pin_rulebook_s *prbp)
{
    xo_filter_data_t *dp = xo_filter_get_data_ptr(xfp);
    if (dp)
	dp->xfd_rulebook = prbp;
}

/*
 * Set the exec state that xfdo_variable_of resolves $name references
 * against.  Call this before each PIN_OP_EVAL run (pin_op_eval); 'esp'
 * may be NULL, which makes every $name reference evaluate as missing.
 */
void
pin_filter_set_esp (xo_filter_t *xfp, pin_exec_state_t *esp)
{
    xo_filter_data_t *dp = xo_filter_get_data_ptr(xfp);
    if (dp)
	dp->xfd_esp = esp;
}

/*
 * Record the in-scope prefix/uri bindings an XPath expression is being
 * compiled "at" (see pin_filter.h for the full doc comment and
 * pin_filter_find_function for how it's used).
 */
void
pin_filter_set_compile_ns (xo_filter_t *xfp, const pin_ns_map_t *bindings,
			    size_t count)
{
    xo_filter_data_t *dp = xo_filter_get_data_ptr(xfp);
    if (dp) {
	dp->xfd_compile_ns = bindings;
	dp->xfd_compile_ns_count = count;
    }
}
