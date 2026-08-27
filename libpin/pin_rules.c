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
#include <libxo/xo.h>
#include "xo_filter.h"

static const psu_byte_t *pin_apply_key_func (pa_pat_t *, pa_pat_data_atom_t);

pin_rulebook_t *
pin_rulebook_setup (pin_workspace_t *pwp,
		   pin_parse_t *script, const char *name)
{
    char namebuf[PA_MMAP_HEADER_NAME_LEN];
    pa_mmap_t *pmp = pwp->pw_mmap;
    pin_rulebook_info_t *infop;
    pa_fixed_t *rules;
    pa_fixed_t *states;
    pa_bitmap_t *bitmaps;

    infop = pa_mmap_header(pmp, pin_mk_name(namebuf, name, "rulebook.info"),
			  PA_TYPE_OPAQUE, 0, sizeof(*infop));

    rules = pa_fixed_open(pmp, pin_mk_name(namebuf, name, "rules.set"),
			  PIN_SHIFT, sizeof(pin_rule_t), PIN_MAX_ATOMS);

    states = pa_fixed_open(pmp, pin_mk_name(namebuf, name, "rulebook.states"),
			   PIN_SHIFT, sizeof(pin_rstate_t), PIN_MAX_ATOMS);

    bitmaps = pa_bitmap_open(pmp, pin_mk_name(namebuf, name, "rulebook.bitmaps"));

    pa_fixed_t *body_instrs =
	pa_fixed_open(pmp, pin_mk_name(namebuf, name, "rulebook.body"),
		      PIN_SHIFT, sizeof(pin_body_instr_t), PIN_MAX_ATOMS);

    pa_fixed_t *apply_entries =
	pa_fixed_open(pmp, pin_mk_name(namebuf, name, "apply.entries"),
		      PIN_SHIFT, sizeof(pin_apply_entry_t), PIN_MAX_ATOMS);

    if (infop == NULL || rules == NULL || states == NULL
	    || bitmaps == NULL || body_instrs == NULL || apply_entries == NULL)
	return NULL;

    pa_pat_t *apply_pat = pa_pat_open(pmp,
				      pin_mk_name(namebuf, name, "apply.index"),
				      apply_entries, pin_apply_key_func,
				      sizeof(pin_name_id_t),
				      PIN_SHIFT, PIN_MAX_ATOMS);
    if (apply_pat == NULL)
	return NULL;

    pin_rulebook_t *prbp = calloc(1, sizeof(*prbp));

    if (prbp) {
	prbp->prb_workspace = pwp;
	prbp->prb_infop = infop;
	prbp->prb_rules = rules;
	prbp->prb_states = states;
	prbp->prb_bitmaps = bitmaps;
	prbp->prb_body_instrs = body_instrs;
	prbp->prb_apply_entries = apply_entries;
	prbp->prb_apply_pat = apply_pat;
	prbp->prb_script = script;

	/*
	 * Ensure PIN_STATE_INITIAL (atom 1) exists and is zeroed on a fresh
	 * pool.  pin_parse_set_rulebook hard-codes atom 1 as the initial
	 * parser state; pre-allocating here prevents later callers (e.g.
	 * pin_rulebook_add_foreach_state) from claiming atom 1 for their
	 * own state.  On reopen, prsi_initial_state is non-null so we skip.
	 */
	if (pin_rstate_id_is_null(infop->prsi_initial_state)) {
	    pin_rstate_id_t init_sid;
	    pin_rstate_t *init_statep = pin_rstate_alloc(prbp, &init_sid);
	    if (init_statep) {
		bzero(init_statep, sizeof(*init_statep));
		infop->prsi_initial_state = init_sid;
	    }
	}
    }

    return prbp;
}

static const char *pin_action_names[] = {
    "none",			/* PIA_NONE */
    "discard",			/* PIA_DISCARD */
    "save",			/* PIA_SAVE */
    "save-simple",		/* PIA_SAVE_ATSTR */
    "save-with-attributes",	/* PIA_SAVE_ATTRIB */
    "emit",			/* PIA_EMIT */
    "return",			/* PIA_RETURN */
    "literal",			/* PIA_LITERAL */
    "wrap",			/* PIA_WRAP */
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
    return PIA_NONE;
}

static const char *
pin_rule_action_name (pin_action_type_t action)
{
    if (action < PSU_NUM_ELTS(pin_action_names))
	return pin_action_names[action];
    return "[unknown]";
}

static void
pin_rule_bitmap_add (pin_rulebook_t *prbp, pin_rule_t *prp, const char *tag)
{
    psu_log("pin_rule_bitmap_add: %p/%p/%s", prbp, prp, tag);

    /* Find the atom representing the tag */
    pin_name_id_t atom = pin_parse_namepool_atom(prbp->prb_script, tag);
    if (pin_name_id_is_null(atom))
	return;

    /* We need to allocate a bitmap for this rule, if we haven't already */
    if (pa_bitmap_is_null(prp->pr_bitmap)) {
	prp->pr_bitmap = pa_bitmap_alloc(prbp->prb_bitmaps);
	if (pa_bitmap_is_null(prp->pr_bitmap))
	    return;
    }

    /* Finally, we can set the atom's bit in the map */
    pa_bitmap_set(prbp->prb_bitmaps, prp->pr_bitmap, pin_name_id_atom_of(atom));
}

pin_rstate_id_t
pin_rulebook_add_foreach_state (pin_rulebook_t *prbp, const char *select_name,
				pin_action_type_t select_action,
				pin_action_type_t default_action)
{
    pin_rstate_id_t sid;
    pin_rstate_t *statep = pin_rstate_alloc(prbp, &sid);
    if (statep == NULL)
	return pin_rstate_id_null_atom();

    bzero(statep, sizeof(*statep));

    /* Catch-all default rule for unselected elements */
    if (default_action != PIA_NONE) {
	pin_rule_id_t def_rid;
	pin_rule_t *def_prp = pin_rule_alloc(prbp, &def_rid);
	if (def_prp) {
	    bzero(def_prp, sizeof(*def_prp));
	    def_prp->pr_flags = PRF_MATCH_ALL;
	    def_prp->pr_action = default_action;
	    statep->prbs_default_rule = def_rid;
	}
    }

    /* Explicit rule for the selected element */
    pin_name_id_t name_id = pin_namepool_atom(prbp->prb_workspace, select_name, TRUE);
    if (!pin_name_id_is_null(name_id)) {
	pin_rule_id_t rid;
	pin_rule_t *prp = pin_rule_alloc(prbp, &rid);
	if (prp) {
	    bzero(prp, sizeof(*prp));
	    prp->pr_action = select_action;
	    prp->pr_bitmap = pa_bitmap_alloc(prbp->prb_bitmaps);
	    if (!pa_bitmap_is_null(prp->pr_bitmap))
		pa_bitmap_set(prbp->prb_bitmaps, prp->pr_bitmap,
			      pin_name_id_atom_of(name_id));
	    statep->prbs_first_rule = rid;
	}
    }

    return sid;
}

pin_rstate_id_t
pin_rulebook_add_foreach_literal_state (pin_rulebook_t *prbp,
					const char *select_name,
					const char *literal_tag,
					const char *literal_text,
					pin_action_type_t default_action)
{
    pin_rstate_id_t sid;
    pin_rstate_t *statep = pin_rstate_alloc(prbp, &sid);
    if (statep == NULL)
	return pin_rstate_id_null_atom();

    bzero(statep, sizeof(*statep));

    /* Catch-all default rule for unselected elements */
    if (default_action != PIA_NONE) {
	pin_rule_id_t def_rid;
	pin_rule_t *def_prp = pin_rule_alloc(prbp, &def_rid);
	if (def_prp) {
	    bzero(def_prp, sizeof(*def_prp));
	    def_prp->pr_flags = PRF_MATCH_ALL;
	    def_prp->pr_action = default_action;
	    statep->prbs_default_rule = def_rid;
	}
    }

    /* Explicit PIA_LITERAL rule for the selected element */
    pin_name_id_t name_id = pin_namepool_atom(prbp->prb_workspace, select_name, TRUE);
    if (!pin_name_id_is_null(name_id)) {
	pin_rule_id_t rid;
	pin_rule_t *prp = pin_rule_alloc(prbp, &rid);
	if (prp) {
	    bzero(prp, sizeof(*prp));
	    prp->pr_action = PIA_LITERAL;
	    prp->pr_use_tag = literal_tag
		? pin_namepool_atom(prbp->prb_workspace, literal_tag, TRUE)
		: pin_name_id_null_atom();
	    prp->pr_literal_text = literal_text
		? pin_namepool_atom(prbp->prb_workspace, literal_text, TRUE)
		: pin_name_id_null_atom();
	    prp->pr_bitmap = pa_bitmap_alloc(prbp->prb_bitmaps);
	    if (!pa_bitmap_is_null(prp->pr_bitmap))
		pa_bitmap_set(prbp->prb_bitmaps, prp->pr_bitmap,
			      pin_name_id_atom_of(name_id));
	    statep->prbs_first_rule = rid;
	}
    }

    return sid;
}

pin_rstate_id_t
pin_rulebook_add_foreach_wrap_state (pin_rulebook_t *prbp,
				     const char *select_name,
				     const char *wrap_tag,
				     const char *pre_tag,
				     const char *pre_text,
				     pin_action_type_t default_action)
{
    pin_rstate_id_t sid;
    pin_rstate_t *statep = pin_rstate_alloc(prbp, &sid);
    if (statep == NULL)
	return pin_rstate_id_null_atom();

    bzero(statep, sizeof(*statep));

    if (default_action != PIA_NONE) {
	pin_rule_id_t def_rid;
	pin_rule_t *def_prp = pin_rule_alloc(prbp, &def_rid);
	if (def_prp) {
	    bzero(def_prp, sizeof(*def_prp));
	    def_prp->pr_flags = PRF_MATCH_ALL;
	    def_prp->pr_action = default_action;
	    statep->prbs_default_rule = def_rid;
	}
    }

    pin_name_id_t name_id = pin_namepool_atom(prbp->prb_workspace, select_name, TRUE);
    if (!pin_name_id_is_null(name_id)) {
	pin_rule_id_t rid;
	pin_rule_t *prp = pin_rule_alloc(prbp, &rid);
	if (prp) {
	    bzero(prp, sizeof(*prp));
	    prp->pr_action = PIA_WRAP;
	    prp->pr_use_tag = wrap_tag
		? pin_namepool_atom(prbp->prb_workspace, wrap_tag, TRUE)
		: pin_name_id_null_atom();
	    prp->pr_pre_tag = pre_tag
		? pin_namepool_atom(prbp->prb_workspace, pre_tag, TRUE)
		: pin_name_id_null_atom();
	    prp->pr_literal_text = pre_text
		? pin_namepool_atom(prbp->prb_workspace, pre_text, TRUE)
		: pin_name_id_null_atom();
	    prp->pr_bitmap = pa_bitmap_alloc(prbp->prb_bitmaps);
	    if (!pa_bitmap_is_null(prp->pr_bitmap))
		pa_bitmap_set(prbp->prb_bitmaps, prp->pr_bitmap,
			      pin_name_id_atom_of(name_id));
	    statep->prbs_first_rule = rid;
	}
    }

    return sid;
}

pin_rstate_id_t
pin_rulebook_add_foreach_body_state (pin_rulebook_t *prbp,
				     const char *select_name,
				     pin_body_instr_id_t body_head,
				     pin_body_retain_t retain,
				     pin_action_type_t default_action)
{
    pin_rstate_id_t sid;
    pin_rstate_t *statep = pin_rstate_alloc(prbp, &sid);
    if (statep == NULL)
	return pin_rstate_id_null_atom();

    bzero(statep, sizeof(*statep));

    if (default_action != PIA_NONE) {
	pin_rule_id_t def_rid;
	pin_rule_t *def_prp = pin_rule_alloc(prbp, &def_rid);
	if (def_prp) {
	    bzero(def_prp, sizeof(*def_prp));
	    def_prp->pr_flags = PRF_MATCH_ALL;
	    def_prp->pr_action = default_action;
	    statep->prbs_default_rule = def_rid;
	}
    }

    pin_name_id_t name_id = pin_namepool_atom(prbp->prb_workspace, select_name, TRUE);
    if (!pin_name_id_is_null(name_id)) {
	pin_rule_id_t rid;
	pin_rule_t *prp = pin_rule_alloc(prbp, &rid);
	if (prp) {
	    bzero(prp, sizeof(*prp));
	    prp->pr_body = body_head;
	    prp->pr_body_retain = retain;
	    prp->pr_bitmap = pa_bitmap_alloc(prbp->prb_bitmaps);
	    if (!pa_bitmap_is_null(prp->pr_bitmap))
		pa_bitmap_set(prbp->prb_bitmaps, prp->pr_bitmap,
			      pin_name_id_atom_of(name_id));
	    statep->prbs_first_rule = rid;
	}
    }

    return sid;
}

/*
 * Key function for the apply-templates patricia tree.
 * The key is the pae_name field (a pin_name_id_t, 4 bytes) of the entry.
 */
static const psu_byte_t *
pin_apply_key_func (pa_pat_t *pp, pa_pat_data_atom_t datom)
{
    pin_apply_entry_t *ep = pa_fixed_atom_addr(
	(pa_fixed_t *) pp->pp_data,
	pa_fixed_atom(pa_pat_data_atom_of(datom)));
    return ep ? (const psu_byte_t *) &ep->pae_name : NULL;
}

int
pin_rulebook_apply_add (pin_rulebook_t *prbp,
			pin_name_id_t name_id, pin_name_id_t mode_id,
			pin_rule_id_t rid)
{
    if (prbp->prb_apply_pat == NULL || prbp->prb_apply_entries == NULL)
	return -1;
    if (pin_name_id_is_null(name_id) || pin_rule_id_is_null(rid))
	return -1;

    pin_apply_id_t aid;
    pin_apply_entry_t *ep = pin_apply_alloc(prbp, &aid);
    if (ep == NULL)
	return -1;

    ep->pae_name = name_id;
    ep->pae_mode = mode_id;
    ep->pae_rule = rid;

    /* Prepend to linked list (last wins: new head is searched first) */
    ep->pae_next = prbp->prb_apply_list;
    prbp->prb_apply_list = aid;

    /*
     * For default-mode entries, also register in the Patricia tree so
     * the O(log n) fast path in PBMODE_APPLY (no mode= on the
     * apply-templates instruction) keeps working.
     */
    if (pin_name_id_is_null(mode_id)) {
	pa_pat_data_atom_t datom = pa_pat_data_atom(
	    pa_fixed_atom_of(pin_apply_id_atom_of(aid)));

	if (!pa_pat_add(prbp->prb_apply_pat, datom, sizeof(pin_name_id_t))) {
	    /* Duplicate: later template takes precedence (XSLT last-wins). */
	    pa_pat_node_t *existing = pa_pat_get(prbp->prb_apply_pat,
						 sizeof(pin_name_id_t), &name_id);
	    if (existing)
		existing->ppn_data = datom;
	}
    }

    return 0;
}

pin_rulebook_t *
pin_rulebook_open (const char *name UNUSED)
{
    return NULL;
}

void
pin_rulebook_close (pin_rulebook_t *rules)
{
    if (rules == NULL)
	return;
    if (rules->prb_if_filters) {
	for (uint32_t i = 0; i < rules->prb_if_filter_count; i++)
	    xo_filter_destroy_standalone(rules->prb_if_filters[i]);
	free(rules->prb_if_filters);
	rules->prb_if_filters = NULL;
	rules->prb_if_filter_count = 0;
	rules->prb_if_filter_cap = 0;
    }
}

uint32_t
pin_rulebook_if_filter_add (pin_rulebook_t *prbp, xo_filter_t *xfp)
{
    if (prbp->prb_if_filter_count >= prbp->prb_if_filter_cap) {
	uint32_t newcap = prbp->prb_if_filter_cap
			  ? prbp->prb_if_filter_cap * 2 : 8;
	xo_filter_t **newp = realloc(prbp->prb_if_filters,
				     newcap * sizeof(*newp));
	if (newp == NULL)
	    return UINT32_MAX;
	prbp->prb_if_filters = newp;
	prbp->prb_if_filter_cap = newcap;
    }
    uint32_t idx = prbp->prb_if_filter_count++;
    prbp->prb_if_filters[idx] = xfp;
    return idx;
}

/*
 * Structure used to retain data while reversing the script input
 * hierarchy.  We save atom numbers here, as well as a stack of open
 * tags.  Fortunately our input is simple (trivial) so the stack depth
 * is small.
 */
#define PIN_DEPTH_MAX_RULES 4
typedef struct pin_rulebook_prep_s {
    pin_rulebook_t *prp_rulebook; /* Rules we are building */
    pin_parse_t *prp_script;	 /* Parsed script "workspace" */
    pin_name_id_t prp_atom_action;	/* Cached atom numbers */
    pin_name_id_t prp_atom_id;
    pin_name_id_t prp_atom_new_state;
    pin_name_id_t prp_atom_rule;
    pin_name_id_t prp_atom_script;
    pin_name_id_t prp_atom_state;
    pin_name_id_t prp_atom_tag;
    pin_name_id_t prp_atom_use_tag;

    int prp_depth;		/* Current depth of stack */
    struct prp_stack_s {
	pa_atom_t prps_state;	/* State atom (pin_rstate_t) */
	pin_rstate_t *prps_statep; /* State array element */
	pin_rule_id_t prps_rule;	/* Current rule atom (pin_rule_t) */
	pin_rule_id_t *prps_nextp;	/* Location to store next atom */
    } prp_stack[PIN_DEPTH_MAX_RULES];
} pin_rulebook_prep_t;

static int
pin_rulebook_prep_cb (pin_parse_t *parsep, pin_node_type_t type,
		     pin_node_id_t node_atom UNUSED, pin_node_t *nodep,
		     const char *data, void *opaque)
{
    pin_tree_t *treep = parsep->pp_insert->pin_tree;
    pin_workspace_t *pwp = treep->pt_workspace;
    pin_rulebook_prep_t *prep = opaque;
    pin_rulebook_t *prbp = prep->prp_rulebook;
    struct prp_stack_s *stackp = &prep->prp_stack[prep->prp_depth];
    const char *id, *action, *tag, *use_tag, *new_state;

#define GET_ATTRIB(_x) pin_get_attrib_string(pwp, nodep, prep->_x)
#define XX(_x) ((_x) ?: "")

#if 0
    int i;
    pa_fixed_t *pfp = prbp->prb_rules;
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
    case PIN_TYPE_OPEN:
	if (pin_name_id_equal(nodep->pn_name, prep->prp_atom_script)) {
	    psu_log("prep: open: script: %s", data);
	} else if (pin_name_id_equal(nodep->pn_name, prep->prp_atom_state)) {
	    psu_log("prep: open: state: %s", data);
	    id = GET_ATTRIB(prp_atom_id);
	    action = GET_ATTRIB(prp_atom_action);
	    psu_log("prep: open: state: [%s/%s]",
		    XX(id), XX(action));

	    /* Valid input requires a good state id number */
	    pin_rstate_id_t sid = pin_rstate_id(strtol(id, NULL, 0));
	    pa_atom_t sid_n = pa_fixed_atom_of(pin_rstate_id_atom_of(sid));
	    if (sid_n > pa_fixed_max_atoms(prbp->prb_states)) {
		psu_log("state id > max: %u .vs. %u",
			sid_n, pa_fixed_max_atoms(prbp->prb_states));
		break;
	    }

	    pin_rstate_t *statep = pin_rstate_element(prbp, sid);
	    if (statep) {
		bzero(statep, sizeof(*statep));

		/* Set the stack "next" point to the first rule of the state */
		stackp->prps_nextp = &statep->prbs_first_rule;

		/* If an action was defined, build a default rule */
		if (action) {
		    pin_rule_id_t rid;
		    pin_rule_t *prp = pin_rule_alloc(prbp, &rid);
		    if (prp == NULL)
			break;

		    bzero(prp, sizeof(*prp));
		    prp->pr_flags = PRF_MATCH_ALL;
		    prp->pr_action = pin_rule_action_value(action);

		    /* Record the rule as the default for this state */
		    statep->prbs_default_rule = rid;
		}
	    }

	    /* Update prsi_max_state */
	    if (sid_n > pa_fixed_atom_of(pin_rstate_id_atom_of(
					 prbp->prb_infop->prsi_max_state)))
		prbp->prb_infop->prsi_max_state = sid;

	} else if (pin_name_id_equal(nodep->pn_name, prep->prp_atom_rule)) {
	    psu_log("prep: open: rule: %s", data);
	    tag = GET_ATTRIB(prp_atom_tag);
	    action = GET_ATTRIB(prp_atom_action);
	    new_state = GET_ATTRIB(prp_atom_new_state);
	    use_tag = GET_ATTRIB(prp_atom_use_tag);
	    psu_log("prep: open: rule: [%s/%s/%s/%s]",
		    XX(tag), XX(action), XX(new_state), XX(use_tag));

	    pin_rule_id_t rid;
	    pin_rule_t *prp = pin_rule_alloc(prbp, &rid);
	    if (prp == NULL)
		break;

	    bzero(prp, sizeof(*prp));
	    if (tag)
		pin_rule_bitmap_add(prbp, prp, tag);

	    if (action)
		prp->pr_action = pin_rule_action_value(action);
	    if (use_tag)
		prp->pr_use_tag = pin_parse_namepool_atom(prbp->prb_script, use_tag);
	    if (new_state)
		prp->pr_new_state = pin_rstate_id(strtol(new_state, NULL, 0));

	    /* Add rule to linked list of rules */
	    *stackp->prps_nextp = stackp->prps_rule = rid;
	    stackp->prps_nextp = &prp->pr_next;

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
    pin_workspace_t *pwp = input->pp_insert->pin_tree->pt_workspace;
    pin_rulebook_t *prbp = pin_rulebook_setup(pwp, input, name);
    pin_rulebook_prep_t prep;

    if (prbp == NULL)
	return NULL;

    bzero(&prep, sizeof(prep));

    prep.prp_rulebook = prbp;
    prep.prp_script = input;

    /* We need all the atom number for the bits we care about */
    /* XXX rewrite as array/loop */
    prep.prp_atom_action = pin_parse_namepool_atom(input, "action");
    prep.prp_atom_id = pin_parse_namepool_atom(input, "id");
    prep.prp_atom_new_state = pin_parse_namepool_atom(input, "new-state");
    prep.prp_atom_rule = pin_parse_namepool_atom(input, "rule");
    prep.prp_atom_script = pin_parse_namepool_atom(input, "script");
    prep.prp_atom_state = pin_parse_namepool_atom(input, "state");
    prep.prp_atom_tag = pin_parse_namepool_atom(input, "tag");
    prep.prp_atom_use_tag = pin_parse_namepool_atom(input, "use-tag");

    pin_parse_emit(input, pin_rulebook_prep_cb, &prep);

    return prbp;
}

/*
 * Find the appropriate rule to process incoming data
 */
pin_rule_t *
pin_rulebook_find (pin_parse_t *parsep UNUSED, pin_rulebook_t *prbp,
		  pin_rstate_t *statep,
		  pin_name_id_t name_id,
		  const char *pref UNUSED, const char *name,
		  const char *attribs UNUSED)
{
    if (prbp == NULL)		/* No rulebook means no rules */
	return NULL;

    if (statep == NULL)
	return NULL;

    pin_rule_id_t rid;
    pin_rule_t *prp;
    for (rid = statep->prbs_first_rule; !pin_rule_id_is_null(rid);
	 rid = prp->pr_next) {
	prp = pin_rulebook_rule(prbp, rid);
	if (prp == NULL)
	    continue;

	/* See if our tag is in the bitmap for this rule */
	if (!pa_bitmap_test(prbp->prb_bitmaps, prp->pr_bitmap,
			    pin_name_id_atom_of(name_id)))
	    continue;

	psu_log("rule match: %u/'%s' rule %u: action %u/%s, flags %#x, "
		"use-tag %u, new_state %u",
		pin_name_id_atom_of(name_id), name ?: "",
		pa_fixed_atom_of(pin_rule_id_atom_of(rid)),
		prp->pr_action, pin_rule_action_name(prp->pr_action),
		prp->pr_flags, pin_name_id_atom_of(prp->pr_use_tag),
		pa_fixed_atom_of(pin_rstate_id_atom_of(prp->pr_new_state)));

	return prp;		/* Success! */
    }

    /* No explicit rule matched; fall back to the state's default rule */
    if (!pin_rule_id_is_null(statep->prbs_default_rule))
	return pin_rulebook_rule(prbp, statep->prbs_default_rule);

    return NULL;
}

/*
 * Turn a bitmap in a rule into a string, expanding names
 */
static const char *
pin_rule_bitmap_string (pin_rulebook_t *prbp, pin_rule_t *prp,
			char *buf, size_t bufsiz)
{
    pa_bitmap_t *pbp = prbp->prb_bitmaps;
    pa_bitmap_id_t bitmap = prp->pr_bitmap;
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
	str = pin_parse_namepool_string(prbp->prb_script, pin_name_id(num));

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
pin_rulebook_dump_rule (pin_rulebook_t *prbp, pin_rule_id_t rid, const char *tag)
{
    pin_rule_t *rulep = pin_rulebook_rule(prbp, rid);
    if (rulep == NULL)
	return pin_rule_id_null_atom();

    const char *rname = pin_rule_action_name(rulep->pr_action);
    char buf[1024];

    psu_log("    %srule %u:", tag,
	    pa_fixed_atom_of(pin_rule_id_atom_of(rid)));
    psu_log("        bitmap: %s",
	    pin_rule_bitmap_string(prbp, rulep, buf, sizeof(buf)));
    psu_log("        flags %#x, action %u/%s, use-tag %u, "
	    "new_state %u, next %u",
	    rulep->pr_flags, rulep->pr_action, rname,
	    pin_name_id_atom_of(rulep->pr_use_tag),
	    pa_fixed_atom_of(pin_rstate_id_atom_of(rulep->pr_new_state)),
	    pa_fixed_atom_of(pin_rule_id_atom_of(rulep->pr_next)));

    return rulep->pr_next;
}

/*
 * Cause sometimes you just need to see what's really going on....
 */
void
pin_rulebook_dump (pin_rulebook_t *prbp)
{
    pa_atom_t sid;
    pa_atom_t max_sid = pa_fixed_atom_of(
		pin_rstate_id_atom_of(prbp->prb_infop->prsi_max_state));
    pin_rule_id_t rid;
    pin_rstate_t *statep;

    psu_log("dumping rulebook");

    for (sid = 1; sid <= max_sid; sid++) {
	statep = (pin_rstate_t *) pa_fixed_element(prbp->prb_states, sid);
	if (statep == NULL)
	    continue;

	psu_log("state %u: flags %#x, default rule %u",
		sid, statep->prbs_flags,
		pa_fixed_atom_of(pin_rule_id_atom_of(statep->prbs_default_rule)));

	/* Dump the full set of rules */
	for (rid = statep->prbs_first_rule; !pin_rule_id_is_null(rid); )
	    rid = pin_rulebook_dump_rule(prbp, rid, "");

	/* Dump the default rule */
	if (!pin_rule_id_is_null(statep->prbs_default_rule))
	    pin_rulebook_dump_rule(prbp, statep->prbs_default_rule, "default ");
    }
}
