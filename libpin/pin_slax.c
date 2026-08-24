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

/*
 * Inspect a for-each body to determine what action to emit for each
 * selected element.  Returns one of:
 *   PIA_SAVE    — body is empty, or contains <xsl:copy-of select="."/>
 *   PIA_LITERAL — body contains a non-XSL literal element
 *
 * For PIA_LITERAL, *out_tag and *out_text are set to the (xmlChar *)
 * values that the caller must xmlFree().  For other returns they are NULL.
 */
/*
 * Allocate one body instruction, chain it onto the linked list via *nextp,
 * and advance *nextp to point at the new instruction's bi_next field.
 * Returns a pointer to the new (zeroed) instruction, or NULL on failure.
 */
static pin_body_instr_t *
pin_slax_body_instr_new (pin_rulebook_t *rb, pin_body_instr_id_t **nextp)
{
    pin_body_instr_id_t bid;
    pin_body_instr_t *bip = pin_body_instr_alloc(rb, &bid);
    if (bip == NULL)
	return NULL;
    bzero(bip, sizeof(*bip));
    **nextp = bid;
    *nextp = &bip->bi_next;
    return bip;
}

/*
 * Compile the children of body_node into a linked list of body instructions.
 * *nextp is a pointer-to-pointer cursor that always points at the id field
 * where the next instruction's id should be written; recursive calls share
 * the same cursor so instructions are appended in source order.
 */
static void
pin_slax_compile_body_r (xmlNodePtr body_node, pin_rulebook_t *rb,
			  pin_body_instr_id_t **nextp)
{
    for (xmlNodePtr child = body_node->children; child; child = child->next) {
	if (child->type == XML_TEXT_NODE) {
	    /* Skip whitespace-only text (XSLT template formatting, not output) */
	    if (child->content && child->content[0] && !xmlIsBlankNode(child)) {
		pin_body_instr_t *bip = pin_slax_body_instr_new(rb, nextp);
		if (bip == NULL) return;
		bip->bi_type = BIA_EMIT_TEXT;
		bip->bi_text = pin_namepool_atom(rb->prb_workspace,
						 (const char *) child->content, TRUE);
	    }
	    continue;
	}
	if (child->type != XML_ELEMENT_NODE)
	    continue;

	/* xsl:copy-of → BIA_COPY (streaming copy of matched input element) */
	if (pin_slax_is_xsl(child, "copy-of")) {
	    pin_body_instr_t *bip = pin_slax_body_instr_new(rb, nextp);
	    if (bip == NULL) return;
	    bip->bi_type = BIA_COPY;
	    continue;
	}

	/* Other xsl:* instructions not yet handled */
	if (pin_slax_is_xsl(child, NULL))
	    continue;

	/* Literal element: open, recurse into children, close */
	pin_name_id_t tag_id = pin_namepool_atom(rb->prb_workspace,
						 (const char *) child->name, TRUE);
	{
	    pin_body_instr_t *bip = pin_slax_body_instr_new(rb, nextp);
	    if (bip == NULL) return;
	    bip->bi_type = BIA_EMIT_OPEN;
	    bip->bi_tag = tag_id;
	}
	pin_slax_compile_body_r(child, rb, nextp);
	{
	    pin_body_instr_t *bip = pin_slax_body_instr_new(rb, nextp);
	    if (bip == NULL) return;
	    bip->bi_type = BIA_EMIT_CLOSE;
	    bip->bi_tag = tag_id;
	}
    }
}

/*
 * Compile the template body rooted at body_node into a body instruction
 * list stored in the rulebook.  Returns the id of the first instruction,
 * or the null id for an empty body.
 */
static pin_body_instr_id_t
pin_slax_compile_body (xmlNodePtr body_node, pin_rulebook_t *rb)
{
    pin_body_instr_id_t head = pin_body_instr_id_null_atom();
    pin_body_instr_id_t *nextp = &head;
    pin_slax_compile_body_r(body_node, rb, &nextp);
    return head;
}

/*
 * Scan a compiled body instruction list and determine how much of the
 * matched input element must be retained in memory.
 */
static pin_body_retain_t
pin_slax_body_retain (pin_body_instr_id_t head, pin_rulebook_t *rb)
{
    pin_body_retain_t retain = BRETAIN_DISCARD;
    for (pin_body_instr_id_t cur = head; !pin_body_instr_id_is_null(cur); ) {
	pin_body_instr_t *bip = pin_body_instr_addr(rb, cur);
	if (bip == NULL) break;
	if (bip->bi_type == BIA_COPY || bip->bi_type == BIA_COPY_SELECT
		|| bip->bi_type == BIA_APPLY)
	    retain = BRETAIN_NONE;
	cur = bip->bi_next;
    }
    return retain;
}

/*
 * Scan a template body for the first simple xsl:for-each select="name".
 * Compiles the for-each body into a body instruction list and returns a
 * new rulebook state (select_name→body, default→DISCARD), or the null id
 * if none was found or the select is too complex.
 */
static pin_rstate_id_t
pin_slax_compile_foreach (xmlNodePtr template_node, pin_rulebook_t *rb)
{
    for (xmlNodePtr child = template_node->children; child; child = child->next) {
	if (!pin_slax_is_xsl(child, "for-each"))
	    continue;

	xmlChar *select = xmlGetProp(child, (const xmlChar *) "select");
	if (select == NULL)
	    continue;

	const char *sel = (const char *) select;

	/* Only handle simple element-name selects (no path, no predicates) */
	int simple = (strpbrk(sel, "/@[]*.:") == NULL && sel[0] != '\0');
	if (!simple) {
	    psu_log("pin_slax: skipping complex for-each select: %s", sel);
	    xmlFree(select);
	    continue;
	}

	pin_body_instr_id_t body_head = pin_slax_compile_body(child, rb);
	pin_rstate_id_t sid;

	if (pin_body_instr_id_is_null(body_head)) {
	    /* Empty body: stream selected elements through (copy them) */
	    sid = pin_rulebook_add_foreach_state(rb, sel, PIA_SAVE, PIA_DISCARD);
	} else {
	    pin_body_retain_t retain = pin_slax_body_retain(body_head, rb);
	    sid = pin_rulebook_add_foreach_body_state(rb, sel, body_head,
						      retain, PIA_DISCARD);
	}

	xmlFree(select);

	if (!pin_rstate_id_is_null(sid))
	    return sid;
    }
    return pin_rstate_id_null_atom();
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

	/* Intern the mode string as a namepool atom (null = default mode) */
	xmlChar *tmode = xmlGetProp(child, (const xmlChar *) "mode");
	pin_name_id_t mode_id = pin_name_id_null_atom();
	if (tmode && tmode[0])
	    mode_id = pin_namepool_atom(rb->prb_workspace,
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
	prp->pr_mode = mode_id;

	/* Check template body for for-each constructs */
	pin_rstate_id_t foreach_sid = pin_slax_compile_foreach(child, rb);
	if (!pin_rstate_id_is_null(foreach_sid)) {
	    /* Template contains a for-each: save the container, enter foreach state */
	    prp->pr_action = PIA_SAVE;
	    prp->pr_new_state = foreach_sid;
	} else {
	    prp->pr_action = action;
	}

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
