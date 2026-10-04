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
#include <sys/types.h>

#include "slaxconfig.h"
#include <libpsu/psulog.h>
#include <parrotdb/pacommon.h>
#include <parrotdb/paconfig.h>
#include <parrotdb/pammap.h>
#include <parrotdb/pafixed.h>

#include <libxo/xo.h>
#include "xo_filter.h"

#include <libpin/pin_common.h>
#include <libpin/pin_workspace.h>
#include <libpin/pin_exec.h>
#include <libpin/pin_filter.h>

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
 * open event fires.  A slot left null_atom (e.g. inside a discard frame,
 * where no tree node is ever created) simply yields "not found" lookups.
 */
struct xo_filter_data_s {
    pin_workspace_t *xfd_workspace; /* namepool and mmap backing */
    const char *xfd_attribs;        /* raw attr string for current token */
    char *xfd_value_buf;            /* scratch space for xfdo_value_of result */
    size_t xfd_value_buf_max;       /* allocated size of xfd_value_buf */
    pin_node_id_t *xfd_node_stack;  /* retained node per depth */
    size_t xfd_node_stack_max;      /* allocated slot count of xfd_node_stack */
    int xfd_node_depth;             /* index of the current (top) slot */
};

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

	if (klen == nlen && strncmp(cp, name, (size_t) nlen) == 0) {
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

static xo_filter_data_ops_t pin_filter_data_ops = {
    .xfdo_version      = XO_FILTER_DATA_OPS_VERSION,
    .xfdo_realloc      = pin_filter_data_realloc,
    .xfdo_free         = pin_filter_data_free,
    .xfdo_name_intern  = pin_filter_data_name_intern,
    .xfdo_name_eq      = pin_filter_data_name_eq,
    .xfdo_value_of     = pin_filter_data_value_of,
    .xfdo_event_open   = pin_filter_data_event_open,
    .xfdo_event_close  = pin_filter_data_event_close,
    .xfdo_destroy      = pin_filter_data_destroy,
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

xo_filter_t *
pin_filter_create (xo_handle_t *xop, pin_workspace_t *pwp)
{
    xo_filter_data_t *dp = calloc(1, sizeof(*dp));
    if (dp == NULL)
	return NULL;

    dp->xfd_workspace = pwp;

    if (pin_filter_data_node_stack_grow(dp, 1) < 0) {
	pin_filter_data_destroy(dp);
	return NULL;
    }

    xo_filter_t *xfp = xo_filter_create_with_data(xop, dp, &pin_filter_data_ops);
    if (xfp == NULL) {
	pin_filter_data_destroy(dp);
	return NULL;
    }

    return xfp;
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
