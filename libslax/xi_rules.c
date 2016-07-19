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
 *
 * Parsing input means three distinct areas of work: parsing input, deciding
 * what to do with that input, and then doing it.  Our "xi_source" module
 * does the parsing, giving us back a "token" of input, which we pass to the
 * "rules" code to determine what needs done.  
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>

#include "slaxconfig.h"
#include <libslax/slaxdef.h>
#include <libslax/slax.h>
#include <libslax/pa_common.h>
#include <libslax/pa_config.h>
#include <libslax/pa_mmap.h>
#include <libslax/pa_fixed.h>
#include <libslax/pa_arb.h>
#include <libslax/pa_istr.h>
#include <libslax/pa_pat.h>
#include <libslax/pa_bitmap.h>
#include <libslax/xi_common.h>
#include <libslax/xi_rules.h>
#include <libslax/xi_tree.h>
#include <libslax/xi_workspace.h>
#include <libslax/xi_parse.h>

xi_rulebook_t *
xi_rulebook_setup (xi_workspace_t *xwp,
		   xi_parse_t *script, const char *name)
{
    char namebuf[PA_MMAP_HEADER_NAME_LEN];
    pa_mmap_t *pmp = xwp->xw_mmap;
    xi_rulebook_info_t *infop;
    pa_fixed_t *rules;
    pa_fixed_t *states;
    pa_bitmap_t *bitmaps;

    infop = pa_mmap_header(pmp, xi_mk_name(namebuf, name, "rulebook.info"),
			  PA_TYPE_OPAQUE, 0, sizeof(*infop));

    rules = pa_fixed_open(pmp, xi_mk_name(namebuf, name, "rules.set"),
			  XI_SHIFT, sizeof(xi_rule_t), XI_MAX_ATOMS);

    states = pa_fixed_open(pmp, xi_mk_name(namebuf, name, "rulebook.states"),
			   XI_SHIFT, sizeof(xi_rstate_t), XI_MAX_ATOMS);

    bitmaps = pa_bitmap_open(pmp, xi_mk_name(namebuf, name, "rulebook.bitmaps"));

    if (infop == NULL || rules == NULL || states == NULL || bitmaps == NULL)
	return NULL;
    
    xi_rulebook_t *xrbp = calloc(1, sizeof(*xrbp));

    if (xrbp) {
	xrbp->xrb_workspace = xwp;
	xrbp->xrb_infop = infop;
	xrbp->xrb_rules = rules;
	xrbp->xrb_states = states;
	xrbp->xrb_bitmaps = bitmaps;
	xrbp->xrb_script = script;
    }

    return xrbp;
}

static const char *xi_action_names[] = {
    "none",			/* XIA_NONE */
    "discard",			/* XIA_DISCARD */
    "save",			/* XIA_SAVE */
    "save-simple",		/* XIA_SAVE_ATSTR */
    "save-with-attributes",	/* XIA_SAVE_ATTRIB */
    "emit",			/* XIA_EMIT */
    "return",			/* XIA_RETURN */
    NULL
};

static xi_action_type_t
xi_rule_action_value (const char *name)
{
    xi_action_type_t type;
    for (type = 0; xi_action_names[type]; type++) {
	if (strcmp(name, xi_action_names[type]) == 0)
	    return type;
    }

    slaxLog("unknown action: '%s'", name);
    return XIA_NONE;
}

static const char *
xi_rule_action_name (xi_action_type_t action)
{
    if (action < XI_NUM_ELTS(xi_action_names))
	return xi_action_names[action];
    return "[unknown]";
}

static void
xi_rule_bitmap_add (xi_rulebook_t *xrbp, xi_rule_t *xrp, const char *tag)
{
    slaxLog("xi_rule_bitmap_add: %p/%p/%s", xrbp, xrp, tag);

    /* Find the atom representing the tag */
    pa_atom_t atom = xi_parse_namepool_atom(xrbp->xrb_script, tag);
    if (atom == PA_NULL_ATOM)
	return;

    /* We need to allocate a bitmap for this rule, if we haven't already */
    if (xrp->xr_bitmap == PA_NULL_ATOM) {
	xrp->xr_bitmap = pa_bitmap_alloc(xrbp->xrb_bitmaps);
	if (xrp->xr_bitmap == PA_NULL_ATOM)
	    return;
    }

    /* Finally, we can set the atom's bit in the map */
    pa_bitmap_set(xrbp->xrb_bitmaps, xrp->xr_bitmap, atom);
}

/*
 * Structure used to retain data while reversing the script input
 * hierarchy.  We save atom numbers here, as well as a stack of open
 * tags.  Fortunately our input is simple (trivial) so the stack depth
 * is small.
 */
#define XI_DEPTH_MAX_RULES 4
typedef struct xi_rulebook_prep_s {
    xi_rulebook_t *xrp_rulebook; /* Rules we are building */
    xi_parse_t *xrp_script;	 /* Parsed script "workspace" */
    pa_atom_t xrp_atom_action;	/* Cached atom numbers */
    pa_atom_t xrp_atom_id;
    pa_atom_t xrp_atom_new_state;
    pa_atom_t xrp_atom_rule;
    pa_atom_t xrp_atom_script;
    pa_atom_t xrp_atom_state;
    pa_atom_t xrp_atom_tag;
    pa_atom_t xrp_atom_use_tag;

    int xrp_depth;		/* Current depth of stack */
    struct xrp_stack_s {
	pa_atom_t xrps_state;	/* State atom (xi_rstate_t) */
	xi_rstate_t *xrps_statep; /* State array element */
	pa_atom_t xrps_rule;	/* Current rule atom (xi_rule_t) */
	pa_atom_t *xrps_nextp;	/* Location to store next atom */
    } xrp_stack[XI_DEPTH_MAX_RULES];
} xi_rulebook_prep_t;

static int
xi_rulebook_prep_cb (xi_parse_t *parsep, xi_node_type_t type,
		  xi_node_t *nodep,
		  const char *data, void *opaque)
{
    xi_tree_t *treep = parsep->xp_insert->xi_tree;
    xi_workspace_t *xwp = treep->xt_workspace;
    xi_rulebook_prep_t *prep = opaque;
    xi_rulebook_t *xrbp = prep->xrp_rulebook;
    struct xrp_stack_s *stackp = &prep->xrp_stack[prep->xrp_depth];
    const char *id, *action, *tag, *use_tag, *new_state;

#define GET_ATTRIB(_x) xi_get_attrib_string(xwp, nodep, prep->_x)
#define XX(_x) ((_x) ?: "")

#if 0
    int i;
    pa_fixed_t *pfp = xrbp->xrb_rules;
    pa_atom_t atom = pfp->pf_free;
    pa_fixed_page_entry_t *addr;
    for (i = 0; i < 5; i++) {
	addr = pa_fixed_atom_addr(pfp, atom);
	slaxLog("rules: check: %u %p", atom, addr);
	if (addr == NULL)
	    break;
	atom = addr[0];
    }
#endif /* 0 */

    switch (type) {
    case XI_TYPE_OPEN:
	if (nodep->xn_name == prep->xrp_atom_script) {
	    slaxLog("prep: open: script: %s", data);
	} else if (nodep->xn_name == prep->xrp_atom_state) {
	    slaxLog("prep: open: state: %s", data);
	    id = GET_ATTRIB(xrp_atom_id);
	    action = GET_ATTRIB(xrp_atom_action);
	    slaxLog("prep: open: state: [%s/%s]",
		    XX(id), XX(action));

	    /* Valid input requires a good state id number */
	    xi_state_id_t sid = strtol(id, NULL, 0);
	    if (sid > pa_fixed_max_atoms(xrbp->xrb_states)) {
		slaxLog("state id > max: %u .vs. %u",
			sid, pa_fixed_max_atoms(xrbp->xrb_states));
		break;
	    }

	    xi_rstate_t *statep;
	    statep = pa_fixed_element(xrbp->xrb_states, sid);
	    if (statep) {
		bzero(statep, sizeof(*statep));
		statep->xrbs_action = xi_rule_action_value(action);

		/* Set the stack "next" point to the first rule of the state */
		stackp->xrps_nextp = &statep->xrbs_first_rule;
	    }

	    /* Update xrsi_max_state */
	    if (sid > xrbp->xrb_infop->xrsi_max_state)
		xrbp->xrb_infop->xrsi_max_state = sid;

	} else if (nodep->xn_name == prep->xrp_atom_rule) {
	    slaxLog("prep: open: rule: %s", data);
	    tag = GET_ATTRIB(xrp_atom_tag);
	    action = GET_ATTRIB(xrp_atom_action);
	    new_state = GET_ATTRIB(xrp_atom_new_state);
	    use_tag = GET_ATTRIB(xrp_atom_use_tag);
	    slaxLog("prep: open: rule: [%s/%s/%s/%s]",
		    XX(tag), XX(action), XX(new_state), XX(use_tag));

	    xi_rule_id_t rid = pa_fixed_alloc_atom(xrbp->xrb_rules);
	    xi_rule_t *xrp = pa_fixed_atom_addr(xrbp->xrb_rules, rid);
	    if (xrp == NULL)
		break;

	    bzero(xrp, sizeof(*xrp));
	    xi_rule_bitmap_add(xrbp, xrp, tag);

	    if (action)
		xrp->xr_action = xi_rule_action_value(action);
	    if (use_tag)
		xrp->xr_use_tag = xi_parse_namepool_atom(xrbp->xrb_script, use_tag);
	    if (new_state)
		xrp->xr_new_state = strtol(new_state, NULL, 0);

	    /* Add rule to linked list of rules */
	    *stackp->xrps_nextp = stackp->xrps_rule = rid;
	    stackp->xrps_nextp = &xrp->xr_next;

	} else {
	    slaxLog("prep: open: unknown: %s", data);
	}
	break;
    }

    return 0;
}

xi_rulebook_t *
xi_rulebook_prep (xi_parse_t *input, const char *name)
{
    xi_workspace_t *xwp = input->xp_insert->xi_tree->xt_workspace;
    xi_rulebook_t *xrbp = xi_rulebook_setup(xwp, input, name);
    xi_rulebook_prep_t prep;

    if (xrbp == NULL)
	return NULL;

    bzero(&prep, sizeof(prep));

    prep.xrp_rulebook = xrbp;
    prep.xrp_script = input;

    /* We need all the atom number for the bits we care about */
    /* XXX rewrite as array/loop */
    prep.xrp_atom_action = xi_parse_namepool_atom(input, "action");
    prep.xrp_atom_id = xi_parse_namepool_atom(input, "id");
    prep.xrp_atom_new_state = xi_parse_namepool_atom(input, "new-state");
    prep.xrp_atom_rule = xi_parse_namepool_atom(input, "rule");
    prep.xrp_atom_script = xi_parse_namepool_atom(input, "script");
    prep.xrp_atom_state = xi_parse_namepool_atom(input, "state");
    prep.xrp_atom_tag = xi_parse_namepool_atom(input, "tag");
    prep.xrp_atom_use_tag = xi_parse_namepool_atom(input, "use-tag");

    xi_parse_emit(input, xi_rulebook_prep_cb, &prep);

    return xrbp;
}

xi_rule_t *
xi_rulebook_find (xi_parse_t *parsep UNUSED, xi_rulebook_t *xrbp,
		  xi_rstate_t *statep,
		  pa_atom_t name_atom UNUSED,
		  const char *pref UNUSED, const char *name UNUSED,
		  const char *attribs UNUSED)
{
    if (xrbp == NULL)		/* No rulebook means no rules */
	return NULL;

    if (statep == NULL)
	return NULL;

    xi_rule_id_t rid;
    xi_rule_t *xrp;
    for (rid = statep->xrbs_first_rule; rid != PA_NULL_ATOM;
	 rid = xrp->xr_next) {
	xrp = xi_rulebook_rule(xrbp, rid);
	if (xrp == NULL)
	    continue;

	/* See if our tag is in the bitmap for this rule */
	if (!pa_bitmap_test(xrbp->xrb_bitmaps, xrp->xr_bitmap, name_atom))
	    continue;

	slaxLog("rule match: %u/'%s' rule %u: action %u/%s, flags %#x, "
		"use-tag %u, new_state %u",
		name_atom, name ?: "",
		rid, xrp->xr_action, xi_rule_action_name(xrp->xr_action),
		xrp->xr_flags, xrp->xr_use_tag, xrp->xr_new_state);

	return xrp;		/* Success! */
    }

    return NULL;
}

static const char *
xi_rule_bitmap_string (xi_rulebook_t *xrbp, xi_rule_t *xrp,
			char *buf, size_t bufsiz)
{
    pa_bitmap_t *pbp = xrbp->xrb_bitmaps;
    pa_bitmap_id_t bitmap = xrp->xr_bitmap;
    pa_bitnumber_t num = PA_BITMAP_FIND_START;
    char *cp = buf;
    char *ep = buf + bufsiz;
    int rc;
    const char *str;

    for (;;) {
	/* Whiffle thru the set of bits */
	num = pa_bitmap_find_next(pbp, bitmap, num);
	if (num == PA_BITMAP_FIND_DONE)
	    break;

	/* Turn the bit into a string */
	str = xi_parse_namepool_string(xrbp->xrb_script, num);

	/* Make some pretty pretty output */
	rc = snprintf(cp, ep - cp, "%s%d%s%s%s",
		      (cp == buf) ? "" : ", ", num,
		      str ? " (" : "", str ?: "", str ? ")" : "");
	if (rc >= ep - cp) {
	    /* Out of room; so sorry */
	    memcpy(ep - 5, "...", 4);
	    break;
	}
	cp += rc;
    }

    *cp = '\0';
    return buf;
}

void
xi_rulebook_dump (xi_rulebook_t *xrbp)
{
    char buf[1024];
    xi_state_id_t sid, max_sid = xrbp->xrb_infop->xrsi_max_state;
    xi_rule_id_t rid;
    xi_rstate_t *statep;
    xi_rule_t *rulep;

    slaxLog("dumping rulebook");

    for (sid = 1; sid <= max_sid; sid++) {
	statep = xi_rulebook_state(xrbp, sid);
	if (statep == NULL)
	    continue;

	slaxLog("state %u: flags %#x, action %d",
		sid, statep->xrbs_flags, statep->xrbs_action);

	for (rid = statep->xrbs_first_rule; rid != PA_NULL_ATOM;
	     rid = rulep->xr_next) {
	    rulep = xi_rulebook_rule(xrbp, rid);
	    if (rulep == NULL)
		continue;

	    const char *rname = xi_rule_action_name(rulep->xr_action);

	    slaxLog("    rule %u:", rid);
	    slaxLog("        bitmap: %s",
		    xi_rule_bitmap_string(xrbp, rulep, buf, sizeof(buf)));
	    slaxLog("        flags %#x, action %u/%s, use-tag %u, "
		    "new_state %u, next %u",
		    rulep->xr_flags, rulep->xr_action, rname,
		    rulep->xr_use_tag, rulep->xr_new_state, rulep->xr_next);
	}
    }
}
