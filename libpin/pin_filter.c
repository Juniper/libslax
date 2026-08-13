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
#include <libpin/pin_filter.h>

/*
 * Real definition of xo_filter_data_t for the pin backend.
 * Only this translation unit sees the layout; the filter core in
 * xo_filter.so sees only the forward declaration from xo_filter.h.
 */
struct xo_filter_data_s {
    pin_workspace_t *xfd_workspace; /* namepool and mmap backing */
    const char *xfd_attribs;        /* raw attr string for current token */
    char xfd_value_buf[512];        /* scratch space for xfdo_value_of result */
};

/* ------------------------------------------------------------------ */
/* Data vtable implementation                                          */
/* ------------------------------------------------------------------ */

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
 * Intern a name string into the pin namepool at trie-compile time.
 * Returns the paistr atom wrapped in xo_name_id_t.
 */
static xo_name_id_t
pin_filter_data_name_intern (xo_filter_data_t *dp,
			     const char *name, ssize_t len UNUSED)
{
    xo_name_id_t nid = { .id = PA_NULL_ATOM };

    if (name == NULL)
	return nid;

    pa_atom_t atom = pin_namepool_atom(dp->xfd_workspace, name, TRUE);
    nid.id = atom;
    return nid;
}

/*
 * Compare a stored paistr atom against an incoming NUL-terminated tag.
 *
 * We do a lookup-without-create: if the tag has never been interned,
 * it can't match any compiled pattern step, so we return 0 immediately.
 * If it has been interned, we compare atoms — an integer equality check.
 *
 * Assumes tag is NUL-terminated (guaranteed for pin_source element tokens).
 */
static int
pin_filter_data_name_eq (xo_filter_data_t *dp, xo_name_id_t id,
			 const char *tag, ssize_t len UNUSED)
{
    if (tag == NULL || id.id == PA_NULL_ATOM)
	return 0;

    pa_atom_t atom = pin_namepool_atom(dp->xfd_workspace, tag, FALSE);
    return (atom != PA_NULL_ATOM && atom == id.id);
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
 */
static const char *
pin_filter_data_value_of (xo_filter_data_t *dp, const char *name, ssize_t nlen)
{
    if (dp->xfd_attribs == NULL || name == NULL)
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
	    if (vlen >= sizeof(dp->xfd_value_buf))
		vlen = sizeof(dp->xfd_value_buf) - 1;
	    memcpy(dp->xfd_value_buf, vp, vlen);
	    dp->xfd_value_buf[vlen] = '\0';
	    return dp->xfd_value_buf;
	}

	cp = vend + 1;  /* advance past closing quote */
    }
    return NULL;
}

static xo_filter_data_ops_t pin_filter_data_ops = {
    .xfdo_version     = XO_FILTER_DATA_OPS_VERSION,
    .xfdo_realloc     = pin_filter_data_realloc,
    .xfdo_free        = pin_filter_data_free,
    .xfdo_name_intern = pin_filter_data_name_intern,
    .xfdo_name_eq     = pin_filter_data_name_eq,
    .xfdo_value_of    = pin_filter_data_value_of,
};

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int
pin_filter_add (xo_filter_t *xfp, const char *xpath)
{
    return xo_filter_walk_add(NULL, xfp, xpath);
}

int
pin_filter_add_with_action (xo_filter_t *xfp, const char *xpath,
			     pin_rule_id_t rid)
{
    uint32_t action = pa_fixed_atom_of(pin_rule_id_atom_of(rid));
    return xo_filter_walk_add_with_action(NULL, xfp, xpath, action);
}

xo_filter_t *
pin_filter_create (xo_handle_t *xop, pin_workspace_t *pwp)
{
    xo_filter_data_t *dp = calloc(1, sizeof(*dp));
    if (dp == NULL)
	return NULL;

    dp->xfd_workspace = pwp;

    xo_filter_t *xfp = xo_filter_create_with_data(xop, dp, &pin_filter_data_ops);
    if (xfp == NULL) {
	free(dp);
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
