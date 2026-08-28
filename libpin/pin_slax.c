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
pin_slax_body_instr_new (pin_rulebook_t *rb, pin_body_instr_id_t **nextp,
			  pin_body_instr_id_t *bidp)
{
    pin_body_instr_id_t bid;
    pin_body_instr_t *bip = pin_body_instr_alloc(rb, &bid);
    if (bip == NULL)
	return NULL;
    bzero(bip, sizeof(*bip));
    **nextp = bid;
    *nextp = &bip->bi_next;
    if (bidp)
	*bidp = bid;
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
		pin_body_instr_t *bip = pin_slax_body_instr_new(rb, nextp, NULL);
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
	    pin_body_instr_t *bip = pin_slax_body_instr_new(rb, nextp, NULL);
	    if (bip == NULL) return;
	    bip->bi_type = BIA_COPY;
	    continue;
	}

	/* xsl:value-of → BIA_VALUE_OF (emit text content or attribute value) */
	if (pin_slax_is_xsl(child, "value-of")) {
	    xmlChar *sel = xmlGetProp(child, (const xmlChar *) "select");
	    const char *sstr = sel ? (const char *) sel : NULL;
	    if (sstr == NULL || strcmp(sstr, ".") == 0) {
		/* select="." — pause and collect text children */
		pin_body_instr_t *bip = pin_slax_body_instr_new(rb, nextp, NULL);
		if (bip != NULL)
		    bip->bi_type = BIA_VALUE_OF;
	    } else if (sstr[0] == '@' && sstr[1] != '\0'
		       && strpbrk(sstr + 1, "/@[]()*") == NULL) {
		/* select="@attr" — emit attribute value synchronously */
		pin_body_instr_t *bip = pin_slax_body_instr_new(rb, nextp, NULL);
		if (bip != NULL) {
		    bip->bi_type = BIA_VALUE_OF;
		    bip->bi_select = pin_namepool_atom(rb->prb_workspace,
						       sstr + 1, TRUE);
		}
	    } else {
		psu_log("pin_slax: skipping value-of with complex select: %s", sstr);
	    }
	    if (sel)
		xmlFree(sel);
	    continue;
	}

	/*
	 * xsl:if → BIA_IF + BIA_JUMP join point.
	 *
	 * Compiled layout in the instruction list:
	 *   [BIA_IF] bi_next→[true-body...] bi_else→[BIA_JUMP]
	 *   [true-body instructions, last bi_next→BIA_JUMP]
	 *   [BIA_JUMP] bi_next→[first after-if instruction]
	 *   [after-if instructions...]
	 *
	 * BIA_JUMP is a stable join point whose ID is known at compile time,
	 * avoiding the need for forward-reference backpatching.
	 */
	if (pin_slax_is_xsl(child, "if")) {
	    xmlChar *test = xmlGetProp(child, (const xmlChar *) "test");
	    if (test == NULL || test[0] == '\0') {
		if (test) xmlFree(test);
		continue;	/* Empty test skips the whole if */
	    }

	    /*
	     * Build a standalone filter for this condition.  The pattern
	     * is `*[@condition]` — wildcard element so the same compiled
	     * filter works regardless of which element name fires the rule.
	     */
	    char xpath_buf[1024];
	    snprintf(xpath_buf, sizeof(xpath_buf),
		     "*[%s]", (const char *) test);
	    xmlFree(test);

	    xo_filter_t *cond_filter = xo_filter_create_standalone();
	    if (cond_filter == NULL) return;
	    if (xo_filter_walk_add(NULL, cond_filter, xpath_buf) < 0) {
		xo_filter_destroy_standalone(cond_filter);
		return;
	    }
	    uint32_t fidx = pin_rulebook_if_filter_add(rb, cond_filter);
	    if (fidx == UINT32_MAX) {
		xo_filter_destroy_standalone(cond_filter);
		return;
	    }

	    /* Create BIA_IF; save its id so we can set bi_else later */
	    pin_body_instr_id_t bid_if;
	    pin_body_instr_t *bip_if =
		pin_slax_body_instr_new(rb, nextp, &bid_if);
	    if (bip_if == NULL) return;
	    bip_if->bi_type = BIA_IF;
	    bip_if->bi_filter_idx = fidx;

	    /* Compile the true-body; bi_next of BIA_IF is filled in here */
	    pin_slax_compile_body_r(child, rb, nextp);

	    /*
	     * Create BIA_JUMP after the true-body.  This fills in bi_next of
	     * the last true-body instruction and provides a stable ID for
	     * BIA_IF.bi_else.  The false branch lands here and falls through
	     * to whatever instruction is wired into BIA_JUMP.bi_next next.
	     */
	    pin_body_instr_id_t bid_jump;
	    pin_body_instr_t *bip_jump =
		pin_slax_body_instr_new(rb, nextp, &bid_jump);
	    if (bip_jump == NULL) return;
	    bip_jump->bi_type = BIA_JUMP;

	    /* Wire the false branch: BIA_IF.bi_else → BIA_JUMP */
	    bip_if->bi_else = bid_jump;
	    continue;
	}

	/* xsl:apply-templates → BIA_APPLY (dispatch children through rules) */
	if (pin_slax_is_xsl(child, "apply-templates")) {
	    pin_body_instr_t *bip = pin_slax_body_instr_new(rb, nextp, NULL);
	    if (bip == NULL) return;
	    bip->bi_type = BIA_APPLY;
	    xmlChar *sel = xmlGetProp(child, (const xmlChar *) "select");
	    if (sel) {
		bip->bi_select = pin_namepool_atom(rb->prb_workspace,
						   (const char *) sel, TRUE);
		xmlFree(sel);
	    }
	    xmlChar *mode = xmlGetProp(child, (const xmlChar *) "mode");
	    if (mode) {
		bip->bi_mode = pin_namepool_atom(rb->prb_workspace,
						 (const char *) mode, TRUE);
		xmlFree(mode);
	    }
	    continue;
	}

	/*
	 * xsl:choose → chain of BIA_IF (one per xsl:when) + optional
	 * BIA_JUMP else-entry + BIA_JUMP join point.
	 *
	 * Each xsl:when produces:
	 *   BIA_IF(cond) → when_body → BIA_GOTO(→join)
	 * The last BIA_IF's bi_else points to the else-entry BIA_JUMP (if
	 * there is an xsl:otherwise) or directly to the join BIA_JUMP.
	 * All BIA_GOTOs are backpatched with bid_join once it is known.
	 */
	if (pin_slax_is_xsl(child, "choose")) {
	    pin_body_instr_id_t bid_gotos[16];
	    int n_gotos = 0;
	    pin_body_instr_id_t bid_last_if = pin_body_instr_id_null_atom();
	    xmlNodePtr otherwise_node = NULL;

	    for (xmlNodePtr wc = child->children; wc; wc = wc->next) {
		if (wc->type != XML_ELEMENT_NODE)
		    continue;
		if (pin_slax_is_xsl(wc, "otherwise")) {
		    otherwise_node = wc;
		    continue;
		}
		if (!pin_slax_is_xsl(wc, "when"))
		    continue;

		xmlChar *test = xmlGetProp(wc, (const xmlChar *) "test");
		if (test == NULL || test[0] == '\0') {
		    if (test) xmlFree(test);
		    continue;
		}
		char xpath_buf[1024];
		snprintf(xpath_buf, sizeof(xpath_buf),
			 "*[%s]", (const char *) test);
		xmlFree(test);

		xo_filter_t *cond_filter = xo_filter_create_standalone();
		if (cond_filter == NULL) return;
		if (xo_filter_walk_add(NULL, cond_filter, xpath_buf) < 0) {
		    xo_filter_destroy_standalone(cond_filter);
		    return;
		}
		uint32_t fidx = pin_rulebook_if_filter_add(rb, cond_filter);
		if (fidx == UINT32_MAX) {
		    xo_filter_destroy_standalone(cond_filter);
		    return;
		}

		pin_body_instr_id_t bid_if;
		pin_body_instr_t *bip_if =
		    pin_slax_body_instr_new(rb, nextp, &bid_if);
		if (bip_if == NULL) return;
		bip_if->bi_type = BIA_IF;
		bip_if->bi_filter_idx = fidx;

		/* Backpatch previous BIA_IF's bi_else to chain to this one */
		if (!pin_body_instr_id_is_null(bid_last_if)) {
		    pin_body_instr_t *prev_if =
			pin_body_instr_addr(rb, bid_last_if);
		    if (prev_if) prev_if->bi_else = bid_if;
		}
		bid_last_if = bid_if;

		pin_slax_compile_body_r(wc, rb, nextp);

		if (n_gotos < 16) {
		    pin_body_instr_id_t bid_goto;
		    pin_body_instr_t *bip_goto =
			pin_slax_body_instr_new(rb, nextp, &bid_goto);
		    if (bip_goto == NULL) return;
		    bip_goto->bi_type = BIA_GOTO;
		    bid_gotos[n_gotos++] = bid_goto;
		}
	    }

	    if (otherwise_node != NULL) {
		pin_body_instr_id_t bid_oth;
		pin_body_instr_t *bip_oth =
		    pin_slax_body_instr_new(rb, nextp, &bid_oth);
		if (bip_oth == NULL) return;
		bip_oth->bi_type = BIA_JUMP;
		if (!pin_body_instr_id_is_null(bid_last_if)) {
		    pin_body_instr_t *last_if =
			pin_body_instr_addr(rb, bid_last_if);
		    if (last_if) last_if->bi_else = bid_oth;
		}
		pin_slax_compile_body_r(otherwise_node, rb, nextp);
	    }

	    pin_body_instr_id_t bid_join;
	    pin_body_instr_t *bip_join =
		pin_slax_body_instr_new(rb, nextp, &bid_join);
	    if (bip_join == NULL) return;
	    bip_join->bi_type = BIA_JUMP;

	    if (otherwise_node == NULL && !pin_body_instr_id_is_null(bid_last_if)) {
		pin_body_instr_t *last_if =
		    pin_body_instr_addr(rb, bid_last_if);
		if (last_if) last_if->bi_else = bid_join;
	    }
	    for (int i = 0; i < n_gotos; i++) {
		pin_body_instr_t *bip_g = pin_body_instr_addr(rb, bid_gotos[i]);
		if (bip_g) bip_g->bi_else = bid_join;
	    }
	    continue;
	}

	/* Other xsl:* instructions not yet handled */
	if (pin_slax_is_xsl(child, NULL))
	    continue;

	/* Literal element: open, recurse into children, close */
	pin_name_id_t tag_id = pin_namepool_atom(rb->prb_workspace,
						 (const char *) child->name, TRUE);
	{
	    pin_body_instr_t *bip = pin_slax_body_instr_new(rb, nextp, NULL);
	    if (bip == NULL) return;
	    bip->bi_type = BIA_EMIT_OPEN;
	    bip->bi_tag = tag_id;

	    /* Capture static attributes from the literal element, if any. */
	    if (child->properties) {
		char abuf[4096];
		size_t apos = 0;
		for (xmlAttrPtr ap = child->properties; ap; ap = ap->next) {
		    const char *aname = (const char *) ap->name;
		    const char *aval = (ap->children && ap->children->content)
				       ? (const char *) ap->children->content : "";
		    /* Skip attribute value templates ({expr}) — not yet supported. */
		    if (strchr(aval, '{'))
			continue;
		    if (apos > 0 && apos < sizeof(abuf) - 1)
			abuf[apos++] = ' ';
		    apos += (size_t) snprintf(abuf + apos, sizeof(abuf) - apos,
					     "%s=\"%s\"", aname, aval);
		    if (apos >= sizeof(abuf)) {
			apos = sizeof(abuf) - 1;
			break;
		    }
		}
		abuf[apos] = '\0';
		if (apos > 0)
		    bip->bi_select = pin_namepool_atom(rb->prb_workspace,
						       abuf, TRUE);
	    }
	}
	pin_slax_compile_body_r(child, rb, nextp);
	{
	    pin_body_instr_t *bip = pin_slax_body_instr_new(rb, nextp, NULL);
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
		|| bip->bi_type == BIA_APPLY || bip->bi_type == BIA_VALUE_OF)
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
	    /*
	     * No for-each: compile the template body as a body instruction list
	     * so it can be executed via BIA_APPLY dispatch.
	     */
	    pin_body_instr_id_t body_head = pin_slax_compile_body(child, rb);
	    if (!pin_body_instr_id_is_null(body_head)) {
		prp->pr_body = body_head;
		prp->pr_body_retain = pin_slax_body_retain(body_head, rb);
	    } else {
		prp->pr_action = action;
	    }
	}

	/*
	 * Register the rule with the filter for top-level element dispatch,
	 * and add it to the apply-templates patricia tree if the match pattern
	 * is a simple element name (no path, predicates, or wildcards).
	 *
	 * match="/" is special: no element filter event fires for the document
	 * root, so store the rule id in prb_root_rule and fire it manually in
	 * pin_parse when the first depth-0 element opens.
	 */
	const char *match_str = (const char *) match;
	if (match_str[0] == '/' && match_str[1] == '\0') {
	    rb->prb_root_rule = rid;
	    xmlFree(match);
	    count++;
	    continue;
	}
	int rc = pin_filter_add_with_action(xfp, match_str, rid);
	if (rc < 0) {
	    psu_log("pin_slax_compile: pin_filter_add_with_action failed");
	    xmlFree(match);
	    return -1;
	}

	if (strpbrk(match_str, "/@[]*.:") == NULL) {
	    pin_name_id_t match_id = pin_namepool_atom(rb->prb_workspace,
						       match_str, TRUE);
	    if (!pin_name_id_is_null(match_id))
		pin_rulebook_apply_add(rb, match_id, mode_id, rid);
	}

	xmlFree(match);
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
