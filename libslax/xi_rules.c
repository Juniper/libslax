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
#include <libslax/xi_common.h>
#include <libslax/xi_rules.h>
#include <libslax/xi_tree.h>
#include <libslax/xi_parse.h>

xi_ruleset_t *
xi_ruleset_setup (pa_mmap_t *pmp, const char *name)
{
    char namebuf[PA_MMAP_HEADER_NAME_LEN];
    xi_ruleset_info_t *infop;
    pa_fixed_t *rules;
    pa_fixed_t *states;

    infop = pa_mmap_header(pmp, xi_mk_name(namebuf, name, "rules.info"),
			  PA_TYPE_OPAQUE, 0, sizeof(*infop));

    rules = pa_fixed_open(pmp, xi_mk_name(namebuf, name, "rules.set"),
			  XI_SHIFT, sizeof(*rules), XI_MAX_ATOMS);

    states = pa_fixed_open(pmp, xi_mk_name(namebuf, name, "rules.states"),
			   XI_SHIFT, sizeof(*states), XI_MAX_ATOMS);

    if (infop == NULL || rules == NULL || states == NULL)
	return NULL;
    
    xi_ruleset_t *xsrp = calloc(1, sizeof(*rules));

    if (xsrp) {
	xsrp->xrs_mmap = pmp;
	xsrp->xrs_infop = infop;
	xsrp->xrs_rules = rules;
	xsrp->xrs_states = states;
    }

    return xsrp;
}

static int
xi_rules_prep_cb (xi_parse_t *parsep UNUSED, xi_node_type_t type,
		  xi_node_t *nodep UNUSED,
		  const char *data UNUSED, void *opaque UNUSED)
{
    xi_ruleset_t *rules UNUSED = opaque;

    pa_atom_t atom_action UNUSED = xi_parse_atom(parsep, "action");
    pa_atom_t atom_count UNUSED = xi_parse_atom(parsep, "count");
    pa_atom_t atom_id UNUSED = xi_parse_atom(parsep, "id");
    pa_atom_t atom_rule UNUSED = xi_parse_atom(parsep, "rule");
    pa_atom_t atom_script UNUSED = xi_parse_atom(parsep, "script");
    pa_atom_t atom_state UNUSED = xi_parse_atom(parsep, "state");
    pa_atom_t atom_tag UNUSED = xi_parse_atom(parsep, "tag");

    switch (type) {
    case XI_TYPE_OPEN:
	if (nodep->xn_name == atom_script) {
	} else if (nodep->xn_name == atom_state) {
	} else if (nodep->xn_name == atom_rule) {
	} else {
	}
	break;
    }

    return 0;
}

xi_ruleset_t *
xi_rules_prep (xi_parse_t *input, const char *name)
{
    pa_mmap_t *pmp = input->xp_insert->xi_tree->xt_mmap;
    xi_ruleset_t *rules = xi_ruleset_setup(pmp, name);

    if (rules == NULL)
	return NULL;

    xi_parse_emit(input, xi_rules_prep_cb, rules);

    return rules;
}

xi_rule_t *
xi_ruleset_find (xi_parse_t *parsep UNUSED, pa_atom_t name_atom UNUSED,
		 const char *pref UNUSED, const char *name UNUSED,
		 const char *attribs UNUSED)
{
    return &parsep->xp_default_rule;
}
