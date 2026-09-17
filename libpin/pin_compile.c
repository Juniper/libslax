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
#include <stdbool.h>
#include <ctype.h>
#include <math.h>

#include "slaxconfig.h"
#include <libpsu/psulog.h>
#include <libpsu/psuerror.h>
#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/uri.h>

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
#include <libpin/pin_compile.h>

/* Standard XSLT namespace URI */
#define PIN_XSL_URI "http://www.w3.org/1999/XSL/Transform"

/*
 * Return non-zero if nodep is an element in the XSLT namespace,
 * optionally requiring its local name to match 'name'.
 * name == NULL matches any XSLT element.
 */
static int
pin_is_xsl (xmlNodePtr nodep, const char *name)
{
    if (nodep == NULL || nodep->type != XML_ELEMENT_NODE)
	return 0;

    return nodep->ns && nodep->ns->href
	&& (name == NULL
	    || strcmp((const char *) nodep->name, name) == 0)
	&& strcmp((const char *) nodep->ns->href, PIN_XSL_URI) == 0;
}

/*
 * Emit a compiler-style error or warning anchored to an XML source node.
 * The filename comes from the node's owning document URL; the line number
 * from libxml2's node annotation.  Either may be absent.
 */
static void PSU_PRINTFLIKE(3, 4)
pin_error (pin_rulebook_t *rb, xmlNodePtr nodep, const char *fmt, ...)
{
    const char *fn = (nodep && nodep->doc && nodep->doc->URL)
        ? (const char *) nodep->doc->URL : NULL;
    int line = nodep ? (int) xmlGetLineNo(nodep) : 0;
    va_list vap;
    va_start(vap, fmt);
    psu_errorv(fn, line, fmt, vap);
    va_end(vap);
    if (rb && rb->prb_workspace)
        rb->prb_workspace->pw_errors += 1;
}

static void PSU_PRINTFLIKE(2, 3)
pin_warn (xmlNodePtr nodep, const char *fmt, ...)
{
    const char *fn = (nodep && nodep->doc && nodep->doc->URL)
        ? (const char *) nodep->doc->URL : NULL;
    int line = nodep ? (int) xmlGetLineNo(nodep) : 0;
    va_list vap;
    va_start(vap, fmt);
    psu_warningv(fn, line, fmt, vap);
    va_end(vap);
}

/*
 * Allocate one body instruction, chain it onto the linked list via *nextp,
 * and advance *nextp to point at the new instruction's bi_next field.
 * Returns a pointer to the new (zeroed) instruction, or NULL on failure.
 */
static pin_body_instr_t *
pin_body_instr_new (pin_rulebook_t *rb, pin_body_instr_id_t **nextp,
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
 * Try to parse test as a position()-based comparison.
 * Recognises: position() OP N  where OP is <, <=, =, !=, >, >= and N >= 0.
 * Returns 1 and fills op_out/n_out on success; 0 on no match.
 */
static int
pin_parse_position_test (const char *test, uint32_t *op_out, uint32_t *n_out)
{
    while (isspace((unsigned char) *test))
	test += 1;

    if (strncmp(test, "position()", 10) != 0)
	return 0;
    test += 10;

    while (isspace((unsigned char) *test))
	test += 1;

    uint32_t op;
    if (test[0] == '<' && test[1] == '=') {
	op = PCMP_LE;
	test += 2;
    } else if (test[0] == '>' && test[1] == '=') {
	op = PCMP_GE;
	test += 2;
    } else if (test[0] == '!' && test[1] == '=') {
	op = PCMP_NE;
	test += 2;
    } else if (test[0] == '<') {
	op = PCMP_LT;
	test += 1;
    } else if (test[0] == '>') {
	op = PCMP_GT;
	test += 1;
    } else if (test[0] == '=') {
	op = PCMP_EQ;
	test += 1;
    } else
	return 0;

    while (isspace((unsigned char) *test))
	test += 1;

    if (!isdigit((unsigned char) *test))
	return 0;

    char *endp = NULL;
    unsigned long n = strtoul(test, &endp, 10);
    if (endp == test)
	return 0;

    *op_out = op;
    *n_out = (uint32_t) n;
    return 1;
}

/*
 * Compile the children of body_node into a linked list of body instructions.
 * *nextp is a pointer-to-pointer cursor that always points at the id field
 * where the next instruction's id should be written; recursive calls share
 * the same cursor so instructions are appended in source order.
 */
static void
pin_compile_body_r (xmlNodePtr body_node, pin_rulebook_t *rb,
		      pin_body_instr_id_t **nextp)
{
    for (xmlNodePtr child = body_node->children; child; child = child->next) {
	if (child->type == XML_TEXT_NODE) {
	    /* Skip whitespace-only text (XSLT template formatting, not output) */
	    if (child->content && child->content[0] && !xmlIsBlankNode(child)) {
		pin_body_instr_t *bip = pin_body_instr_new(rb, nextp, NULL);
		if (bip == NULL)
		    return;
		bip->bi_type = BIA_EMIT_TEXT;
		bip->bi_text = pin_namepool_atom(rb->prb_workspace,
						 (const char *) child->content, TRUE);
	    }
	    continue;
	}
	if (child->type != XML_ELEMENT_NODE)
	    continue;

	/* xsl:copy-of → BIA_COPY (streaming copy of matched input element) */
	if (pin_is_xsl(child, "copy-of")) {
	    pin_body_instr_t *bip = pin_body_instr_new(rb, nextp, NULL);
	    if (bip == NULL)
		return;
	    bip->bi_type = BIA_COPY;
	    continue;
	}

	/* xsl:value-of → BIA_VALUE_OF (emit text content or attribute value) */
	if (pin_is_xsl(child, "value-of")) {
	    xmlChar *sel = xmlGetProp(child, (const xmlChar *) "select");
	    const char *sstr = sel ? (const char *) sel : NULL;
	    if (sstr == NULL || sstr[0] == '\0' || strcmp(sstr, ".") == 0) {
		/* select="." — pause and collect text children */
		pin_body_instr_t *bip = pin_body_instr_new(rb, nextp, NULL);
		if (bip != NULL)
		    bip->bi_type = BIA_VALUE_OF;
	    } else if (sstr[0] == '@' && sstr[1] != '\0'
		       && strpbrk(sstr + 1, "/@[]()*") == NULL) {
		/* select="@attr" — emit attribute value synchronously */
		pin_body_instr_t *bip = pin_body_instr_new(rb, nextp, NULL);
		if (bip != NULL) {
		    bip->bi_type = BIA_VALUE_OF;
		    bip->bi_select = pin_namepool_atom(rb->prb_workspace,
						       sstr + 1, TRUE);
		}
	    } else if (sstr[0] == '\'' || sstr[0] == '"') {
		/* select="'literal'" or select='"literal"' — static text */
		char q = sstr[0];
		size_t slen = strlen(sstr);
		if (slen >= 2 && sel[slen - 1] == q) {
		    sel[slen - 1] = '\0'; /* strip trailing quote in-place; sel freed below */
		    pin_body_instr_t *bip = pin_body_instr_new(rb, nextp, NULL);
		    if (bip != NULL) {
			bip->bi_type = BIA_EMIT_TEXT;
			bip->bi_text = pin_namepool_atom(rb->prb_workspace, sstr + 1, TRUE);
		    }
		}
	    } else {
		/* General path or expression: evaluate against context node */
		pin_body_instr_t *bip = pin_body_instr_new(rb, nextp, NULL);
		if (bip != NULL) {
		    bip->bi_type = BIA_VALUE_OF;
		    bip->bi_text = pin_namepool_atom(rb->prb_workspace, sstr, TRUE);
		}
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
	if (pin_is_xsl(child, "if")) {
	    xmlChar *test = xmlGetProp(child, (const xmlChar *) "test");
	    if (test == NULL || test[0] == '\0') {
		pin_error(rb, child, "xsl:if: missing or empty test attribute");
		if (test)
		    xmlFree(test);
		continue;
	    }

	    pin_body_instr_id_t bid_if;
	    pin_body_instr_t *bip_if;

	    uint32_t pcmp_op = 0, pcmp_n = 0;
	    if (pin_parse_position_test((const char *) test, &pcmp_op, &pcmp_n)) {
		/* position()-based test: emit BIA_IF_POSITION */
		xmlFree(test);
		bip_if = pin_body_instr_new(rb, nextp, &bid_if);
		if (bip_if == NULL)
		    return;
		bip_if->bi_type = BIA_IF_POSITION;
		bip_if->bi_tag = pin_name_id(pcmp_op); /* operator in tag atom */
		bip_if->bi_filter_idx = pcmp_n;         /* comparison value N */
	    } else {
		/*
		 * General test: build a standalone xo_filter.  The pattern
		 * is `*[condition]` — wildcard so the filter fires regardless
		 * of which element name fires the rule.
		 */
		char xpath_buf[1024];
		snprintf(xpath_buf, sizeof(xpath_buf),
			 "*[%s]", (const char *) test);
		xmlFree(test);

		xo_filter_t *cond_filter = xo_filter_create_standalone();
		if (cond_filter == NULL)
		    return;
		if (xo_filter_walk_add(NULL, cond_filter, xpath_buf) < 0) {
		    xo_filter_destroy_standalone(cond_filter);
		    return;
		}
		uint32_t fidx = pin_rulebook_if_filter_add(rb, cond_filter);
		if (fidx == UINT32_MAX) {
		    xo_filter_destroy_standalone(cond_filter);
		    return;
		}
		bip_if = pin_body_instr_new(rb, nextp, &bid_if);
		if (bip_if == NULL)
		    return;
		bip_if->bi_type = BIA_IF;
		bip_if->bi_filter_idx = fidx;
	    }

	    /* Compile the true-body; bi_next of bip_if is filled in here */
	    pin_compile_body_r(child, rb, nextp);

	    /* Create BIA_JUMP join point after the true-body */
	    pin_body_instr_id_t bid_jump;
	    pin_body_instr_t *bip_jump =
		pin_body_instr_new(rb, nextp, &bid_jump);
	    if (bip_jump == NULL)
		return;
	    bip_jump->bi_type = BIA_JUMP;

	    /* Wire the false branch: BIA_IF.bi_else → BIA_JUMP */
	    bip_if->bi_else = bid_jump;
	    continue;
	}

	/* xsl:apply-templates → BIA_APPLY (dispatch children through rules) */
	if (pin_is_xsl(child, "apply-templates")) {
	    pin_body_instr_t *bip = pin_body_instr_new(rb, nextp, NULL);
	    if (bip == NULL)
		return;
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
	if (pin_is_xsl(child, "choose")) {
	    pin_body_instr_id_t *bid_gotos = NULL;
	    int n_gotos = 0, gotos_size = 0;
	    pin_body_instr_id_t bid_last_if = pin_body_instr_id_null_atom();
	    xmlNodePtr otherwise_node = NULL;

	    for (xmlNodePtr wc = child->children; wc; wc = wc->next) {
		if (wc->type != XML_ELEMENT_NODE)
		    continue;
		if (pin_is_xsl(wc, "otherwise")) {
		    otherwise_node = wc;
		    continue;
		}
		if (!pin_is_xsl(wc, "when"))
		    continue;

		xmlChar *test = xmlGetProp(wc, (const xmlChar *) "test");
		if (test == NULL || test[0] == '\0') {
		    pin_error(rb, wc, "xsl:when: missing or empty test attribute");
		    if (test)
			xmlFree(test);
		    continue;
		}
		size_t tlen = strlen((const char *) test);
		char *xpath_buf = xo_realloc(NULL, tlen + 4); /* "*[" + test + "]" + NUL */
		if (xpath_buf == NULL) {
		    xmlFree(test);
		    return;
		}
		snprintf(xpath_buf, tlen + 4, "*[%s]", (const char *) test);
		xmlFree(test);

		xo_filter_t *cond_filter = xo_filter_create_standalone();
		if (cond_filter == NULL) {
		    xo_free(xpath_buf);
		    return;
		}
		if (xo_filter_walk_add(NULL, cond_filter, xpath_buf) < 0) {
		    xo_filter_destroy_standalone(cond_filter);
		    xo_free(xpath_buf);
		    return;
		}
		xo_free(xpath_buf);
		uint32_t fidx = pin_rulebook_if_filter_add(rb, cond_filter);
		if (fidx == UINT32_MAX) {
		    xo_filter_destroy_standalone(cond_filter);
		    return;
		}

		pin_body_instr_id_t bid_if;
		pin_body_instr_t *bip_if =
		    pin_body_instr_new(rb, nextp, &bid_if);
		if (bip_if == NULL)
		    return;
		bip_if->bi_type = BIA_IF;
		bip_if->bi_filter_idx = fidx;

		/* Backpatch previous BIA_IF's bi_else to chain to this one */
		if (!pin_body_instr_id_is_null(bid_last_if)) {
		    pin_body_instr_t *prev_if =
			pin_body_instr_addr(rb, bid_last_if);
		    if (prev_if)
			prev_if->bi_else = bid_if;
		}
		bid_last_if = bid_if;

		pin_compile_body_r(wc, rb, nextp);

		if (n_gotos >= gotos_size) {
		    int newsize = gotos_size ? gotos_size * 2 : 4;
		    pin_body_instr_id_t *ng = xo_realloc(bid_gotos,
					     newsize * sizeof(*bid_gotos));
		    if (ng == NULL) {
			xo_free(bid_gotos);
			return;
		    }
		    bid_gotos = ng;
		    gotos_size = newsize;
		}
		pin_body_instr_id_t bid_goto;
		pin_body_instr_t *bip_goto =
		    pin_body_instr_new(rb, nextp, &bid_goto);
		if (bip_goto == NULL) {
		    xo_free(bid_gotos);
		    return;
		}
		bip_goto->bi_type = BIA_GOTO;
		bid_gotos[n_gotos++] = bid_goto;
	    }

	    if (otherwise_node != NULL) {
		pin_body_instr_id_t bid_oth;
		pin_body_instr_t *bip_oth =
		    pin_body_instr_new(rb, nextp, &bid_oth);
		if (bip_oth == NULL)
		    return;
		bip_oth->bi_type = BIA_JUMP;
		if (!pin_body_instr_id_is_null(bid_last_if)) {
		    pin_body_instr_t *last_if =
			pin_body_instr_addr(rb, bid_last_if);
		    if (last_if)
			last_if->bi_else = bid_oth;
		}
		pin_compile_body_r(otherwise_node, rb, nextp);
	    }

	    pin_body_instr_id_t bid_join;
	    pin_body_instr_t *bip_join =
		pin_body_instr_new(rb, nextp, &bid_join);
	    if (bip_join == NULL) {
		xo_free(bid_gotos);
		return;
	    }
	    bip_join->bi_type = BIA_JUMP;

	    if (otherwise_node == NULL && !pin_body_instr_id_is_null(bid_last_if)) {
		pin_body_instr_t *last_if =
		    pin_body_instr_addr(rb, bid_last_if);
		if (last_if)
		    last_if->bi_else = bid_join;
	    }
	    for (int i = 0; i < n_gotos; i++) {
		pin_body_instr_t *bip_g = pin_body_instr_addr(rb, bid_gotos[i]);
		if (bip_g)
		    bip_g->bi_else = bid_join;
	    }
	    xo_free(bid_gotos);
	    continue;
	}

	/* xsl:sort is processed by the enclosing xsl:for-each compiler;
	 * silently skip it here when encountered during body compilation. */
	if (pin_is_xsl(child, "sort")) {
	    continue;
	}

	/* xsl:variable → BIA_VARIABLE */
	if (pin_is_xsl(child, "variable")) {
	    xmlChar *vname = xmlGetProp(child, (const xmlChar *) "name");
	    if (vname == NULL || vname[0] == '\0') {
		pin_error(rb, child, "xsl:variable: missing or empty name attribute");
		if (vname)
		    xmlFree(vname);
		continue;
	    }
	    xmlChar *vsel = xmlGetProp(child, (const xmlChar *) "select");
	    pin_body_instr_t *bip = pin_body_instr_new(rb, nextp, NULL);
	    if (bip) {
		bip->bi_type = BIA_VARIABLE;
		bip->bi_tag = pin_namepool_atom(rb->prb_workspace,
					        (const char *) vname, TRUE);
		if (vsel && vsel[0] != '\0')
		    bip->bi_select = pin_namepool_atom(rb->prb_workspace,
						       (const char *) vsel, TRUE);
	    }
	    xmlFree(vname);
	    if (vsel)
		xmlFree(vsel);
	    continue;
	}

	/*
	 * xsl:for-each -> BIA_FOR_EACH.
	 *
	 * bi_select = select path (may be absolute "/a/b" or relative "a/b")
	 * bi_text   = multi-key sort spec (newline-separated, "-" prefix = desc)
	 * bi_else   = head of the for-each body sub-list (compiled separately)
	 * bi_next   = first instruction after the for-each
	 */
	if (pin_is_xsl(child, "for-each")) {
	    xmlChar *fsel = xmlGetProp(child, (const xmlChar *) "select");
	    if (fsel == NULL || fsel[0] == '\0') {
		pin_error(rb, child, "xsl:for-each: missing or empty select attribute");
		if (fsel)
		    xmlFree(fsel);
		continue;
	    }

	    /* Build multi-key sort spec from xsl:sort children */
	    xo_buffer_t sortbuf;
	    xo_buf_init(&sortbuf);
	    for (xmlNodePtr sc = child->children; sc; sc = sc->next) {
		if (!pin_is_xsl(sc, "sort"))
		    continue;
		xmlChar *skey = xmlGetProp(sc, (const xmlChar *) "select");
		xmlChar *sord = xmlGetProp(sc, (const xmlChar *) "order");
		const char *kstr = skey ? (const char *) skey : ".";
		bool desc = sord && strcmp((const char *) sord, "descending") == 0;

		if (!xo_buf_is_empty(&sortbuf))
		    xo_buf_append(&sortbuf, "\n", 1);
		if (desc)
		    xo_buf_append(&sortbuf, "-", 1);
		xo_buf_append_str(&sortbuf, kstr);

		if (skey)
		    xmlFree(skey);
		if (sord)
		    xmlFree(sord);
	    }

	    pin_body_instr_id_t bid_fe;
	    pin_body_instr_t *bip_fe = pin_body_instr_new(rb, nextp, &bid_fe);
	    if (bip_fe == NULL) {
		xmlFree(fsel);
		xo_buf_cleanup(&sortbuf);
		return;
	    }
	    bip_fe->bi_type = BIA_FOR_EACH;
	    bip_fe->bi_select = pin_namepool_atom(rb->prb_workspace,
						   (const char *) fsel, TRUE);
	    if (!xo_buf_is_empty(&sortbuf)) {
		xo_buf_force_nul(&sortbuf);
		bip_fe->bi_text = pin_namepool_atom(rb->prb_workspace,
						     xo_buf_data(&sortbuf, 0), TRUE);
	    }
	    xo_buf_cleanup(&sortbuf);
	    xmlFree(fsel);

	    /* Compile for-each body as an independent sub-list via bi_else */
	    pin_body_instr_id_t body_head = pin_body_instr_id_null_atom();
	    pin_body_instr_id_t *body_nextp = &body_head;
	    pin_compile_body_r(child, rb, &body_nextp);
	    bip_fe->bi_else = body_head;
	    continue;
	}

	/* xsl:element → BIA_ELEMENT_OPEN + children + BIA_ELEMENT_CLOSE */
	if (pin_is_xsl(child, "element")) {
	    xmlChar *ename = xmlGetProp(child, (const xmlChar *) "name");
	    if (ename == NULL) {
		pin_warn(child, "xsl:element: missing name attribute");
		continue;
	    }
	    pin_body_instr_t *bip = pin_body_instr_new(rb, nextp, NULL);
	    if (bip == NULL) {
		xmlFree(ename);
		return;
	    }
	    bip->bi_type = BIA_ELEMENT_OPEN;
	    bip->bi_select = pin_namepool_atom(rb->prb_workspace,
					      (const char *) ename, TRUE);
	    xmlFree(ename);
	    pin_compile_body_r(child, rb, nextp);
	    pin_body_instr_t *cbip = pin_body_instr_new(rb, nextp, NULL);
	    if (cbip == NULL)
		return;
	    cbip->bi_type = BIA_ELEMENT_CLOSE;
	    continue;
	}

	/* xsl:attribute → BIA_ATTRIB: bi_tag=name, bi_text=value-AVT */
	if (pin_is_xsl(child, "attribute")) {
	    xmlChar *aname = xmlGetProp(child, (const xmlChar *) "name");
	    if (aname == NULL) {
		pin_warn(child, "xsl:attribute: missing name attribute");
		continue;
	    }
	    /* Build value AVT from child nodes */
	    xo_buffer_t vbuf;
	    xo_buf_init(&vbuf);
	    for (xmlNodePtr vc = child->children; vc; vc = vc->next) {
		if (vc->type == XML_TEXT_NODE && vc->content && vc->content[0]) {
		    xo_buf_append_str(&vbuf, (const char *) vc->content);
		} else if (pin_is_xsl(vc, "value-of")) {
		    xmlChar *sel = xmlGetProp(vc, (const xmlChar *) "select");
		    if (sel) {
			xo_buf_append(&vbuf, "{", 1);
			xo_buf_append_str(&vbuf, (const char *) sel);
			xo_buf_append(&vbuf, "}", 1);
			xmlFree(sel);
		    }
		}
	    }
	    pin_body_instr_t *bip = pin_body_instr_new(rb, nextp, NULL);
	    if (bip == NULL) {
		xmlFree(aname);
		xo_buf_cleanup(&vbuf);
		return;
	    }
	    bip->bi_type = BIA_ATTRIB;
	    bip->bi_tag = pin_namepool_atom(rb->prb_workspace,
					   (const char *) aname, TRUE);
	    if (!xo_buf_is_empty(&vbuf)) {
		xo_buf_force_nul(&vbuf);
		bip->bi_text = pin_namepool_atom(rb->prb_workspace,
						 xo_buf_data(&vbuf, 0), TRUE);
	    }
	    xo_buf_cleanup(&vbuf);
	    xmlFree(aname);
	    continue;
	}

	/* xsl:text → BIA_EMIT_TEXT (preserves whitespace; no xsl:text child elements) */
	if (pin_is_xsl(child, "text")) {
	    xo_buffer_t tbuf;
	    xo_buf_init(&tbuf);
	    for (xmlNodePtr tc = child->children; tc; tc = tc->next) {
		if (tc->type == XML_TEXT_NODE && tc->content)
		    xo_buf_append_str(&tbuf, (const char *) tc->content);
	    }
	    if (!xo_buf_is_empty(&tbuf)) {
		xo_buf_force_nul(&tbuf);
		pin_body_instr_t *bip = pin_body_instr_new(rb, nextp, NULL);
		if (bip != NULL) {
		    bip->bi_type = BIA_EMIT_TEXT;
		    bip->bi_text = pin_namepool_atom(rb->prb_workspace,
						     xo_buf_data(&tbuf, 0), TRUE);
		}
	    }
	    xo_buf_cleanup(&tbuf);
	    continue;
	}

	/* xsl:copy → BIA_COPY_OPEN + children + BIA_ELEMENT_CLOSE */
	if (pin_is_xsl(child, "copy")) {
	    pin_body_instr_t *bip = pin_body_instr_new(rb, nextp, NULL);
	    if (bip == NULL)
		continue;
	    bip->bi_type = BIA_COPY_OPEN;
	    pin_compile_body_r(child, rb, nextp);
	    pin_body_instr_t *cbip = pin_body_instr_new(rb, nextp, NULL);
	    if (cbip == NULL)
		continue;
	    cbip->bi_type = BIA_ELEMENT_CLOSE;
	    continue;
	}

	/* xsl:message → BIA_MESSAGE_OPEN + children + BIA_MESSAGE_CLOSE */
	if (pin_is_xsl(child, "message")) {
	    xmlChar *term = xmlGetProp(child, (const xmlChar *) "terminate");
	    pin_body_instr_t *bip = pin_body_instr_new(rb, nextp, NULL);
	    if (bip != NULL)
		bip->bi_type = BIA_MESSAGE_OPEN;
	    pin_compile_body_r(child, rb, nextp);
	    pin_body_instr_t *cbip = pin_body_instr_new(rb, nextp, NULL);
	    if (cbip != NULL) {
		cbip->bi_type = BIA_MESSAGE_CLOSE;
		if (term && xmlStrcmp(term, (const xmlChar *) "yes") == 0)
		    cbip->bi_tag = pin_namepool_atom(rb->prb_workspace, "yes", TRUE);
	    }
	    if (term)
		xmlFree(term);
	    continue;
	}

	/* xsl:comment → BIA_COMMENT_OPEN + children + BIA_COMMENT_CLOSE */
	if (pin_is_xsl(child, "comment")) {
	    pin_body_instr_t *bip = pin_body_instr_new(rb, nextp, NULL);
	    if (bip != NULL)
		bip->bi_type = BIA_COMMENT_OPEN;
	    pin_compile_body_r(child, rb, nextp);
	    pin_body_instr_t *cbip = pin_body_instr_new(rb, nextp, NULL);
	    if (cbip != NULL)
		cbip->bi_type = BIA_COMMENT_CLOSE;
	    continue;
	}

	/* xsl:processing-instruction → BIA_PI_OPEN + children + BIA_PI_CLOSE */
	if (pin_is_xsl(child, "processing-instruction")) {
	    xmlChar *piname = xmlGetProp(child, (const xmlChar *) "name");
	    pin_body_instr_t *bip = pin_body_instr_new(rb, nextp, NULL);
	    if (bip != NULL) {
		bip->bi_type = BIA_PI_OPEN;
		if (piname && piname[0])
		    bip->bi_tag = pin_namepool_atom(rb->prb_workspace,
						    (const char *) piname, TRUE);
	    }
	    if (piname)
		xmlFree(piname);
	    pin_compile_body_r(child, rb, nextp);
	    pin_body_instr_t *cbip = pin_body_instr_new(rb, nextp, NULL);
	    if (cbip != NULL)
		cbip->bi_type = BIA_PI_CLOSE;
	    continue;
	}

	/* xsl:number → BIA_NUMBER */
	if (pin_is_xsl(child, "number")) {
	    xmlChar *val = xmlGetProp(child, (const xmlChar *) "value");
	    xmlChar *fmt = xmlGetProp(child, (const xmlChar *) "format");
	    if (val && val[0]) {
		pin_body_instr_t *bip = pin_body_instr_new(rb, nextp, NULL);
		if (bip != NULL) {
		    bip->bi_type = BIA_NUMBER;
		    bip->bi_select = pin_namepool_atom(rb->prb_workspace,
						       (const char *) val, TRUE);
		    if (fmt && fmt[0])
			bip->bi_text = pin_namepool_atom(rb->prb_workspace,
							 (const char *) fmt, TRUE);
		}
	    }
	    if (val)
		xmlFree(val);
	    if (fmt)
		xmlFree(fmt);
	    continue;
	}

	/* Other xsl:* instructions not yet handled */
	if (pin_is_xsl(child, NULL)) {
	    pin_warn(child, "xsl:%s: element not supported", child->name);
	    continue;
	}

	/* Literal element: open, recurse into children, close */
	pin_name_id_t tag_id = pin_namepool_atom(rb->prb_workspace,
						 (const char *) child->name, TRUE);
	{
	    pin_body_instr_t *bip = pin_body_instr_new(rb, nextp, NULL);
	    if (bip == NULL)
		return;
	    bip->bi_type = BIA_EMIT_OPEN;
	    bip->bi_tag = tag_id;

	    /*
	     * Capture attributes from the literal element.
	     * Static-only attributes go in bi_select (existing path).
	     * If any attribute has an AVT ({expr}), the full attribute set
	     * is stored in bi_text for runtime expansion; bi_select is left
	     * null in that case so the executor uses bi_text instead.
	     */
	    if (child->properties) {
		xo_buffer_t abuf, avtbuf;
		xo_buf_init(&abuf);
		xo_buf_init(&avtbuf);
		bool has_avt = false;

		for (xmlAttrPtr ap = child->properties; ap; ap = ap->next) {
		    const char *aname = (const char *) ap->name;
		    const char *aval = (ap->children && ap->children->content)
				       ? (const char *) ap->children->content : "";

		    if (strchr(aval, '{'))
			has_avt = true;

		    /* Always add to AVT buffer */
		    if (!xo_buf_is_empty(&avtbuf))
			xo_buf_append(&avtbuf, " ", 1);
		    xo_buf_append_str(&avtbuf, aname);
		    xo_buf_append(&avtbuf, "=\"", 2);
		    xo_buf_append_str(&avtbuf, aval);
		    xo_buf_append(&avtbuf, "\"", 1);

		    /* Only add pure-static attributes to static buffer */
		    if (!strchr(aval, '{')) {
			if (!xo_buf_is_empty(&abuf))
			    xo_buf_append(&abuf, " ", 1);
			xo_buf_append_str(&abuf, aname);
			xo_buf_append(&abuf, "=\"", 2);
			xo_buf_append_str(&abuf, aval);
			xo_buf_append(&abuf, "\"", 1);
		    }
		}

		if (has_avt && !xo_buf_is_empty(&avtbuf)) {
		    xo_buf_force_nul(&avtbuf);
		    bip->bi_text = pin_namepool_atom(rb->prb_workspace,
						     xo_buf_data(&avtbuf, 0), TRUE);
		} else if (!xo_buf_is_empty(&abuf)) {
		    xo_buf_force_nul(&abuf);
		    bip->bi_select = pin_namepool_atom(rb->prb_workspace,
						       xo_buf_data(&abuf, 0), TRUE);
		}
		xo_buf_cleanup(&abuf);
		xo_buf_cleanup(&avtbuf);
	    }
	}
	pin_compile_body_r(child, rb, nextp);
	{
	    pin_body_instr_t *bip = pin_body_instr_new(rb, nextp, NULL);
	    if (bip == NULL)
		return;
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
pin_compile_body (xmlNodePtr body_node, pin_rulebook_t *rb)
{
    pin_body_instr_id_t head = pin_body_instr_id_null_atom();
    pin_body_instr_id_t *nextp = &head;
    pin_compile_body_r(body_node, rb, &nextp);
    return head;
}

/*
 * Scan a compiled body instruction list and determine how much of the
 * matched input element must be retained in memory.
 */
static pin_body_retain_t
pin_body_retain (pin_body_instr_id_t head, pin_rulebook_t *rb)
{
    pin_body_retain_t retain = BRETAIN_DISCARD;
    for (pin_body_instr_id_t cur = head; !pin_body_instr_id_is_null(cur); ) {
	pin_body_instr_t *bip = pin_body_instr_addr(rb, cur);
	if (bip == NULL)
	    break;
	if (bip->bi_type == BIA_COPY || bip->bi_type == BIA_COPY_SELECT
		|| bip->bi_type == BIA_APPLY || bip->bi_type == BIA_VALUE_OF)
	    retain = BRETAIN_NONE;
	if (bip->bi_type == BIA_FOR_EACH)
	    retain = BRETAIN_DOCUMENT;
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
pin_compile_foreach (xmlNodePtr template_node, pin_rulebook_t *rb)
{
    for (xmlNodePtr child = template_node->children; child; child = child->next) {
	if (!pin_is_xsl(child, "for-each"))
	    continue;

	xmlChar *select = xmlGetProp(child, (const xmlChar *) "select");
	if (select == NULL) {
	    pin_error(rb, child, "xsl:for-each: missing select attribute");
	    continue;
	}

	const char *sel = (const char *) select;

	/* Only handle simple element-name selects (no path, no predicates) */
	int simple = (strpbrk(sel, "/@[]*.:") == NULL && sel[0] != '\0');
	if (!simple) {
	    pin_warn(child, "xsl:for-each: complex select not supported: %s",
			  sel);
	    xmlFree(select);
	    continue;
	}

	pin_body_instr_id_t body_head = pin_compile_body(child, rb);
	pin_rstate_id_t sid;

	if (pin_body_instr_id_is_null(body_head)) {
	    /* Empty body: stream selected elements through (copy them) */
	    sid = pin_rulebook_add_foreach_state(rb, sel, PIA_SAVE, PIA_DISCARD);
	} else {
	    pin_body_retain_t retain = pin_body_retain(body_head, rb);
	    sid = pin_rulebook_add_foreach_body_state(rb, sel, body_head,
						      retain, PIA_DISCARD);
	}

	xmlFree(select);

	if (!pin_rstate_id_is_null(sid))
	    return sid;
    }
    return pin_rstate_id_null_atom();
}

/*
 * Walking cursor for the op-sequence compiler.  poc_nextp always points to
 * the pin_op_id_t field where the next allocated op's id should be written;
 * recursive calls share the same cursor so ops are chained in source order.
 */
typedef struct pin_op_cursor_s {
    pin_op_id_t    *poc_nextp;    /* field to fill with next op's id */
    pin_rulebook_t *poc_rb;       /* rulebook receiving the new ops */
    pin_name_id_t   poc_src_file; /* namepool atom for the source filename */
    uint32_t        poc_src_line; /* line number of the node being compiled */
    uint64_t        poc_complexity;  /* bitmask: bit (1<<type) set for each complex op present */
    xmlNodePtr      poc_node;     /* current source node (for error messages) */
} pin_op_cursor_t;

/* Cursor-anchored warning: use poc_src_file + poc_src_line. */
static void PSU_PRINTFLIKE(2, 3)
pin_cursor_warn (pin_op_cursor_t *cur, const char *fmt, ...)
{
    const char *fn = pin_namepool_string(cur->poc_rb->prb_workspace,
					 cur->poc_src_file);
    va_list vap;
    va_start(vap, fmt);
    psu_warningv(fn, (int) cur->poc_src_line, fmt, vap);
    va_end(vap);
}

/*
 * Allocate one op node, chain it at *poc_nextp, advance the cursor,
 * zero-fill the new node, and record source location.
 * If oidp is non-NULL, *oidp receives the new op's id (used for backpatching).
 */
static pin_op_t *
pin_op_new (pin_op_cursor_t *cur, pin_op_id_t *oidp)
{
    pin_op_id_t oid;
    pin_op_t *opp = pin_op_alloc(cur->poc_rb, &oid);
    if (opp == NULL)
	return NULL;

    bzero(opp, sizeof(*opp));
    if (oidp)
	*oidp = oid;

    *cur->poc_nextp = oid;
    cur->poc_nextp = &opp->po_next;

    opp->po_src_file = cur->poc_src_file;
    opp->po_src_line = cur->poc_src_line;
    return opp;
}

/*
 * Classify a select/test expression at compile time.
 *
 * SELK_DOWN covers "a", "a/b", "a/b/c" — all steps descend from context.
 * Everything that does not fit a simpler category and is not a clean
 * downward path is SELK_COMPLEX and causes a BIA_ complexity fallback.
 */
typedef enum {
    SELK_EMPTY,     /* null or empty string */
    SELK_SELF,      /* "." */
    SELK_ATTR,      /* "@name" */
    SELK_LITERAL,   /* "'string'" */
    SELK_VAR,       /* "$name" */
    SELK_DOWN,      /* "a", "a/b/c" — descending element path */
    SELK_COMPLEX,   /* "..", "/abs", "//", predicates, functions, etc. */
} pin_sel_kind_t;

static pin_sel_kind_t
pin_classify_select (const char *s)
{
    if (s == NULL || s[0] == '\0')
	return SELK_EMPTY;
    if (strcmp(s, ".") == 0)
	return SELK_SELF;
    if (s[0] == '@')
	return (s[1] != '\0' && strpbrk(s + 1, " \t=<>'\"/@[]()*") == NULL)
	    ? SELK_ATTR : SELK_COMPLEX;
    if (s[0] == '\'')
	return (strlen(s) >= 2 && s[strlen(s) - 1] == '\'')
	    ? SELK_LITERAL : SELK_COMPLEX;
    if (s[0] == '$')
	return (s[1] != '\0' && strpbrk(s + 1, " \t=<>'\"/@[]()*") == NULL)
	    ? SELK_VAR : SELK_COMPLEX;
    /* Downward: no absolute prefix, no upward steps, no predicates/funcs */
    if (s[0] != '/' && strstr(s, "..") == NULL
	    && strpbrk(s, " \t=<>'\"@[]()*") == NULL)
	return SELK_DOWN;
    return SELK_COMPLEX;
}

/*
 * Emit the PUSH_* op that best represents an xsl:if / xsl:when test
 * expression, leaving the result on the value stack for a following
 * PUSH_BOOL + IF to consume.
 *
 * Supported forms:
 *   @attr        → PUSH_ATTR  (non-null string → true)
 *   .            → PUSH_TEXT  (non-empty string → true)
 *   simple-name  → PUSH_NODES (non-empty nodeset → true)
 *
 * Anything more complex is stubbed as PUSH_BOOL (always false).
 */
static pin_op_t *
pin_op_push_for_test (pin_op_cursor_t *cur, const char *test)
{
    pin_workspace_t *pwp = cur->poc_rb->prb_workspace;

    pin_op_t *push = pin_op_new(cur, NULL);
    if (push == NULL)
	return NULL;

    switch (pin_classify_select(test)) {
    case SELK_ATTR:
	push->po_type = PIN_OP_PUSH_ATTR;
	push->po_name = pin_namepool_atom(pwp, test + 1, TRUE);
	break;
    case SELK_SELF:
	push->po_type = PIN_OP_PUSH_TEXT;
	push->po_name = pin_namepool_atom(pwp, ".", TRUE);
	break;
    case SELK_VAR:
	push->po_type = PIN_OP_LOAD_VAR;
	push->po_name = pin_namepool_atom(pwp, test + 1, TRUE);
	break;
    case SELK_DOWN:
	push->po_type = PIN_OP_PUSH_NODES;
	push->po_name = pin_namepool_atom(pwp, test, TRUE);
	break;
    default:
	push->po_type = PIN_OP_COMPLEX_EXPR;
	cur->poc_complexity |= (1ULL << PIN_OP_COMPLEX_EXPR);
	break;
    }
    return push;
}

/*
 * Recursively compile the children of body_node into PIN_OP_* sequences,
 * appended at the cursor position.  The layout mirrors the BIA_ compiler
 * above but uses pin_op_t nodes for the op-dispatch engine.
 */
static void
pin_compile_ops_r (xmlNodePtr body_node, pin_op_cursor_t *cur)
{
    pin_workspace_t *pwp = cur->poc_rb->prb_workspace;

    for (xmlNodePtr child = body_node->children; child; child = child->next) {
	cur->poc_src_line = (uint32_t) xmlGetLineNo(child);
	cur->poc_node = child;

	if (child->type == XML_TEXT_NODE) {
	    if (!child->content || xmlIsBlankNode(child))
		continue;

	    pin_op_t *push = pin_op_new(cur, NULL);
	    if (push == NULL)
		return;
	    push->po_type = PIN_OP_PUSH_STRING;
	    push->po_name = pin_namepool_atom(pwp,
		    (const char *) child->content, TRUE);

	    pin_op_t *emit = pin_op_new(cur, NULL);
	    if (emit == NULL)
		return;
	    emit->po_type = PIN_OP_EMIT;
	    continue;
	}

	if (child->type != XML_ELEMENT_NODE)
	    continue;

	/*
         * xsl:sort is processed by the enclosing xsl:for-each compiler;
	 * silently skip it here when encountered during body compilation.
         */
	if (pin_is_xsl(child, "sort"))
	    continue;

	if (pin_is_xsl(child, "copy-of")) {
	    xmlChar *sel = xmlGetProp(child, (const xmlChar *) "select");
	    const char *s = sel ? (const char *) sel : ".";
	    pin_sel_kind_t kind = pin_classify_select(s);

	    if (kind == SELK_COMPLEX || kind == SELK_LITERAL || kind == SELK_EMPTY) {
		/* Fall back to stub */
		cur->poc_complexity |= (1ULL << PIN_OP_PUSH_NODE);
		pin_op_t *push = pin_op_new(cur, NULL);
		if (push)
		    push->po_type = PIN_OP_PUSH_NODE;
		if (sel)
		    xmlFree(sel);
		continue;
	    }

	    pin_op_t *op = pin_op_new(cur, NULL);
	    if (op == NULL) {
		if (sel)
		    xmlFree(sel);
		return;
	    }
	    op->po_type = PIN_OP_COPY_OF;

	    if (kind == SELK_SELF) {
		op->po_name = pin_name_id_null_atom();   /* null = context node */
	    } else if (kind == SELK_VAR) {
		op->po_name = pin_namepool_atom(pwp, s, TRUE);  /* "$varname" */
	    } else {  /* SELK_DOWN */
		op->po_name = pin_namepool_atom(pwp, s, TRUE);
	    }

	    if (sel)
		xmlFree(sel);
	    continue;
	}

	if (pin_is_xsl(child, "value-of")) {
	    xmlChar *sel = xmlGetProp(child, (const xmlChar *) "select");
	    const char *s = sel ? (const char *) sel : ".";

	    pin_op_t *push = pin_op_new(cur, NULL);
	    if (push == NULL) {
		if (sel)
		    xmlFree(sel);
		return;
	    }

	    switch (pin_classify_select(s)) {
	    case SELK_EMPTY:
	    case SELK_SELF:
		push->po_type = PIN_OP_PUSH_TEXT;
		push->po_name = pin_namepool_atom(pwp, ".", TRUE);
		break;
	    case SELK_ATTR:
		push->po_type = PIN_OP_PUSH_ATTR;
		push->po_name = pin_namepool_atom(pwp, s + 1, TRUE);
		break;
	    case SELK_VAR:
		push->po_type = PIN_OP_LOAD_VAR;
		push->po_name = pin_namepool_atom(pwp, s + 1, TRUE);
		break;
	    case SELK_DOWN:
		push->po_type = PIN_OP_PUSH_TEXT;
		push->po_name = pin_namepool_atom(pwp, s, TRUE);
		break;
	    case SELK_LITERAL: {
		/* Strip the enclosing quotes in-place; sel is freed below */
		char q = s[0];
		size_t slen = strlen(s);
		if (slen >= 2 && sel[slen - 1] == q) {
		    sel[slen - 1] = '\0';
		    push->po_type = PIN_OP_PUSH_STRING;
		    push->po_name = pin_namepool_atom(pwp, s + 1, TRUE);
		}
		break;
	    }
	    default:  /* SELK_COMPLEX */
		push->po_type = PIN_OP_COMPLEX_EXPR;
		cur->poc_complexity |= (1ULL << PIN_OP_COMPLEX_EXPR);
		break;
	    }
	    if (sel)
		xmlFree(sel);

	    pin_op_t *emit = pin_op_new(cur, NULL);
	    if (emit == NULL)
		return;
	    emit->po_type = PIN_OP_EMIT;
	    continue;
	}

	if (pin_is_xsl(child, "apply-templates")) {
	    cur->poc_complexity |= (1ULL << PIN_OP_APPLY);
	    pin_op_t *apply = pin_op_new(cur, NULL);
	    if (apply == NULL)
		return;
	    apply->po_type = PIN_OP_APPLY;
	    xmlChar *mode = xmlGetProp(child, (const xmlChar *) "mode");
	    if (mode) {
		apply->po_name = pin_namepool_atom(pwp, (const char *) mode, TRUE);
		xmlFree(mode);
	    }
	    continue;
	}

	/*
	 * xsl:if → PUSH_* + PUSH_BOOL + IF + body + JUMP (join point).
	 *
	 *   IF  po_next → [first body op]   po_alt → JUMP
	 *   [body ops]  last po_next → JUMP
	 *   JUMP po_next → [continuation]
	 *
	 * The JUMP is allocated after the body so the cursor naturally
	 * chains the last body op to it.  IF.po_alt is backpatched here.
	 */
	if (pin_is_xsl(child, "if")) {
	    xmlChar *test = xmlGetProp(child, (const xmlChar *) "test");
	    if (test == NULL || test[0] == '\0') {
		pin_error(cur->poc_rb, child, "xsl:if: missing or empty test attribute");
		if (test)
		    xmlFree(test);
		continue;
	    }

	    if (pin_op_push_for_test(cur, (const char *) test) == NULL) {
		xmlFree(test);
		return;
	    }
	    xmlFree(test);

	    pin_op_t *push_bool = pin_op_new(cur, NULL);
	    if (push_bool == NULL)
		return;
	    push_bool->po_type = PIN_OP_PUSH_BOOL;

	    pin_op_id_t bid_if;
	    pin_op_t *bip_if = pin_op_new(cur, &bid_if);
	    if (bip_if == NULL)
		return;
	    bip_if->po_type = PIN_OP_IF;

	    pin_compile_ops_r(child, cur);

	    pin_op_id_t bid_jump;
	    pin_op_t *bip_jump = pin_op_new(cur, &bid_jump);
	    if (bip_jump == NULL)
		return;
	    bip_jump->po_type = PIN_OP_JUMP;

	    bip_if->po_alt = bid_jump;
	    continue;
	}

	/*
	 * xsl:choose → chain of (PUSH_* + PUSH_BOOL + IF + when-body + GOTO),
	 * optional JUMP + otherwise-body, and a final JUMP join point.
	 *
	 * Each IF.po_alt is backpatched to the next IF (false chain).
	 * The last IF.po_alt points to the otherwise entry or the join JUMP.
	 * All GOTO.po_alt fields are backpatched to the join JUMP.
	 */
	if (pin_is_xsl(child, "choose")) {
	    pin_op_id_t *gotos = NULL;
	    int n_gotos = 0, gotos_size = 0;
	    pin_op_id_t bid_last_if = pin_op_id_null_atom();
	    xmlNodePtr otherwise_node = NULL;

	    for (xmlNodePtr wc = child->children; wc; wc = wc->next) {
		if (wc->type != XML_ELEMENT_NODE)
		    continue;
		if (pin_is_xsl(wc, "otherwise")) {
		    otherwise_node = wc;
		    continue;
		}
		if (!pin_is_xsl(wc, "when"))
		    continue;

		xmlChar *test = xmlGetProp(wc, (const xmlChar *) "test");
		if (test == NULL || test[0] == '\0') {
		    pin_error(cur->poc_rb, wc, "xsl:when: missing or empty test attribute");
		    if (test)
			xmlFree(test);
		    continue;
		}

		cur->poc_src_line = (uint32_t) xmlGetLineNo(wc);
		/* Save nextp so we can recover the id of the first push op below */
		pin_op_id_t *saved_nextp = cur->poc_nextp;
		if (pin_op_push_for_test(cur, (const char *) test) == NULL) {
		    xmlFree(test);
		    return;
		}
		xmlFree(test);
		pin_op_id_t bid_when_start = *saved_nextp; /* id of the push op */

		pin_op_t *push_bool = pin_op_new(cur, NULL);
		if (push_bool == NULL)
		    return;
		push_bool->po_type = PIN_OP_PUSH_BOOL;

		pin_op_id_t bid_if;
		pin_op_t *bip_if = pin_op_new(cur, &bid_if);
		if (bip_if == NULL)
		    return;
		bip_if->po_type = PIN_OP_IF;

		/*
		 * Backpatch previous IF's false chain to the START of this
		 * when-branch evaluation (the push op), not to the IF itself.
		 * Jumping directly to the IF would skip the push and leave a
		 * stale value on the stack.
		 */
		if (!pin_op_id_is_null(bid_last_if)) {
		    pin_op_t *prev_if = pin_op_addr(cur->poc_rb, bid_last_if);
		    if (prev_if)
			prev_if->po_alt = bid_when_start;
		}
		bid_last_if = bid_if;

		pin_compile_ops_r(wc, cur);

		if (n_gotos >= gotos_size) {
		    int newsize = gotos_size ? gotos_size * 2 : 4;
		    pin_op_id_t *ng = xo_realloc(gotos, newsize * sizeof(*gotos));
		    if (ng == NULL) {
			xo_free(gotos);
			return;
		    }
		    gotos = ng;
		    gotos_size = newsize;
		}
		pin_op_id_t bid_goto;
		pin_op_t *bip_goto = pin_op_new(cur, &bid_goto);
		if (bip_goto == NULL) {
		    xo_free(gotos);
		    return;
		}
		bip_goto->po_type = PIN_OP_GOTO;
		gotos[n_gotos] = bid_goto;
		n_gotos += 1;
	    }

	    if (otherwise_node != NULL) {
		cur->poc_src_line = (uint32_t) xmlGetLineNo(otherwise_node);
		pin_op_id_t bid_oth;
		pin_op_t *bip_oth = pin_op_new(cur, &bid_oth);
		if (bip_oth == NULL)
		    return;
		bip_oth->po_type = PIN_OP_JUMP;

		if (!pin_op_id_is_null(bid_last_if)) {
		    pin_op_t *last_if = pin_op_addr(cur->poc_rb, bid_last_if);
		    if (last_if)
			last_if->po_alt = bid_oth;
		}
		pin_compile_ops_r(otherwise_node, cur);
	    }

	    pin_op_id_t bid_join;
	    pin_op_t *bip_join = pin_op_new(cur, &bid_join);
	    if (bip_join == NULL) {
		xo_free(gotos);
		return;
	    }
	    bip_join->po_type = PIN_OP_JUMP;

	    if (otherwise_node == NULL && !pin_op_id_is_null(bid_last_if)) {
		pin_op_t *last_if = pin_op_addr(cur->poc_rb, bid_last_if);
		if (last_if)
		    last_if->po_alt = bid_join;
	    }
	    for (int i = 0; i < n_gotos; i++) {
		pin_op_t *bip_g = pin_op_addr(cur->poc_rb, gotos[i]);
		if (bip_g)
		    bip_g->po_alt = bid_join;
	    }
	    xo_free(gotos);
	    continue;
	}

	if (pin_is_xsl(child, "for-each")) {
	    xmlChar *sel = xmlGetProp(child, (const xmlChar *) "select");
	    if (sel == NULL) {
		pin_error(cur->poc_rb, child, "xsl:for-each: missing select attribute");
		continue;
	    }
	    const char *s = (const char *) sel;

	    /* Only simple element-name selects (no path, predicates, wildcards) */
	    if (strpbrk(s, "/@[]*.:") != NULL) {
		cur->poc_complexity |= (1ULL << PIN_OP_COMPLEX_EXPR);
		xmlFree(sel);
		continue;
	    }

	    /* Collect xsl:sort children into a newline-separated spec string */
	    xo_buffer_t spec;
	    xo_buf_init(&spec);
	    bool sort_ok = true;
	    for (xmlNodePtr sp = child->children; sp; sp = sp->next) {
		if (!pin_is_xsl(sp, "sort"))
		    continue;

		xmlChar *order  = xmlGetProp(sp, (const xmlChar *) "order");
		xmlChar *corder = xmlGetProp(sp, (const xmlChar *) "case-order");
		xmlChar *dtype  = xmlGetProp(sp, (const xmlChar *) "data-type");
		xmlChar *kexpr  = xmlGetProp(sp, (const xmlChar *) "select");

		/* data-type="number" not yet supported in op-dispatch */
		if (dtype && strcmp((char *) dtype, "number") == 0)
		    sort_ok = false;

		const char *kstr = kexpr ? (const char *) kexpr : ".";
		if (sort_ok && strpbrk(kstr, "[()*") != NULL)
		    sort_ok = false;

		if (sort_ok) {
		    bool desc  = order  && strcmp((char *) order,  "descending") == 0;
		    bool upper = corder && strcmp((char *) corder, "upper-first") == 0;
		    if (!xo_buf_is_empty(&spec))
			xo_buf_append(&spec, "\n", 1);
		    if (desc)
			xo_buf_append(&spec, "-", 1);
		    if (upper)
			xo_buf_append(&spec, "^", 1);
		    xo_buf_append_str(&spec, kstr);
		}

		if (order)
		    xmlFree(order);
		if (corder)
		    xmlFree(corder);
		if (dtype)
		    xmlFree(dtype);
		if (kexpr)
		    xmlFree(kexpr);
	    }

	    if (!sort_ok) {
		cur->poc_complexity |= (1ULL << PIN_OP_COMPLEX_EXPR);
		xo_buf_cleanup(&spec);
		xmlFree(sel);
		continue;
	    }

	    /* Allocate PIN_OP_FOR_EACH op */
	    pin_op_t *fe = pin_op_new(cur, NULL);
	    if (fe == NULL) {
		xo_buf_cleanup(&spec);
		xmlFree(sel);
		continue;
	    }
	    fe->po_type = PIN_OP_FOR_EACH;
	    fe->po_name = pin_namepool_atom(pwp, s, TRUE);
	    if (!xo_buf_is_empty(&spec)) {
		xo_buf_force_nul(&spec);
		fe->po_name2 = pin_namepool_atom(pwp, xo_buf_data(&spec, 0), TRUE);
	    }
	    xo_buf_cleanup(&spec);
	    xmlFree(sel);

	    /* Compile body into independent sub-sequence at fe->po_alt */
	    pin_op_cursor_t sub = {
		.poc_nextp    = &fe->po_alt,
		.poc_rb       = cur->poc_rb,
		.poc_src_file = cur->poc_src_file,
		.poc_src_line = cur->poc_src_line,
		.poc_complexity = 0,
	    };
	    pin_compile_ops_r(child, &sub);

	    if (sub.poc_complexity) {
		/* Body too complex for op-dispatch; neutralise and fall back */
		cur->poc_complexity |= sub.poc_complexity;
		fe->po_type = PIN_OP_NONE;
	    }
	    continue;
	}

	/*
	 * xsl:variable name="x" select="expr"  — compile expr, emit STORE_VAR.
	 * xsl:variable name="x">text body</xsl:variable> — push literal text.
	 *
	 * Supported select forms: @attr, ., 'literal', $var, child-path.
	 * Complex expressions (predicates, functions) fall back to BIA_.
	 */
	if (pin_is_xsl(child, "variable")) {
	    xmlChar *vname = xmlGetProp(child, (const xmlChar *) "name");
	    if (vname == NULL || vname[0] == '\0') {
		pin_error(cur->poc_rb, child,
			  "xsl:variable: missing or empty name attribute");
		if (vname)
		    xmlFree(vname);
		continue;
	    }

	    xmlChar *sel = xmlGetProp(child, (const xmlChar *) "select");
	    pin_op_t *push = pin_op_new(cur, NULL);
	    if (push == NULL) {
		xmlFree(vname);
		if (sel)
		    xmlFree(sel);
		return;
	    }

	    if (sel != NULL) {
		const char *s = (const char *) sel;
		size_t slen = strlen(s);

		switch (pin_classify_select(s)) {
		case SELK_LITERAL: {
		    /* Strip enclosing quotes in-place; sel is freed below */
		    char q = s[0];
		    if (slen >= 2 && sel[slen - 1] == q) {
			sel[slen - 1] = '\0';
			push->po_type = PIN_OP_PUSH_STRING;
			push->po_name = pin_namepool_atom(pwp, s + 1, TRUE);
		    }
		    break;
		}
		case SELK_ATTR:
		    push->po_type = PIN_OP_PUSH_ATTR;
		    push->po_name = pin_namepool_atom(pwp, s + 1, TRUE);
		    break;
		case SELK_VAR:
		    push->po_type = PIN_OP_LOAD_VAR;
		    push->po_name = pin_namepool_atom(pwp, s + 1, TRUE);
		    break;
		case SELK_SELF:
		    push->po_type = PIN_OP_PUSH_TEXT;
		    push->po_name = pin_namepool_atom(pwp, ".", TRUE);
		    break;
		case SELK_DOWN:
		    /* Downward path: collect matching nodes as a nodeset */
		    push->po_type = PIN_OP_PUSH_NODES;
		    push->po_name = pin_namepool_atom(pwp, s, TRUE);
		    break;
		default:
		    pin_cursor_warn(cur,
			"xsl:variable: complex select not supported: %s", s);
		    push->po_type = PIN_OP_COMPLEX_EXPR;
		    cur->poc_complexity |= (1ULL << PIN_OP_COMPLEX_EXPR);
		    break;
		}
		xmlFree(sel);
	    } else {
		/* Body form: use first non-blank text child as literal value */
		const char *text = NULL;
		for (xmlNodePtr gc = child->children; gc; gc = gc->next) {
		    if (gc->type == XML_TEXT_NODE && gc->content
			    && !xmlIsBlankNode(gc)) {
			text = (const char *) gc->content;
			break;
		    }
		}
		push->po_type = PIN_OP_PUSH_STRING;
		push->po_name = pin_namepool_atom(pwp, text ? text : "", TRUE);
	    }

	    pin_op_t *store = pin_op_new(cur, NULL);
	    if (store == NULL) {
		xmlFree(vname);
		return;
	    }
	    store->po_type = PIN_OP_STORE_VAR;
	    store->po_name = pin_namepool_atom(pwp, (const char *) vname, TRUE);
	    xmlFree(vname);
	    continue;
	}

	/*
	 * xsl:param name="x" [select="expr"]
	 *
	 * LOAD_PARAM("x") pushes [value, bool]; IF branches on bool.
	 * If param was passed: STORE_VAR("x") binds value, GOTO skips default.
	 * If not passed: DISCARD null, evaluate default expr, STORE_VAR("x").
	 * JUMP is the join point reached by both paths.
	 *
	 * No-default form: LOAD_PARAM("x") + DISCARD + STORE_VAR("x").
	 */
	if (pin_is_xsl(child, "param")) {
	    xmlChar *pname = xmlGetProp(child, (const xmlChar *) "name");
	    if (pname == NULL || pname[0] == '\0') {
		pin_error(cur->poc_rb, child, "xsl:param: missing or empty name attribute");
		if (pname)
		    xmlFree(pname);
		continue;
	    }
	    pin_name_id_t pnid = pin_namepool_atom(pwp, (const char *) pname, TRUE);
	    xmlChar *psel = xmlGetProp(child, (const xmlChar *) "select");

	    pin_op_t *load = pin_op_new(cur, NULL);
	    if (load == NULL) {
		xmlFree(pname);
		if (psel)
		    xmlFree(psel);
		return;
	    }
	    load->po_type = PIN_OP_LOAD_PARAM;
	    load->po_name = pnid;

	    if (psel && psel[0] != '\0') {
		/* With default: IF + true-branch + GOTO + false-branch + JUMP */
		pin_op_t *bip_bool = pin_op_new(cur, NULL);
		if (bip_bool == NULL) {
		    xmlFree(pname);
		    xmlFree(psel);
		    return;
		}
		bip_bool->po_type = PIN_OP_PUSH_BOOL;

		pin_op_id_t bid_if;
		pin_op_t *bip_if = pin_op_new(cur, &bid_if);
		if (bip_if == NULL) {
		    xmlFree(pname);
		    xmlFree(psel);
		    return;
		}
		bip_if->po_type = PIN_OP_IF;

		/* True branch: param was provided, value is top of stack */
		pin_op_t *store_true = pin_op_new(cur, NULL);
		if (store_true == NULL) {
		    xmlFree(pname);
		    xmlFree(psel);
		    return;
		}
		store_true->po_type = PIN_OP_STORE_VAR;
		store_true->po_name = pnid;

		pin_op_id_t bid_goto;
		pin_op_t *bip_goto = pin_op_new(cur, &bid_goto);
		if (bip_goto == NULL) {
		    xmlFree(pname);
		    xmlFree(psel);
		    return;
		}
		bip_goto->po_type = PIN_OP_GOTO;

		/* False branch: discard null, evaluate default, bind */
		pin_op_t *discard = pin_op_new(cur, NULL);
		if (discard == NULL) {
		    xmlFree(pname);
		    xmlFree(psel);
		    return;
		}
		discard->po_type = PIN_OP_DISCARD;

		/* Compile default select expression */
		const char *s = (const char *) psel;
		pin_op_t *push_def = pin_op_new(cur, NULL);
		if (push_def == NULL) {
		    xmlFree(pname);
		    xmlFree(psel);
		    return;
		}
		switch (pin_classify_select(s)) {
		case SELK_LITERAL: {
		    /* Strip enclosing quotes in-place; psel is freed below */
		    char q = s[0];
		    size_t slen = strlen(s);
		    if (slen >= 2 && psel[slen - 1] == q) {
			psel[slen - 1] = '\0';
			push_def->po_type = PIN_OP_PUSH_STRING;
			push_def->po_name = pin_namepool_atom(pwp, s + 1, TRUE);
		    }
		    break;
		}
		case SELK_ATTR:
		    push_def->po_type = PIN_OP_PUSH_ATTR;
		    push_def->po_name = pin_namepool_atom(pwp, s + 1, TRUE);
		    break;
		case SELK_VAR:
		    push_def->po_type = PIN_OP_LOAD_VAR;
		    push_def->po_name = pin_namepool_atom(pwp, s + 1, TRUE);
		    break;
		case SELK_SELF:
		    push_def->po_type = PIN_OP_PUSH_TEXT;
		    push_def->po_name = pin_namepool_atom(pwp, ".", TRUE);
		    break;
		case SELK_DOWN:
		    push_def->po_type = PIN_OP_PUSH_NODES;
		    push_def->po_name = pin_namepool_atom(pwp, s, TRUE);
		    break;
		default:
		    push_def->po_type = PIN_OP_COMPLEX_EXPR;
		    cur->poc_complexity |= (1ULL << PIN_OP_COMPLEX_EXPR);
		    break;
		}

		pin_op_t *store_def = pin_op_new(cur, NULL);
		if (store_def == NULL) {
		    xmlFree(pname);
		    xmlFree(psel);
		    return;
		}
		store_def->po_type = PIN_OP_STORE_VAR;
		store_def->po_name = pnid;

		pin_op_id_t bid_join;
		pin_op_t *bip_join = pin_op_new(cur, &bid_join);
		if (bip_join == NULL) {
		    xmlFree(pname);
		    xmlFree(psel);
		    return;
		}
		bip_join->po_type = PIN_OP_JUMP;

		/*
		 * Backpatch branches:
		 *   IF.po_alt   → DISCARD (false branch: param not provided)
		 *   GOTO.po_alt → JUMP (join after true branch)
		 * bip_goto->po_next was set when DISCARD was allocated above.
		 */
		(void) bid_if;
		bip_if->po_alt   = bip_goto->po_next;
		bip_goto->po_alt = bid_join;

	    } else {
		/* No default: DISCARD bool, STORE_VAR("x") with whatever was found (or null) */
		pin_op_t *discard = pin_op_new(cur, NULL);
		if (discard == NULL) {
		    xmlFree(pname);
		    if (psel)
			xmlFree(psel);
		    return;
		}
		discard->po_type = PIN_OP_DISCARD;

		pin_op_t *store = pin_op_new(cur, NULL);
		if (store == NULL) {
		    xmlFree(pname);
		    if (psel)
			xmlFree(psel);
		    return;
		}
		store->po_type = PIN_OP_STORE_VAR;
		store->po_name = pnid;
	    }

	    xmlFree(pname);
	    if (psel)
		xmlFree(psel);
	    continue;
	}

	/*
	 * xsl:call-template name="x" — possibly with xsl:with-param children.
	 * Compile each with-param as: [value expr] + WITH_PARAM("name").
	 * Then emit CALL("x") with po_count = number of with-param children.
	 */
	if (pin_is_xsl(child, "call-template")) {
	    xmlChar *tname = xmlGetProp(child, (const xmlChar *) "name");
	    if (tname == NULL || tname[0] == '\0') {
		pin_error(cur->poc_rb, child,
			  "xsl:call-template: missing or empty name attribute");
		if (tname)
		    xmlFree(tname);
		continue;
	    }

	    uint16_t nparam = 0;
	    for (xmlNodePtr wpc = child->children; wpc; wpc = wpc->next) {
		if (!pin_is_xsl(wpc, "with-param"))
		    continue;
		xmlChar *wpname = xmlGetProp(wpc, (const xmlChar *) "name");
		xmlChar *wpsel  = xmlGetProp(wpc, (const xmlChar *) "select");
		if (wpname == NULL || wpname[0] == '\0') {
		    pin_error(cur->poc_rb, wpc,
			      "xsl:with-param: missing or empty name attribute");
		    if (wpname)
			xmlFree(wpname);
		    if (wpsel)
			xmlFree(wpsel);
		    continue;
		}

		/* Compile value expression */
		const char *s = wpsel ? (const char *) wpsel : ".";
		pin_op_t *push = pin_op_new(cur, NULL);
		if (push == NULL) {
		    if (wpname)
			xmlFree(wpname);
		    if (wpsel)
			xmlFree(wpsel);
		    xmlFree(tname);
		    return;
		}
		switch (pin_classify_select(s)) {
		case SELK_LITERAL: {
		    /* Strip enclosing quotes in-place; wpsel is freed below */
		    char q = s[0];
		    size_t slen = strlen(s);
		    if (slen >= 2 && wpsel[slen - 1] == q) {
			wpsel[slen - 1] = '\0';
			push->po_type = PIN_OP_PUSH_STRING;
			push->po_name = pin_namepool_atom(pwp, s + 1, TRUE);
		    }
		    break;
		}
		case SELK_ATTR:
		    push->po_type = PIN_OP_PUSH_ATTR;
		    push->po_name = pin_namepool_atom(pwp, s + 1, TRUE);
		    break;
		case SELK_VAR:
		    push->po_type = PIN_OP_LOAD_VAR;
		    push->po_name = pin_namepool_atom(pwp, s + 1, TRUE);
		    break;
		case SELK_SELF:
		    push->po_type = PIN_OP_PUSH_TEXT;
		    push->po_name = pin_namepool_atom(pwp, ".", TRUE);
		    break;
		case SELK_DOWN:
		    push->po_type = PIN_OP_PUSH_NODES;
		    push->po_name = pin_namepool_atom(pwp, s, TRUE);
		    break;
		default:
		    push->po_type = PIN_OP_COMPLEX_EXPR;
		    cur->poc_complexity |= (1ULL << PIN_OP_COMPLEX_EXPR);
		    break;
		}

		pin_op_t *wp = pin_op_new(cur, NULL);
		if (wp == NULL) {
		    if (wpname)
			xmlFree(wpname);
		    if (wpsel)
			xmlFree(wpsel);
		    xmlFree(tname);
		    return;
		}
		wp->po_type = PIN_OP_WITH_PARAM;
		wp->po_name = pin_namepool_atom(pwp, (const char *) wpname, TRUE);
		nparam++;

		if (wpname)
		    xmlFree(wpname);
		if (wpsel)
		    xmlFree(wpsel);
	    }

	    pin_op_t *call_op = pin_op_new(cur, NULL);
	    if (call_op == NULL) {
		xmlFree(tname);
		return;
	    }
	    call_op->po_type  = PIN_OP_CALL;
	    call_op->po_name  = pin_namepool_atom(pwp, (const char *) tname, TRUE);
	    call_op->po_count = nparam;
	    xmlFree(tname);
	    continue;
	}

	/* xsl:element → [optional push] + ELEMENT_OPEN + body + ELEMENT_CLOSE */
	if (pin_is_xsl(child, "element")) {
	    xmlChar *ename = xmlGetProp(child, (const xmlChar *) "name");
	    if (ename == NULL) {
		pin_warn(child, "xsl:element: missing name attribute");
		continue;
	    }
	    const char *estr = (const char *) ename;
	    pin_name_id_t tag_id = pin_name_id_null_atom();
	    if (estr[0] != '{') {
		/* Literal name: store directly in ELEMENT_OPEN, no push needed */
		tag_id = pin_namepool_atom(pwp, estr, TRUE);
	    } else {
		/* AVT name: emit a push op first, ELEMENT_OPEN pops the name */
		size_t elen = strlen(estr);
		if (elen >= 4 && estr[1] == '$' && estr[elen - 1] == '}') {
		    /* {$varname} — strip braces in-place; ename is freed below */
		    ename[elen - 1] = '\0';
		    pin_op_t *lv = pin_op_new(cur, NULL);
		    if (lv != NULL) {
			lv->po_type = PIN_OP_LOAD_VAR;
			lv->po_name = pin_namepool_atom(pwp, estr + 2, TRUE);
		    }
		} else {
		    /* Other AVT expression: push as literal string */
		    pin_op_t *ps = pin_op_new(cur, NULL);
		    if (ps != NULL) {
			ps->po_type = PIN_OP_PUSH_STRING;
			ps->po_name = pin_namepool_atom(pwp, estr, TRUE);
		    }
		}
		/* tag_id remains null → ELEMENT_OPEN pops name from stack */
	    }
	    pin_op_t *open_op = pin_op_new(cur, NULL);
	    if (open_op == NULL) {
		xmlFree(ename);
		return;
	    }
	    open_op->po_type = PIN_OP_ELEMENT_OPEN;
	    open_op->po_name = tag_id;
	    pin_compile_ops_r(child, cur);
	    pin_op_t *close_op = pin_op_new(cur, NULL);
	    if (close_op == NULL) {
		xmlFree(ename);
		return;
	    }
	    close_op->po_type = PIN_OP_ELEMENT_CLOSE;
	    xmlFree(ename);
	    continue;
	}

	/* xsl:attribute → value expr on stack + EMIT_ATTRIB(name) */
	if (pin_is_xsl(child, "attribute")) {
	    xmlChar *aname = xmlGetProp(child, (const xmlChar *) "name");
	    if (aname == NULL) {
		pin_warn(child, "xsl:attribute: missing name attribute");
		continue;
	    }
	    /* Collect value from children: build PUSH ops then EMIT_ATTRIB */
	    for (xmlNodePtr vc = child->children; vc; vc = vc->next) {
		if (vc->type == XML_TEXT_NODE && vc->content && vc->content[0]) {
		    pin_op_t *ps_op = pin_op_new(cur, NULL);
		    if (ps_op == NULL) {
		xmlFree(aname);
		return;
	    }
		    ps_op->po_type = PIN_OP_PUSH_STRING;
		    ps_op->po_name = pin_namepool_atom(pwp,
						       (const char *) vc->content, TRUE);
		} else if (pin_is_xsl(vc, "value-of")) {
		    xmlChar *sel = xmlGetProp(vc, (const xmlChar *) "select");
		    if (sel) {
			const char *sstr = (const char *) sel;
			pin_op_t *lv_op = pin_op_new(cur, NULL);
			if (lv_op != NULL) {
			    if (sstr[0] == '$') {
				lv_op->po_type = PIN_OP_LOAD_VAR;
				lv_op->po_name = pin_namepool_atom(pwp, sstr + 1, TRUE);
			    } else {
				lv_op->po_type = PIN_OP_PUSH_TEXT;
				lv_op->po_name = pin_namepool_atom(pwp, sstr, TRUE);
			    }
			}
			xmlFree(sel);
		    }
		}
	    }
	    pin_op_t *ea_op = pin_op_new(cur, NULL);
	    if (ea_op == NULL) {
		xmlFree(aname);
		return;
	    }
	    ea_op->po_type = PIN_OP_EMIT_ATTRIB;
	    ea_op->po_name = pin_namepool_atom(pwp, (const char *) aname, TRUE);
	    xmlFree(aname);
	    continue;
	}

	/* xsl:text → PUSH_STRING + EMIT */
	if (pin_is_xsl(child, "text")) {
	    xo_buffer_t tbuf;
	    xo_buf_init(&tbuf);
	    for (xmlNodePtr tc = child->children; tc; tc = tc->next) {
		if (tc->type == XML_TEXT_NODE && tc->content)
		    xo_buf_append_str(&tbuf, (const char *) tc->content);
	    }
	    if (!xo_buf_is_empty(&tbuf)) {
		xo_buf_force_nul(&tbuf);
		pin_op_t *push = pin_op_new(cur, NULL);
		if (push == NULL) {
		    xo_buf_cleanup(&tbuf);
		    return;
		}
		push->po_type = PIN_OP_PUSH_STRING;
		push->po_name = pin_namepool_atom(pwp, xo_buf_data(&tbuf, 0), TRUE);

		pin_op_t *emit = pin_op_new(cur, NULL);
		if (emit == NULL) {
		    xo_buf_cleanup(&tbuf);
		    return;
		}
		emit->po_type = PIN_OP_EMIT;
	    }
	    xo_buf_cleanup(&tbuf);
	    continue;
	}

	/* xsl:copy → PIN_OP_COPY_OPEN + children + PIN_OP_ELEMENT_CLOSE */
	if (pin_is_xsl(child, "copy")) {
	    pin_op_t *op = pin_op_new(cur, NULL);
	    if (op == NULL)
		return;
	    op->po_type = PIN_OP_COPY_OPEN;
	    pin_compile_ops_r(child, cur);
	    pin_op_t *cop = pin_op_new(cur, NULL);
	    if (cop == NULL)
		return;
	    cop->po_type = PIN_OP_ELEMENT_CLOSE;
	    continue;
	}

	/* xsl:message → PIN_OP_MESSAGE_OPEN + children + PIN_OP_MESSAGE_CLOSE */
	if (pin_is_xsl(child, "message")) {
	    xmlChar *term = xmlGetProp(child, (const xmlChar *) "terminate");
	    pin_op_t *op = pin_op_new(cur, NULL);
	    if (op != NULL)
		op->po_type = PIN_OP_MESSAGE_OPEN;
	    pin_compile_ops_r(child, cur);
	    pin_op_t *cop = pin_op_new(cur, NULL);
	    if (cop != NULL) {
		cop->po_type = PIN_OP_MESSAGE_CLOSE;
		if (term && xmlStrcmp(term, (const xmlChar *) "yes") == 0)
		    cop->po_name = pin_namepool_atom(pwp, "yes", TRUE);
	    }
	    if (term)
		xmlFree(term);
	    continue;
	}

	/* xsl:comment → PIN_OP_COMMENT_OPEN + children + PIN_OP_COMMENT_CLOSE */
	if (pin_is_xsl(child, "comment")) {
	    pin_op_t *op = pin_op_new(cur, NULL);
	    if (op != NULL)
		op->po_type = PIN_OP_COMMENT_OPEN;
	    pin_compile_ops_r(child, cur);
	    pin_op_t *cop = pin_op_new(cur, NULL);
	    if (cop != NULL)
		cop->po_type = PIN_OP_COMMENT_CLOSE;
	    continue;
	}

	/* xsl:processing-instruction → PIN_OP_PI_OPEN + children + PIN_OP_PI_CLOSE */
	if (pin_is_xsl(child, "processing-instruction")) {
	    xmlChar *piname = xmlGetProp(child, (const xmlChar *) "name");
	    pin_op_t *op = pin_op_new(cur, NULL);
	    if (op != NULL) {
		op->po_type = PIN_OP_PI_OPEN;
		if (piname && piname[0])
		    op->po_name = pin_namepool_atom(pwp,
						    (const char *) piname, TRUE);
	    }
	    if (piname)
		xmlFree(piname);
	    pin_compile_ops_r(child, cur);
	    pin_op_t *cop = pin_op_new(cur, NULL);
	    if (cop != NULL)
		cop->po_type = PIN_OP_PI_CLOSE;
	    continue;
	}

	/* xsl:number → PIN_OP_NUMBER */
	if (pin_is_xsl(child, "number")) {
	    xmlChar *val = xmlGetProp(child, (const xmlChar *) "value");
	    xmlChar *fmt = xmlGetProp(child, (const xmlChar *) "format");
	    if (val && val[0]) {
		pin_op_t *op = pin_op_new(cur, NULL);
		if (op != NULL) {
		    op->po_type = PIN_OP_NUMBER;
		    op->po_name = pin_namepool_atom(pwp,
						    (const char *) val, TRUE);
		    if (fmt && fmt[0])
			op->po_name2 = pin_namepool_atom(pwp,
							 (const char *) fmt, TRUE);
		}
	    }
	    if (val)
		xmlFree(val);
	    if (fmt)
		xmlFree(fmt);
	    continue;
	}

	/* Other xsl:* instructions not yet handled */
	if (pin_is_xsl(child, NULL)) {
	    pin_warn(child, "xsl:%s: element not supported", child->name);
	    continue;
	}

	/* Literal element: EMIT_OPEN, recurse into children, EMIT_CLOSE */
	{
	    pin_name_id_t tag_id = pin_namepool_atom(pwp,
		    (const char *) child->name, TRUE);

	    pin_op_t *open_op = pin_op_new(cur, NULL);
	    if (open_op == NULL)
		return;
	    open_op->po_type = PIN_OP_EMIT_OPEN;
	    open_op->po_name = tag_id;

	    /*
	     * Capture attributes.  Static attrs go in open_op->po_name2.
	     * AVT attrs become PUSH_AVT + EMIT_ATTRIB op pairs after EMIT_OPEN.
	     * po_count is set to 1 on EMIT_OPEN when any AVT attrs follow so
	     * that pin_op_emit_open forces PIA_SAVE_ATTRIB mode.
	     */
	    if (child->properties) {
		xo_buffer_t abuf;
		xo_buf_init(&abuf);
		bool has_avt = false;
		for (xmlAttrPtr ap = child->properties; ap; ap = ap->next) {
		    const char *aval = (ap->children && ap->children->content)
			? (const char *) ap->children->content : "";
		    if (strchr(aval, '{')) {
			has_avt = true;
			continue;
		    }
		    const char *aname = (const char *) ap->name;
		    if (!xo_buf_is_empty(&abuf))
			xo_buf_append(&abuf, " ", 1);
		    xo_buf_append_str(&abuf, aname);
		    xo_buf_append(&abuf, "=\"", 2);
		    xo_buf_append_str(&abuf, aval);
		    xo_buf_append(&abuf, "\"", 1);
		}
		if (!xo_buf_is_empty(&abuf)) {
		    xo_buf_force_nul(&abuf);
		    open_op->po_name2 = pin_namepool_atom(pwp,
						 xo_buf_data(&abuf, 0), TRUE);
		}
		if (has_avt)
		    open_op->po_count = 1;
		xo_buf_cleanup(&abuf);

		/* Emit PUSH_AVT + EMIT_ATTRIB for each AVT attribute */
		if (has_avt) {
		    for (xmlAttrPtr ap = child->properties; ap; ap = ap->next) {
			const char *aval = (ap->children && ap->children->content)
			    ? (const char *) ap->children->content : "";
			if (!strchr(aval, '{'))
			    continue;
			pin_name_id_t avt_id = pin_namepool_atom(pwp, aval, TRUE);
			pin_name_id_t aname_id = pin_namepool_atom(pwp,
							(const char *) ap->name, TRUE);

			pin_op_t *push_op = pin_op_new(cur, NULL);
			if (push_op == NULL)
			    return;
			push_op->po_type = PIN_OP_PUSH_AVT;
			push_op->po_name2 = avt_id;

			pin_op_t *attr_op = pin_op_new(cur, NULL);
			if (attr_op == NULL)
			    return;
			attr_op->po_type = PIN_OP_EMIT_ATTRIB;
			attr_op->po_name = aname_id;
		    }
		}
	    }

	    pin_compile_ops_r(child, cur);

	    pin_op_t *close_op = pin_op_new(cur, NULL);
	    if (close_op == NULL)
		return;
	    close_op->po_type = PIN_OP_EMIT_CLOSE;
	    close_op->po_name = tag_id;
	}
    }
}

/*
 * Compile the template body rooted at body_node into a PIN_OP_* sequence
 * in the rulebook's prb_ops pool.
 * Returns the id of the first op, or the null id for an empty body.
 */
static pin_op_id_t
pin_compile_ops (xmlNodePtr body_node, pin_rulebook_t *rb,
		      pin_name_id_t src_file_id, uint64_t *complexity_out)
{
    pin_op_id_t head = pin_op_id_null_atom();
    pin_op_cursor_t cur = {
	.poc_nextp = &head,
	.poc_rb = rb,
	.poc_src_file = src_file_id,
	.poc_src_line = 0,
	.poc_complexity = 0,
    };
    pin_compile_ops_r(body_node, &cur);
    if (complexity_out)
	*complexity_out = cur.poc_complexity;
    return head;
}

/*
 * Compute the default priority for a single (already-split) match pattern.
 * Rules from XSLT spec §5.5, applied in order:
 *   No special chars         -> QName or NCName -> 0.0
 *   Exactly "*"              -> -0.5
 *   NCName:*                 -> -0.25
 *   text(), comment(), etc.  -> -0.5
 *   Everything else          -> 0.5
 */
static float
pin_default_priority (const char *match_str)
{
    /* Bare QName or NCName: no special characters at all */
    if (strpbrk(match_str, "/@[]*.:|(") == NULL)
	return 0.0f;

    /* Exactly "*" */
    if (strcmp(match_str, "*") == 0)
	return -0.5f;

    /* NCName:* — one colon, ends with ":*", no other special chars before colon */
    const char *colon = strchr(match_str, ':');
    if (colon && strcmp(colon, ":*") == 0) {
	size_t pre = (size_t)(colon - match_str);
	if (pre > 0 && strpbrk(match_str, "/@[]*.(|") == NULL)
	    return -0.25f;
    }

    /* Node-type tests */
    if (strncmp(match_str, "text(", 5) == 0
	    || strncmp(match_str, "comment(", 8) == 0
	    || strncmp(match_str, "node(", 5) == 0
	    || strncmp(match_str, "processing-instruction(", 23) == 0)
	return -0.5f;

    return 0.5f;
}

/*
 * Register one (already-split, trimmed) match token with the apply-templates
 * dispatch list.  Only tokens that are bare element names (no path, predicate,
 * wildcard, or function syntax) are registered; everything else is silently
 * skipped (the filter handles it for streaming dispatch).
 */
static void
pin_compile_apply_add_split (pin_rulebook_t *rb, const char *match_str,
			     pin_name_id_t mode_id, pin_rule_id_t rid,
			     float base_priority, int16_t import_prec)
{
    char *buf = strdup(match_str);
    if (buf == NULL)
	return;

    char *tok = buf;
    int depth = 0;
    char *start = tok;

    for (;; tok++) {
	char c = *tok;
	if (c == '[')
	    depth += 1;
	else if (c == ']' && depth > 0)
	    depth -= 1;
	else if ((c == '|' && depth == 0) || c == '\0') {
	    char saved = *tok;
	    *tok = '\0';

	    /* Trim leading whitespace */
	    while (*start == ' ' || *start == '\t')
		start += 1;
	    /* Trim trailing whitespace */
	    char *end = tok - 1;
	    while (end > start && (*end == ' ' || *end == '\t'))
		*end-- = '\0';

	    if (*start != '\0' && strpbrk(start, "/@[]*.:|(") == NULL) {
		float pri = base_priority;
		if (isnan((double) pri))
		    pri = pin_default_priority(start);
		pin_name_id_t match_id = pin_namepool_atom(rb->prb_workspace,
							   start, TRUE);
		if (!pin_name_id_is_null(match_id))
		    pin_rulebook_apply_add(rb, match_id, mode_id, rid,
					  pri, import_prec);
	    }

	    if (saved == '\0')
		break;
	    start = tok + 1;
	}
    }

    free(buf);
}

/*
 * Compile a top-level xsl:variable or xsl:param node into a global binding
 * in the rulebook.  Supported value forms: string literal ('x' or "x"),
 * bare integer, or text body content.  Complex expressions store an empty
 * value and evaluate to "" at runtime.
 * Returns 0 (no count contribution, never a fatal error).
 */
static int
pin_compile_global_var (xmlNodePtr child, pin_rulebook_t *rb)
{
    xmlChar *vname = xmlGetProp(child, (const xmlChar *) "name");
    if (vname == NULL || vname[0] == '\0') {
	if (vname)
	    xmlFree(vname);
	return 0;
    }

    pin_name_id_t nid = pin_namepool_atom(rb->prb_workspace,
					  (const char *) vname, TRUE);
    pin_name_id_t vid = pin_name_id_null_atom();
    xmlChar *vsel = xmlGetProp(child, (const xmlChar *) "select");
    if (vsel && vsel[0]) {
	const char *s = (const char *) vsel;
	size_t slen = strlen(s);
	/* String literal: 'value' or "value" */
	if (slen >= 2
		&& ((s[0] == '\'' && s[slen - 1] == '\'')
		    || (s[0] == '"' && s[slen - 1] == '"'))) {
	    char *inner = strndup(s + 1, slen - 2);
	    if (inner) {
		vid = pin_namepool_atom(rb->prb_workspace, inner, TRUE);
		free(inner);
	    }
	} else {
	    /* Numeric literal (optional leading '-') */
	    const char *p = s;
	    if (*p == '-')
		p += 1;
	    int is_num = (*p != '\0');
	    for (; *p; p++) {
		if (*p < '0' || *p > '9') {
		    is_num = 0;
		    break;
		}
	    }
	    if (is_num)
		vid = pin_namepool_atom(rb->prb_workspace, s, TRUE);
	}
    } else {
	/* Text body: <xsl:variable name="x">value</xsl:variable> */
	xmlChar *content = xmlNodeGetContent(child);
	if (content && content[0])
	    vid = pin_namepool_atom(rb->prb_workspace,
				   (const char *) content, TRUE);
	if (content)
	    xmlFree(content);
    }
    if (vsel)
	xmlFree(vsel);
    if (!pin_name_id_is_null(nid))
	pin_rulebook_global_add(rb, nid, vid);
    xmlFree(vname);
    return 0;
}

/*
 * Compile one xsl:template node (either named or match-based).
 * Returns 1 if a rule or named template was registered, 0 if the template
 * was skipped or rejected without error, or -1 on a fatal error.
 */
static int
pin_compile_template (xmlNodePtr child, xmlDocPtr docp, xo_filter_t *xfp,
		      pin_rulebook_t *rb, pin_action_type_t action,
		      int16_t import_prec)
{
    const char *url = docp->URL ? (const char *) docp->URL : "(unknown)";
    pin_name_id_t src_file_id = pin_namepool_atom(rb->prb_workspace,
						  url, TRUE);
    xmlChar *match = xmlGetProp(child, (const xmlChar *) "match");
    if (match == NULL) {
	/* Named template (name= but no match=): compile body as op sequence */
	xmlChar *tname = xmlGetProp(child, (const xmlChar *) "name");
	if (tname == NULL || tname[0] == '\0') {
	    pin_error(rb, child,
		      "xsl:template: missing or empty name attribute");
	    if (tname)
		xmlFree(tname);
	    return 0;
	}
	uint64_t complexity = 0;
	pin_op_id_t ops = pin_compile_ops(child, rb, src_file_id, &complexity);
	int count = 0;
	if (!pin_op_id_is_null(ops) && complexity == 0) {
	    pin_name_id_t nid = pin_namepool_atom(rb->prb_workspace,
		    (const char *) tname, TRUE);
	    pin_rulebook_named_add(rb, nid, ops);
	    count = 1;
	}
	xmlFree(tname);
	return count;
    }

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
	psu_log("pin_compile: pin_rule_alloc failed");
	xmlFree(match);
	return -1;
    }

    bzero(prp, sizeof(*prp));
    prp->pr_mode = mode_id;
    prp->pr_src_file = src_file_id;
    prp->pr_src_line = (uint32_t) xmlGetLineNo(child);

    /*
     * Compile to PIN_OP_* sequences first.  If all ops are simple (no
     * apply-templates, no copy-of), op-dispatch executes the full body
     * without BIA_.  Complex ops set bits in complexity, triggering BIA_.
     */
    uint64_t complexity = 0;
    prp->pr_close_ops = pin_compile_ops(child, rb, src_file_id, &complexity);

    if (complexity != 0 || pin_op_id_is_null(prp->pr_close_ops)) {
	pin_rstate_id_t foreach_sid = pin_compile_foreach(child, rb);
	if (!pin_rstate_id_is_null(foreach_sid)) {
	    prp->pr_action = PIA_SAVE;
	    prp->pr_new_state = foreach_sid;
	    prp->pr_close_ops = pin_op_id_null_atom();
	} else {
	    pin_body_instr_id_t body_head = pin_compile_body(child, rb);
	    if (!pin_body_instr_id_is_null(body_head)) {
		prp->pr_body = body_head;
		prp->pr_body_retain = pin_body_retain(body_head, rb);
	    } else {
		prp->pr_action = action;
	    }
	}
    }

    /*
     * Read explicit priority= or compute the default from the pattern.
     * For | alternatives, each token gets its own default, but a single
     * explicit priority= applies uniformly to all alternatives.
     * NAN signals "compute per token" in pin_compile_apply_add_split.
     */
    xmlChar *tpri = xmlGetProp(child, (const xmlChar *) "priority");
    float priority;
    const char *match_str = (const char *) match;
    if (tpri && tpri[0])
	priority = (float) atof((const char *) tpri);
    else
	priority = pin_default_priority(match_str);
    if (tpri)
	xmlFree(tpri);

    /*
     * Register the rule with the filter.
     * match="/" is special: the document root fires no element event, so
     * store the rule id in prb_root_rule and fire it manually in pin_parse.
     */
    if (match_str[0] == '/' && match_str[1] == '\0') {
	rb->prb_root_rule = rid;
	xmlFree(match);
	return 1;
    }

    int rc = pin_filter_add_with_action(xfp, match_str, rid);
    if (rc < 0) {
	pin_error(rb, child, "xsl:template: unsupported match pattern: %s",
		  match_str);
	xmlFree(match);
	return -1;
    }

    /*
     * Register in the apply-templates dispatch list, splitting | alternatives.
     * Each simple name token is registered separately with its priority.
     */
    pin_compile_apply_add_split(rb, match_str, mode_id, rid,
				priority, import_prec);

    xmlFree(match);
    return 1;
}

/*
 * Compile an xsl:include: load the referenced document and compile it at
 * the same import precedence as the including stylesheet.
 */
static int
pin_compile_include (xmlNodePtr child, xmlDocPtr docp, xo_filter_t *xfp,
		     pin_rulebook_t *rb, pin_action_type_t action,
		     int16_t import_prec)
{
    xmlChar *href = xmlGetProp(child, (const xmlChar *) "href");
    if (href == NULL)
	return 0;
    xmlChar *url = xmlBuildURI(href, docp->URL);
    xmlFree(href);
    if (url == NULL)
	return 0;
    xmlDocPtr inc = xmlReadFile((const char *) url, NULL,
				XML_PARSE_NOENT | XML_PARSE_NONET);
    xmlFree(url);
    if (inc == NULL)
	return 0;
    int count = pin_compile(inc, xfp, rb, action, import_prec);
    xmlFreeDoc(inc);
    return (count > 0) ? count : 0;
}

/*
 * Compile an xsl:import: load the referenced document and compile it at
 * one lower import precedence than the importing stylesheet.
 */
static int
pin_compile_import (xmlNodePtr child, xmlDocPtr docp, xo_filter_t *xfp,
		    pin_rulebook_t *rb, pin_action_type_t action,
		    int16_t import_prec)
{
    xmlChar *href = xmlGetProp(child, (const xmlChar *) "href");
    if (href == NULL)
	return 0;
    xmlChar *url = xmlBuildURI(href, docp->URL);
    xmlFree(href);
    if (url == NULL)
	return 0;
    xmlDocPtr imp = xmlReadFile((const char *) url, NULL,
				XML_PARSE_NOENT | XML_PARSE_NONET);
    xmlFree(url);
    if (imp == NULL)
	return 0;
    int count = pin_compile(imp, xfp, rb, action, (int16_t)(import_prec - 1));
    xmlFreeDoc(imp);
    return (count > 0) ? count : 0;
}

int
pin_compile (xmlDocPtr docp, xo_filter_t *xfp, pin_rulebook_t *rb,
		  pin_action_type_t action, int16_t import_prec)
{
    if (docp == NULL || xfp == NULL || rb == NULL)
	return -1;

    xmlNodePtr root = xmlDocGetRootElement(docp);
    if (!pin_is_xsl(root, NULL)) {
	psu_error(docp->URL ? (const char *) docp->URL : NULL, 0,
		  "document root is not an xsl:stylesheet element");
	return -1;
    }

    int count = 0;

    for (xmlNodePtr child = root->children; child; child = child->next) {
	int rc;
	if (pin_is_xsl(child, "variable") || pin_is_xsl(child, "param"))
	    rc = pin_compile_global_var(child, rb);
	else if (pin_is_xsl(child, "template"))
	    rc = pin_compile_template(child, docp, xfp, rb, action, import_prec);
	else if (pin_is_xsl(child, "include"))
	    rc = pin_compile_include(child, docp, xfp, rb, action, import_prec);
	else if (pin_is_xsl(child, "import"))
	    rc = pin_compile_import(child, docp, xfp, rb, action, import_prec);
	else
	    continue;
	if (rc < 0)
	    return -1;
	count += rc;
    }

    if (rb->prb_workspace->pw_errors)
	return -1;
    return count;
}

int
pin_for_each_mode (xmlDocPtr docp, pin_mode_fn fn, void *opaque)
{
    xmlNodePtr root = xmlDocGetRootElement(docp);
    if (!pin_is_xsl(root, NULL))
	return -1;

    struct seen_mode_s { xmlChar *mode; int count; };
    struct seen_mode_s *seen = NULL;
    int nseen = 0, seen_size = 0;
    int default_count = 0;

    for (xmlNodePtr child = root->children; child; child = child->next) {
	if (!pin_is_xsl(child, "template"))
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

	if (!found) {
	    if (nseen >= seen_size) {
		int newsize = seen_size ? seen_size * 2 : 4;
		struct seen_mode_s *ns = xo_realloc(seen,
					 newsize * sizeof(*seen));
		if (ns == NULL) {
		    xmlFree(tmode);
		    continue;
		}
		seen = ns;
		seen_size = newsize;
	    }
	    seen[nseen].mode = tmode;   /* retained for dedup; freed below */
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
    xo_free(seen);

    return total;
}
