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
 * Selection-only compiler: SLAX/XSLT xmlDoc -> pin filter.
 *
 * Scans the top-level children of an xsl:stylesheet for xsl:template
 * elements that carry a match= attribute (match-pattern templates).
 * For each one, allocates a pin_rule_t in the provided rulebook (with
 * pr_mode set to the template's mode= as a namepool atom) and registers
 * the match pattern with the filter via pin_filter_add_with_action.
 * All modes are compiled in one pass; the parser's execution context
 * selects which rules fire at runtime based on the current mode.
 *
 * What is NOT handled here (deferred to later items):
 *   - named templates / call-template (item 15)
 *   - variables and parameters (item 14)
 *   - template body expression language (item 17)
 *   - apply-templates body / frame stack (item 16)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slaxconfig.h"
#include <libpsu/psulog.h>
#include <libxml/tree.h>

#include <parrotdb/pacommon.h>
#include <parrotdb/paconfig.h>
#include <parrotdb/pammap.h>
#include <parrotdb/pafixed.h>

#include <libxo/xo.h>
#include "xo_filter.h"

#include <libpin/pin_common.h>
#include <libpin/pin_workspace.h>
#include <libpin/pin_rules.h>
#include <libpin/pin_filter.h>
#include <libpin/pin_slax.h>

/* Standard XSLT namespace URI */
#define PIN_SLAX_XSL_URI "http://www.w3.org/1999/XSL/Transform"

/*
 * Return non-zero if nodep is an element in the XSLT namespace,
 * optionally requiring its local name to match 'name'.
 * name == NULL matches any XSLT element.
 */
static int
pin_slax_is_xsl (xmlNodePtr nodep, const char *name)
{
    if (nodep == NULL || nodep->type != XML_ELEMENT_NODE)
	return 0;

    return nodep->ns && nodep->ns->href
	&& (name == NULL
	    || strcmp((const char *) nodep->name, name) == 0)
	&& strcmp((const char *) nodep->ns->href, PIN_SLAX_XSL_URI) == 0;
}

int
pin_slax_compile (xmlDocPtr docp, xo_filter_t *xfp, pin_rulebook_t *rb,
		  pin_action_type_t action)
{
    if (docp == NULL || xfp == NULL || rb == NULL)
	return -1;

    xmlNodePtr root = xmlDocGetRootElement(docp);
    if (!pin_slax_is_xsl(root, NULL)) {
	psu_log("pin_slax_compile: document root is not an xsl:* element");
	return -1;
    }

    int count = 0;

    for (xmlNodePtr child = root->children; child; child = child->next) {
	if (!pin_slax_is_xsl(child, "template"))
	    continue;

	xmlChar *match = xmlGetProp(child, (const xmlChar *) "match");
	if (match == NULL)
	    continue;		/* named template without match=; skip */

	/* Intern the mode string as a namepool atom (PA_NULL_ATOM = default) */
	xmlChar *tmode = xmlGetProp(child, (const xmlChar *) "mode");
	pa_atom_t mode_atom = PA_NULL_ATOM;
	if (tmode && tmode[0])
	    mode_atom = pin_namepool_atom(rb->prb_workspace,
					 (const char *) tmode, TRUE);
	if (tmode)
	    xmlFree(tmode);

	pin_rule_id_t rid;
	pin_rule_t *prp = pin_rule_alloc(rb, &rid);
	if (prp == NULL) {
	    psu_log("pin_slax_compile: pin_rule_alloc failed");
	    xmlFree(match);
	    return -1;
	}

	bzero(prp, sizeof(*prp));
	prp->pr_action = action;
	prp->pr_mode = mode_atom;

	int rc = pin_filter_add_with_action(xfp, (const char *) match, rid);
	xmlFree(match);

	if (rc < 0) {
	    psu_log("pin_slax_compile: pin_filter_add_with_action failed");
	    return -1;
	}

	count++;
    }

    return count;
}

#define PIN_SLAX_MAX_MODES 64

int
pin_slax_for_each_mode (xmlDocPtr docp, pin_slax_mode_fn fn, void *opaque)
{
    xmlNodePtr root = xmlDocGetRootElement(docp);
    if (!pin_slax_is_xsl(root, NULL))
	return -1;

    struct { xmlChar *mode; int count; } seen[PIN_SLAX_MAX_MODES];
    int nseen = 0;
    int default_count = 0;

    for (xmlNodePtr child = root->children; child; child = child->next) {
	if (!pin_slax_is_xsl(child, "template"))
	    continue;

	xmlChar *match = xmlGetProp(child, (const xmlChar *) "match");
	if (match == NULL)
	    continue;		/* named template, skip */
	xmlFree(match);

	xmlChar *tmode = xmlGetProp(child, (const xmlChar *) "mode");

	if (tmode == NULL || tmode[0] == '\0') {
	    if (tmode)
		xmlFree(tmode);
	    default_count++;
	    continue;
	}

	int found = FALSE;
	for (int i = 0; i < nseen; i++) {
	    if (strcmp((const char *) seen[i].mode, (const char *) tmode) == 0) {
		seen[i].count++;
		found = TRUE;
		break;
	    }
	}

	if (!found && nseen < PIN_SLAX_MAX_MODES) {
	    seen[nseen].mode = tmode;	/* retained for dedup; freed below */
	    seen[nseen].count = 1;
	    nseen++;
	} else {
	    xmlFree(tmode);
	}
    }

    int total = (default_count > 0 ? 1 : 0) + nseen;

    if (fn) {
	if (default_count > 0)
	    fn(opaque, NULL, default_count);
	for (int i = 0; i < nseen; i++)
	    fn(opaque, (const char *) seen[i].mode, seen[i].count);
    }

    for (int i = 0; i < nseen; i++)
	xmlFree(seen[i].mode);

    return total;
}
