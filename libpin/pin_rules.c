/*
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
 * what to do with that input, and then doing it.  Our "pin_source" module
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

pin_rulebook_t *
pin_rulebook_setup (pin_workspace_t *xwp,
		   pin_parse_t *script, const char *name)
{
    char namebuf[PA_MMAP_HEADER_NAME_LEN];
    pa_mmap_t *pmp = xwp->xw_mmap;
    pin_rulebook_info_t *infop;
    pa_fixed_t *rules;
    pa_fixed_t *states;
    pa_bitmap_t *bitmaps;

    infop = pa_mmap_header(pmp, pin_mk_name(namebuf, name, "rulebook.info"),
			  PA_TYPE_OPAQUE, 0, sizeof(*infop));

    rules = pa_fixed_open(pmp, pin_mk_name(namebuf, name, "rules.set"),
			  XI_SHIFT, sizeof(pin_rule_t), XI_MAX_ATOMS);

    states = pa_fixed_open(pmp, pin_mk_name(namebuf, name, "rulebook.states"),
			   XI_SHIFT, sizeof(pin_rstate_t), XI_MAX_ATOMS);

    bitmaps = pa_bitmap_open(pmp, pin_mk_name(namebuf, name, "rulebook.bitmaps"));

    if (infop == NULL || rules == NULL || states == NULL || bitmaps == NULL)
	return NULL;
    
    pin_rulebook_t *xrbp = calloc(1, sizeof(*xrbp));

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

static const char *pin_action_names[] = {
    "none",			/* XIA_NONE */
    "discard",			/* XIA_DISCARD */
    "save",			/* XIA_SAVE */
    "save-simple",		/* XIA_SAVE_ATSTR */
    "save-with-attributes",	/* XIA_SAVE_ATTRIB */
    "emit",			/* XIA_EMIT */
    "return",			/* XIA_RETURN */
    NULL
};

static pin_action_type_t
pin_rule_action_value (const char *name)
{
    pin_action_type_t type;
    for (type = 0; pin_action_names[type]; type++) {
	if (strcmp(name, pin_action_names[type]) == 0)
	    return type;
    }

    psu_log("unknown action: '%s'", name);
    return XIA_NONE;
}

static const char *
pin_rule_action_name (pin_action_type_t action)
{
    if (action < PSU_NUM_ELTS(pin_action_names))
	return pin_action_names[action];
    return "[unknown]";
}

static void
pin_rule_bitmap_add (pin_rulebook_t *xrbp, pin_rule_t *xrp, const char *tag)
{
    psu_log("pin_rule_bitmap_add: %p/%p/%s", xrbp, xrp, tag);

    /* Find the atom representing the tag */
    pa_atom_t atom = pin_parse_namepool_atom(xrbp->xrb_script, tag);
    if (atom == PA_NULL_ATOM)
	return;

    /* We need to allocate a bitmap for this rule, if we haven't already */
    if (pa_bitmap_is_null(xrp->xr_bitmap)) {
	xrp->xr_bitmap = pa_bitmap_alloc(xrbp->xrb_bitmaps);
	if (pa_bitmap_is_null(xrp->xr_bitmap))
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
typedef struct pin_rulebook_prep_s {
    pin_rulebook_t *xrp_rulebook; /* Rules we are building */
    pin_parse_t *xrp_script;	 /* Parsed script "workspace" */
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
	pa_atom_t xrps_state;	/* State atom (pin_rstate_t) */
	pin_rstate_t *xrps_statep; /* State array element */
	pin_rule_id_t xrps_rule;	/* Current rule atom (pin_rule_t) */
	pin_rule_id_t *xrps_nextp;	/* Location to store next atom */
    } xrp_stack[XI_DEPTH_MAX_RULES];
} pin_rulebook_prep_t;

static int
pin_rulebook_prep_cb (pin_parse_t *parsep, pin_node_type_t type,
		     pin_node_id_t node_atom UNUSED, pin_node_t *nodep,
		     const char *data, void *opaque)
{
    pin_tree_t *treep = parsep->xp_insert->pin_tree;
    pin_workspace_t *xwp = treep->xt_workspace;
    pin_rulebook_prep_t *prep = opaque;
    pin_rulebook_t *xrbp = prep->xrp_rulebook;
    struct xrp_stack_s *stackp = &prep->xrp_stack[prep->xrp_depth];
    const char *id, *action, *tag, *use_tag, *new_state;

#define GET_ATTRIB(_x) pin_get_attrib_string(xwp, nodep, prep->_x)
#define XX(_x) ((_x) ?: "")

#if 0
    int i;
    pa_fixed_t *pfp = xrbp->xrb_rules;
    pa_atom_t atom = pfp->pf_free;
    pa_fixed_page_entry_t *addr;
    for (i = 0; i < 5; i++) {
	addr = pa_fixed_atom_addr(pfp, atom);
	psu_log("rules: check: %u %p", atom, addr);
	if (addr == NULL)
	    break;
	atom = addr[0];
    }
#endif /* 0 */

    switch (type) {
    case XI_TYPE_OPEN:
	if (nodep->xn_name == prep->xrp_atom_script) {
	    psu_log("prep: open: script: %s", data);
	} else if (nodep->xn_name == prep->xrp_atom_state) {
	    psu_log("prep: open: state: %s", data);
	    id = GET_ATTRIB(xrp_atom_id);
	    action = GET_ATTRIB(xrp_atom_action);
	    psu_log("prep: open: state: [%s/%s]",
		    XX(id), XX(action));

	    /* Valid input requires a good state id number */
	    pin_rstate_id_t sid = pin_rstate_id(strtol(id, NULL, 0));
	    pa_atom_t sid_n = pa_fixed_atom_of(pin_rstate_id_atom_of(sid));
	    if (sid_n > pa_fixed_max_atoms(xrbp->xrb_states)) {
		psu_log("state id > max: %u .vs. %u",
			sid_n, pa_fixed_max_atoms(xrbp->xrb_states));
		break;
	    }

	    pin_rstate_t *statep = pin_rstate_element(xrbp, sid);
	    if (statep) {
		bzero(statep, sizeof(*statep));

		/* Set the stack "next" point to the first rule of the state */
		stackp->xrps_nextp = &statep->xrbs_first_rule;

		/* If an action was defined, build a default rule */
		if (action) {
		    pin_rule_id_t rid;
		    pin_rule_t *xrp = pin_rule_alloc(xrbp, &rid);
		    if (xrp == NULL)
			break;

		    bzero(xrp, sizeof(*xrp));
		    xrp->xr_flags = XRF_MATCH_ALL;
		    xrp->xr_action = pin_rule_action_value(action);

		    /* Record the rule as the default for this state */
		    statep->xrbs_default_rule = rid;
		}
	    }

	    /* Update xrsi_max_state */
	    if (sid_n > pa_fixed_atom_of(pin_rstate_id_atom_of(
					 xrbp->xrb_infop->xrsi_max_state)))
		xrbp->xrb_infop->xrsi_max_state = sid;

	} else if (nodep->xn_name == prep->xrp_atom_rule) {
	    psu_log("prep: open: rule: %s", data);
	    tag = GET_ATTRIB(xrp_atom_tag);
	    action = GET_ATTRIB(xrp_atom_action);
	    new_state = GET_ATTRIB(xrp_atom_new_state);
	    use_tag = GET_ATTRIB(xrp_atom_use_tag);
	    psu_log("prep: open: rule: [%s/%s/%s/%s]",
		    XX(tag), XX(action), XX(new_state), XX(use_tag));

	    pin_rule_id_t rid;
	    pin_rule_t *xrp = pin_rule_alloc(xrbp, &rid);
	    if (xrp == NULL)
		break;

	    bzero(xrp, sizeof(*xrp));
	    if (tag)
		pin_rule_bitmap_add(xrbp, xrp, tag);

	    if (action)
		xrp->xr_action = pin_rule_action_value(action);
	    if (use_tag)
		xrp->xr_use_tag = pin_parse_namepool_atom(xrbp->xrb_script, use_tag);
	    if (new_state)
		xrp->xr_new_state = pin_rstate_id(strtol(new_state, NULL, 0));

	    /* Add rule to linked list of rules */
	    *stackp->xrps_nextp = stackp->xrps_rule = rid;
	    stackp->xrps_nextp = &xrp->xr_next;

	} else {
	    psu_log("prep: open: unknown: %s", data);
	}
	break;
    }

    return 0;
}

pin_rulebook_t *
pin_rulebook_prep (pin_parse_t *input, const char *name)
{
    pin_workspace_t *xwp = input->xp_insert->pin_tree->xt_workspace;
    pin_rulebook_t *xrbp = pin_rulebook_setup(xwp, input, name);
    pin_rulebook_prep_t prep;

    if (xrbp == NULL)
	return NULL;

    bzero(&prep, sizeof(prep));

    prep.xrp_rulebook = xrbp;
    prep.xrp_script = input;

    /* We need all the atom number for the bits we care about */
    /* XXX rewrite as array/loop */
    prep.xrp_atom_action = pin_parse_namepool_atom(input, "action");
    prep.xrp_atom_id = pin_parse_namepool_atom(input, "id");
    prep.xrp_atom_new_state = pin_parse_namepool_atom(input, "new-state");
    prep.xrp_atom_rule = pin_parse_namepool_atom(input, "rule");
    prep.xrp_atom_script = pin_parse_namepool_atom(input, "script");
    prep.xrp_atom_state = pin_parse_namepool_atom(input, "state");
    prep.xrp_atom_tag = pin_parse_namepool_atom(input, "tag");
    prep.xrp_atom_use_tag = pin_parse_namepool_atom(input, "use-tag");

    pin_parse_emit(input, pin_rulebook_prep_cb, &prep);

    return xrbp;
}

/*
 * Find the appropriate rule to process incoming data
 */
pin_rule_t *
pin_rulebook_find (pin_parse_t *parsep UNUSED, pin_rulebook_t *xrbp,
		  pin_rstate_t *statep,
		  pa_atom_t name_atom,
		  const char *pref UNUSED, const char *name,
		  const char *attribs UNUSED)
{
    if (xrbp == NULL)		/* No rulebook means no rules */
	return NULL;

    if (statep == NULL)
	return NULL;

    pin_rule_id_t rid;
    pin_rule_t *xrp;
    for (rid = statep->xrbs_first_rule; !pin_rule_id_is_null(rid);
	 rid = xrp->xr_next) {
	xrp = pin_rulebook_rule(xrbp, rid);
	if (xrp == NULL)
	    continue;

	/* See if our tag is in the bitmap for this rule */
	if (!pa_bitmap_test(xrbp->xrb_bitmaps, xrp->xr_bitmap, name_atom))
	    continue;

	psu_log("rule match: %u/'%s' rule %u: action %u/%s, flags %#x, "
		"use-tag %u, new_state %u",
		name_atom, name ?: "",
		pa_fixed_atom_of(pin_rule_id_atom_of(rid)),
		xrp->xr_action, pin_rule_action_name(xrp->xr_action),
		xrp->xr_flags, xrp->xr_use_tag,
		pa_fixed_atom_of(pin_rstate_id_atom_of(xrp->xr_new_state)));

	return xrp;		/* Success! */
    }

    /* No explicit rule matched; fall back to the state's default rule */
    if (!pin_rule_id_is_null(statep->xrbs_default_rule))
	return pin_rulebook_rule(xrbp, statep->xrbs_default_rule);

    return NULL;
}

/*
 * Turn a bitmap in a rule into a string, expanding names
 */
static const char *
pin_rule_bitmap_string (pin_rulebook_t *xrbp, pin_rule_t *xrp,
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
	str = pin_parse_namepool_string(xrbp->xrb_script, num);

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

static pin_rule_id_t
pin_rulebook_dump_rule (pin_rulebook_t *xrbp, pin_rule_id_t rid, const char *tag)
{
    pin_rule_t *rulep = pin_rulebook_rule(xrbp, rid);
    if (rulep == NULL)
	return pin_rule_id_null_atom();

    const char *rname = pin_rule_action_name(rulep->xr_action);
    char buf[1024];

    psu_log("    %srule %u:", tag,
	    pa_fixed_atom_of(pin_rule_id_atom_of(rid)));
    psu_log("        bitmap: %s",
	    pin_rule_bitmap_string(xrbp, rulep, buf, sizeof(buf)));
    psu_log("        flags %#x, action %u/%s, use-tag %u, "
	    "new_state %u, next %u",
	    rulep->xr_flags, rulep->xr_action, rname,
	    rulep->xr_use_tag,
	    pa_fixed_atom_of(pin_rstate_id_atom_of(rulep->xr_new_state)),
	    pa_fixed_atom_of(pin_rule_id_atom_of(rulep->xr_next)));

    return rulep->xr_next;
}

/*
 * Cause sometimes you just need to see what's really going on....
 */
void
pin_rulebook_dump (pin_rulebook_t *xrbp)
{
    pa_atom_t sid;
    pa_atom_t max_sid = pa_fixed_atom_of(
		pin_rstate_id_atom_of(xrbp->xrb_infop->xrsi_max_state));
    pin_rule_id_t rid;
    pin_rstate_t *statep;

    psu_log("dumping rulebook");

    for (sid = 1; sid <= max_sid; sid++) {
	statep = (pin_rstate_t *) pa_fixed_element(xrbp->xrb_states, sid);
	if (statep == NULL)
	    continue;

	psu_log("state %u: flags %#x, default rule %u",
		sid, statep->xrbs_flags,
		pa_fixed_atom_of(pin_rule_id_atom_of(statep->xrbs_default_rule)));

	/* Dump the full set of rules */
	for (rid = statep->xrbs_first_rule; !pin_rule_id_is_null(rid); )
	    rid = pin_rulebook_dump_rule(xrbp, rid, "");

	/* Dump the default rule */
	if (!pin_rule_id_is_null(statep->xrbs_default_rule))
	    pin_rulebook_dump_rule(xrbp, statep->xrbs_default_rule, "default ");
    }
}
