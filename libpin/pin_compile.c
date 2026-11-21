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
 *   - variables and parameters (item 14)
 *   - template body expression language (item 17)
 *   - apply-templates body / frame stack (item 16)
 *
 * named templates / call-template (item 15) are handled by both compilers:
 * pin_compile_template()'s named branch, xsl:call-template/xsl:with-param
 * handling in pin_compile_ops_r() (op-dispatch), and the BIA_WITH_PARAM/
 * BIA_CALL branch in pin_compile_body_r() (body-FSM; for-each sub-bodies
 * only -- see build/plans/call-template-real.rst).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <math.h>
#include <sys/param.h>

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
#include <libxo/xo_buf.h>
#include <libxo/xo_filter.h>

#include <libpin/pin_common.h>
#include <libpin/pin_workspace.h>
#include <libpin/pin_rules.h>
#include <libpin/pin_filter.h>
#include <libpin/pin_compile.h>

/* Standard XSLT namespace URI */
#define PIN_XSL_URI "http://www.w3.org/1999/XSL/Transform"
#define PIN_SLAX_URI "http://xml.libslax.org/slax"

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
 * Return non-zero if nodep is the element "name" in the SLAX
 * namespace, which is how SLAX statements with no XSLT equivalent
 * (mvar append and set) reach us.
 */
static int
pin_is_slax (xmlNodePtr nodep, const char *name)
{
    if (nodep == NULL || nodep->type != XML_ELEMENT_NODE)
	return 0;

    return nodep->ns && nodep->ns->href
	&& strcmp((const char *) nodep->name, name) == 0
	&& strcmp((const char *) nodep->ns->href, PIN_SLAX_URI) == 0;
}

/* Return non-zero if nodep has an element child */
static int
pin_has_element_child (xmlNodePtr nodep)
{
    for (xmlNodePtr cp = nodep->children; cp; cp = cp->next) {
	if (cp->type == XML_ELEMENT_NODE)
	    return 1;
    }

    return 0;
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
 * Is s[0..len) a step path the sort key evaluator can walk: ".", a
 * child path ("a", "a/b", "./a"), or (when allow_attr) "@name"?  An
 * attribute followed by steps ("@name/last") is accepted: it is empty
 * for every item, in XPath and in pin_for_each_eval_expr() alike.
 */
static bool
pin_sort_path_ok (const char *s, size_t len, bool allow_attr)
{
    if (len == 0 || s[0] == '/')
	return false;

    size_t i = 0;
    if (s[0] == '@') {
	if (!allow_attr || len == 1)
	    return false;
	i = 1;
    }

    for (size_t step = i; i < len; i++) {
	unsigned char c = (unsigned char) s[i];
	/* A step starts like an XML name, or is "." */
	if (i == step && !isalpha(c) && c != '_' && c != '.')
	    return false;
	if (c == '/')
	    step = i + 1;
	if (!isalnum(c) && c != '_' && c != '-' && c != '.' && c != '/'
	        && c != ':')
	    return false;
	if (i + 1 < len && ((c == '/' && s[i + 1] == '/')
			    || (c == '.' && s[i + 1] == '.')
			    || (c == ':' && s[i + 1] == ':')
			    || (c == '/' && s[i + 1] == '@')))
	    return false;
    }

    return true;
}

/*
 * Can pin_for_each_eval_expr() evaluate this sort key?  It handles
 * ".", "@attr", a child path, and "path OP path" arithmetic (OP one of
 * + - * / with single spaces).  A string or number literal is a
 * constant key, which ties every item and so keeps document order,
 * as XSLT does.  Anything else (predicates, function calls, "..",
 * "//", axes, unions, variables, other operators) would silently
 * evaluate to an empty key for every item.
 */
static bool
pin_sort_key_supported (const char *k)
{
    size_t len = strlen(k);

    if (len >= 2 && (k[0] == '\'' || k[0] == '"')
	    && k[len - 1] == k[0] && memchr(k + 1, k[0], len - 2) == NULL)
	return true;

    if (len > 0 && strspn(k, "0123456789.") == len)
	return true;

    if (strpbrk(k, " \t\r\n") == NULL)
	return pin_sort_path_ok(k, len, true);

    /* Arithmetic: split on " OP "; every operand must be a child path */
    const char *start = k;
    for (const char *p = k; *p; p++) {
	if (p[0] == ' ' && p[1] && strchr("+-*/", p[1]) && p[2] == ' ') {
	    if (!pin_sort_path_ok(start, (size_t) (p - start), false))
		return false;
	    start = p + 3;
	    p += 2;
	}
    }

    return start != k && pin_sort_path_ok(start, strlen(start), false);
}

/*
 * Build the multi-key sort spec for an xsl:for-each from its xsl:sort
 * children, for both the BIA_ and op-dispatch compilers.  Keys are
 * newline-separated; each starts with optional flags, in this order:
 * '-' (order="descending"), '^' (case-order="upper-first") and '#'
 * (data-type="number"), followed by the key expression ("." when
 * select is absent).  An unknown data-type is a compile error when
 * report is set; otherwise it just makes the spec non-simple, so a
 * match template falls back to BIA_, which reports it once.  A key
 * that pin_sort_key_supported() rejects is reported the same way.
 * Returns false if any key expression has a predicate, function call
 * or wildcard, which op dispatch can't evaluate; the spec is still
 * built in full.
 */
static bool
pin_compile_sort_spec (pin_rulebook_t *rb, xmlNodePtr for_each,
                       xo_buffer_t *specp, bool report)
{
    bool simple = true;

    for (xmlNodePtr sp = for_each->children; sp; sp = sp->next) {
	if (!pin_is_xsl(sp, "sort"))
	    continue;

	xmlChar *order  = xmlGetProp(sp, (const xmlChar *) "order");
	xmlChar *corder = xmlGetProp(sp, (const xmlChar *) "case-order");
	xmlChar *dtype  = xmlGetProp(sp, (const xmlChar *) "data-type");
	xmlChar *kexpr  = xmlGetProp(sp, (const xmlChar *) "select");

	const char *kstr = kexpr ? (const char *) kexpr : ".";
	bool supported = pin_sort_key_supported(kstr);
	if (!supported || strpbrk(kstr, "[()*") != NULL)
	    simple = false;

	/*
	 * Both engines share one key evaluator (pin_for_each_eval_expr);
	 * a form it doesn't handle would come out empty for every item
	 * and the sort would silently be document order.
	 */
	if (report && !supported)
	    pin_error(rb, sp, "xsl:sort: select \"%s\": this sort key form "
		      "is not supported yet (only '.', '@attr', a child path, "
		      "or arithmetic on child paths)", kstr);

	bool desc   = order  && strcmp((char *) order,  "descending") == 0;
	bool upper  = corder && strcmp((char *) corder, "upper-first") == 0;
	bool number = false;
	if (dtype) {
	    if (strcmp((char *) dtype, "number") == 0)
		number = true;
	    else if (strcmp((char *) dtype, "text") != 0) {
		if (report)
		    pin_error(rb, sp, "xsl:sort: unsupported data-type '%s'",
			      (char *) dtype);
		else
		    simple = false;
	    }
	}

	if (!xo_buf_is_empty(specp))
	    xo_buf_append(specp, "\n", 1);
	if (desc)
	    xo_buf_append(specp, "-", 1);
	if (upper)
	    xo_buf_append(specp, "^", 1);
	if (number)
	    xo_buf_append(specp, "#", 1);
	xo_buf_append_str(specp, kstr);

	if (order)
	    xmlFree(order);
	if (corder)
	    xmlFree(corder);
	if (dtype)
	    xmlFree(dtype);
	if (kexpr)
	    xmlFree(kexpr);
    }

    return simple;
}

/*
 * Compile the children of body_node into a linked list of body instructions.
 * *nextp is a pointer-to-pointer cursor that always points at the id field
 * where the next instruction's id should be written; recursive calls share
 * the same cursor so instructions are appended in source order.
 */
static void
pin_compile_body_r (xmlNodePtr body_node, pin_rulebook_t *rb,
		      pin_body_instr_id_t **nextp, int in_foreach)
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

	/* xsl:copy-of -> BIA_COPY (streaming copy of matched input element) */
	if (pin_is_xsl(child, "copy-of")) {
	    pin_body_instr_t *bip = pin_body_instr_new(rb, nextp, NULL);
	    if (bip == NULL)
		return;
	    bip->bi_type = BIA_COPY;
	    continue;
	}

	/* xsl:value-of -> BIA_VALUE_OF (emit text content or attribute value) */
	if (pin_is_xsl(child, "value-of")) {
	    xmlChar *sel = xmlGetProp(child, (const xmlChar *) "select");
	    const char *sstr = sel ? (const char *) sel : NULL;
	    if (sstr == NULL || sstr[0] == '\0' || strcmp(sstr, ".") == 0) {
		/* select="." -- pause and collect text children */
		pin_body_instr_t *bip = pin_body_instr_new(rb, nextp, NULL);
		if (bip != NULL)
		    bip->bi_type = BIA_VALUE_OF;
	    } else if (sstr[0] == '@' && sstr[1] != '\0'
		       && strpbrk(sstr + 1, "/@[]()*") == NULL) {
		/* select="@attr" -- emit attribute value synchronously */
		pin_body_instr_t *bip = pin_body_instr_new(rb, nextp, NULL);
		if (bip != NULL) {
		    bip->bi_type = BIA_VALUE_OF;
		    bip->bi_select = pin_namepool_atom(rb->prb_workspace,
						       sstr + 1, TRUE);
		}
	    } else if (sstr[0] == '\'' || sstr[0] == '"') {
		/* select="'literal'" or select='"literal"' -- static text */
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
	 * xsl:if -> BIA_IF + BIA_JUMP join point.
	 *
	 * Compiled layout in the instruction list:
	 *   [BIA_IF] bi_next->[true-body...] bi_else->[BIA_JUMP]
	 *   [true-body instructions, last bi_next->BIA_JUMP]
	 *   [BIA_JUMP] bi_next->[first after-if instruction]
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
		 * is `*[condition]` -- wildcard so the filter fires regardless
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
	    pin_compile_body_r(child, rb, nextp, in_foreach);

	    /* Create BIA_JUMP join point after the true-body */
	    pin_body_instr_id_t bid_jump;
	    pin_body_instr_t *bip_jump =
		pin_body_instr_new(rb, nextp, &bid_jump);
	    if (bip_jump == NULL)
		return;
	    bip_jump->bi_type = BIA_JUMP;

	    /* Wire the false branch: BIA_IF.bi_else -> BIA_JUMP */
	    bip_if->bi_else = bid_jump;
	    continue;
	}

	/* xsl:apply-templates -> BIA_APPLY (dispatch children through rules) */
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
	 * xsl:choose -> chain of BIA_IF (one per xsl:when) + optional
	 * BIA_JUMP else-entry + BIA_JUMP join point.
	 *
	 * Each xsl:when produces:
	 *   BIA_IF(cond) -> when_body -> BIA_GOTO(->join)
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

		pin_compile_body_r(wc, rb, nextp, in_foreach);

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
		pin_compile_body_r(otherwise_node, rb, nextp, in_foreach);
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

	/* xsl:variable -> BIA_VARIABLE */
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
	 * bi_text   = multi-key sort spec (see pin_compile_sort_spec())
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
	    pin_compile_sort_spec(rb, child, &sortbuf, true);

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

	    /*
	     * The template's one reader, streaming as items arrive: its
	     * body runs as ops (compiled by the caller), so there's no
	     * BIA_ sub-list to build.
	     */
	    if (child == rb->prb_stream_fe) {
		bip_fe->bi_filter_idx
		    = pa_fixed_atom_of(pin_op_id_atom_of(rb->prb_stream_ops));
		xo_buf_cleanup(&sortbuf);
		xmlFree(fsel);
		continue;
	    }
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
	    pin_compile_body_r(child, rb, &body_nextp, 1);
	    bip_fe->bi_else = body_head;
	    continue;
	}

	/* xsl:element -> BIA_ELEMENT_OPEN + children + BIA_ELEMENT_CLOSE */
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
	    pin_compile_body_r(child, rb, nextp, in_foreach);
	    pin_body_instr_t *cbip = pin_body_instr_new(rb, nextp, NULL);
	    if (cbip == NULL)
		return;
	    cbip->bi_type = BIA_ELEMENT_CLOSE;
	    continue;
	}

	/* xsl:attribute -> BIA_ATTRIB: bi_tag=name, bi_text=value-AVT */
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

	/* xsl:text -> BIA_EMIT_TEXT (preserves whitespace; no xsl:text child elements) */
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

	/* xsl:copy -> BIA_COPY_OPEN + children + BIA_ELEMENT_CLOSE */
	if (pin_is_xsl(child, "copy")) {
	    pin_body_instr_t *bip = pin_body_instr_new(rb, nextp, NULL);
	    if (bip == NULL)
		continue;
	    bip->bi_type = BIA_COPY_OPEN;
	    pin_compile_body_r(child, rb, nextp, in_foreach);
	    pin_body_instr_t *cbip = pin_body_instr_new(rb, nextp, NULL);
	    if (cbip == NULL)
		continue;
	    cbip->bi_type = BIA_ELEMENT_CLOSE;
	    continue;
	}

	/* xsl:message -> BIA_MESSAGE_OPEN + children + BIA_MESSAGE_CLOSE */
	if (pin_is_xsl(child, "message")) {
	    xmlChar *term = xmlGetProp(child, (const xmlChar *) "terminate");
	    pin_body_instr_t *bip = pin_body_instr_new(rb, nextp, NULL);
	    if (bip != NULL)
		bip->bi_type = BIA_MESSAGE_OPEN;
	    pin_compile_body_r(child, rb, nextp, in_foreach);
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

	/* xsl:comment -> BIA_COMMENT_OPEN + children + BIA_COMMENT_CLOSE */
	if (pin_is_xsl(child, "comment")) {
	    pin_body_instr_t *bip = pin_body_instr_new(rb, nextp, NULL);
	    if (bip != NULL)
		bip->bi_type = BIA_COMMENT_OPEN;
	    pin_compile_body_r(child, rb, nextp, in_foreach);
	    pin_body_instr_t *cbip = pin_body_instr_new(rb, nextp, NULL);
	    if (cbip != NULL)
		cbip->bi_type = BIA_COMMENT_CLOSE;
	    continue;
	}

	/* xsl:processing-instruction -> BIA_PI_OPEN + children + BIA_PI_CLOSE */
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
	    pin_compile_body_r(child, rb, nextp, in_foreach);
	    pin_body_instr_t *cbip = pin_body_instr_new(rb, nextp, NULL);
	    if (cbip != NULL)
		cbip->bi_type = BIA_PI_CLOSE;
	    continue;
	}

	/* xsl:number -> BIA_NUMBER */
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

	/*
	 * xsl:call-template name="x" -- possibly with xsl:with-param children.
	 * Compile each with-param as BIA_WITH_PARAM(name, raw value-expr text),
	 * then BIA_CALL(name, with-param count); bi_select holds the raw
	 * expression text (evaluated at runtime via pin_exec_eval_expr_string)
	 * rather than classifying it at compile time, per Open Question 3 in
	 * build/plans/call-template-real.rst.
	 *
	 * Only supported inside a for-each sub-body (in_foreach): there
	 * pin_body_foreach_body() always supplies a fully-closed, retained
	 * context node for the callee's PUSH_ATTR/PUSH_TEXT/PUSH_NODES ops to
	 * walk (see pin_exec_run_call()).  Outside a for-each, the main body
	 * FSM's "current node" can be a still-open element with an incomplete
	 * retained subtree (Open Question 1), so warn and skip rather than
	 * risk silent wrong output.
	 */
	if (pin_is_xsl(child, "call-template")) {
	    xmlChar *tname = xmlGetProp(child, (const xmlChar *) "name");
	    if (tname == NULL || tname[0] == '\0') {
		pin_error(rb, child,
			  "xsl:call-template: missing or empty name attribute");
		if (tname)
		    xmlFree(tname);
		continue;
	    }

	    if (!in_foreach) {
		pin_warn(child, "xsl:call-template name=\"%s\": not supported "
			 "here; only supported inside xsl:for-each in the "
			 "streaming compiler", (const char *) tname);
		xmlFree(tname);
		continue;
	    }

	    uint32_t nparam = 0;
	    for (xmlNodePtr wpc = child->children; wpc; wpc = wpc->next) {
		if (!pin_is_xsl(wpc, "with-param"))
		    continue;
		xmlChar *wpname = xmlGetProp(wpc, (const xmlChar *) "name");
		xmlChar *wpsel  = xmlGetProp(wpc, (const xmlChar *) "select");
		if (wpname == NULL || wpname[0] == '\0') {
		    pin_error(rb, wpc,
			      "xsl:with-param: missing or empty name attribute");
		    if (wpname)
			xmlFree(wpname);
		    if (wpsel)
			xmlFree(wpsel);
		    continue;
		}

		pin_body_instr_t *wbip = pin_body_instr_new(rb, nextp, NULL);
		if (wbip == NULL) {
		    xmlFree(wpname);
		    if (wpsel)
			xmlFree(wpsel);
		    xmlFree(tname);
		    return;
		}
		wbip->bi_type = BIA_WITH_PARAM;
		wbip->bi_tag = pin_namepool_atom(rb->prb_workspace,
						 (const char *) wpname, TRUE);
		const char *s = wpsel ? (const char *) wpsel : ".";
		wbip->bi_select = pin_namepool_atom(rb->prb_workspace, s, TRUE);
		nparam += 1;

		xmlFree(wpname);
		if (wpsel)
		    xmlFree(wpsel);
	    }

	    pin_body_instr_t *cbip = pin_body_instr_new(rb, nextp, NULL);
	    if (cbip == NULL) {
		xmlFree(tname);
		return;
	    }
	    cbip->bi_type = BIA_CALL;
	    cbip->bi_select = pin_namepool_atom(rb->prb_workspace,
						(const char *) tname, TRUE);
	    cbip->bi_filter_idx = nparam;

	    const char *url = (child->doc && child->doc->URL)
			      ? (const char *) child->doc->URL : "(unknown)";
	    pin_name_id_t src_file = pin_namepool_atom(rb->prb_workspace,
						       url, TRUE);
	    pin_rulebook_pending_call_add(rb, cbip->bi_select, src_file,
					  (uint32_t) xmlGetLineNo(child));

	    xmlFree(tname);
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
	pin_compile_body_r(child, rb, nextp, in_foreach);
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
pin_compile_body (xmlNodePtr body_node, pin_rulebook_t *rb, int in_foreach)
{
    pin_body_instr_id_t head = pin_body_instr_id_null_atom();
    pin_body_instr_id_t *nextp = &head;
    pin_compile_body_r(body_node, rb, &nextp, in_foreach);
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

	pin_body_retain_t level = BRETAIN_DISCARD;
	if (bip->bi_type == BIA_COPY || bip->bi_type == BIA_COPY_SELECT
		|| bip->bi_type == BIA_APPLY || bip->bi_type == BIA_VALUE_OF)
	    level = BRETAIN_NONE;
	if (bip->bi_type == BIA_FOR_EACH)
	    level = BRETAIN_DOCUMENT;
	/*
	 * BIA_CALL hands the matched element to pin_exec_run_call(), whose
	 * ops (PUSH_ATTR/PUSH_TEXT/PUSH_NODES) require a fully-closed
	 * retained subtree; a single BIA_COPY-style stream isn't enough.
	 */
	if (bip->bi_type == BIA_CALL && level < BRETAIN_ELEMENT)
	    level = BRETAIN_ELEMENT;
	if (level > retain)
	    retain = level;

	cur = bip->bi_next;
    }
    return retain;
}

/*
 * Return true if node's body holds an xsl:call-template outside any
 * xsl:for-each (whose sub-body BIA_ can already run a call from).
 * Such a call needs the whole context element, and BIA_ can't wait
 * for it at the top level, so the template must buffer and run as
 * ops at the element's close.
 */
static bool
pin_has_toplevel_call (xmlNodePtr node)
{
    for (xmlNodePtr child = node->children; child; child = child->next) {
	if (child->type != XML_ELEMENT_NODE)
	    continue;
	if (pin_is_xsl(child, "for-each"))
	    continue;
	if (pin_is_xsl(child, "call-template"))
	    return true;
	if (pin_has_toplevel_call(child))
	    return true;
    }

    return false;
}

/*
 * Count the instructions in node's body that read the context
 * element's children: xsl:for-each and xsl:apply-templates, outside
 * any for-each (a nested one runs per item of its outer loop).  One
 * reader can stream; two or more need the whole element, so the
 * template buffers and runs as ops at the element's close.
 */
static int
pin_count_readers (xmlNodePtr node)
{
    int count = 0;

    for (xmlNodePtr child = node->children; child; child = child->next) {
	if (child->type != XML_ELEMENT_NODE)
	    continue;
	if (pin_is_xsl(child, "for-each")
	        || pin_is_xsl(child, "apply-templates"))
	    count += 1;
	else
	    count += pin_count_readers(child);
    }

    return count;
}

/*
 * Return true if the XPath expression s (len bytes) reads nothing but
 * attributes of the context node, variables and literals: no child
 * paths, ".", or functions that could read one.  Used for the parts
 * of a template body outside a streaming for-each, where the context
 * element's children are not retained.
 */
static bool
pin_expr_attrs_only (const char *s, size_t len)
{
    static const char *ok_words[] = {
	"and", "or", "div", "mod", "not", "true", "false", "concat",
	"contains", "starts-with", "translate", "substring",
	"substring-before", "substring-after", NULL
    };
    size_t i = 0;

    while (i < len) {
	char c = s[i];

	if (c == '\'' || c == '"') {
	    const char *q = memchr(s + i + 1, c, len - i - 1);
	    if (q == NULL)
		return false;
	    i = (size_t) (q - s) + 1;
	    continue;
	}

	if (c == '@' || c == '$') {
	    i += 1;
	    while (i < len && (isalnum((unsigned char) s[i])
				|| s[i] == '_' || s[i] == '-' || s[i] == '.'))
		i += 1;
	    if (i < len && s[i] == ':')
		return false;
	    continue;
	}

	if (isalpha((unsigned char) c) || c == '_') {
	    size_t start = i;
	    while (i < len && (isalnum((unsigned char) s[i])
				|| s[i] == '_' || s[i] == '-'))
		i += 1;
	    bool known = false;
	    for (int w = 0; ok_words[w]; w++) {
		if (strlen(ok_words[w]) == i - start
			&& strncmp(ok_words[w], s + start, i - start) == 0) {
		    known = true;
		    break;
		}
	    }
	    if (!known)
		return false;
	    continue;
	}

	if (c == '.' && !(i + 1 < len && isdigit((unsigned char) s[i + 1])))
	    return false;
	if (c == '/' || c == '*' || c == '[' || c == '|' || c == ':')
	    return false;
	i += 1;
    }

    return true;
}

/*
 * Return true if the expressions in node's AVT attribute values (the
 * "{...}" parts) all satisfy check.  A "{{" is a literal brace.
 */
static bool
pin_avts_ok (xmlNodePtr node, bool (*check)(const char *, size_t))
{
    for (xmlAttrPtr attr = node->properties; attr; attr = attr->next) {
	xmlChar *val = xmlNodeGetContent((xmlNodePtr) attr);
	if (val == NULL)
	    continue;

	bool ok = true;
	for (const char *cp = (const char *) val; ok && *cp; cp++) {
	    if (*cp != '{')
		continue;
	    if (cp[1] == '{') {
		cp += 1;
		continue;
	    }
	    const char *ep = strchr(cp, '}');
	    if (ep == NULL)
		break;
	    ok = check(cp + 1, (size_t) (ep - cp - 1));
	    cp = ep;
	}
	xmlFree(val);
	if (!ok)
	    return false;
    }

    return true;
}

/*
 * Return true if node's expression attributes (select, test) and AVTs
 * all satisfy check.  For xsl:* elements other than xsl:element and
 * xsl:attribute, only select and test hold expressions; the rest are
 * plain names (mode, name, ...).
 */
static bool
pin_node_exprs_ok (xmlNodePtr node, bool (*check)(const char *, size_t))
{
    if (pin_is_xsl(node, NULL)) {
	static const char *expr_attrs[] = { "select", "test", NULL };
	for (int a = 0; expr_attrs[a]; a++) {
	    xmlChar *val = xmlGetProp(node, (const xmlChar *) expr_attrs[a]);
	    if (val == NULL)
		continue;
	    bool ok = check((const char *) val, strlen((const char *) val));
	    xmlFree(val);
	    if (!ok)
		return false;
	}
	if (!pin_is_xsl(node, "element") && !pin_is_xsl(node, "attribute"))
	    return true;
    }

    return pin_avts_ok(node, check);
}

/*
 * Return true if the XPath expression s (len bytes) can run against one
 * streamed for-each item: nothing that looks outside the item's own
 * subtree (parents, siblings, the document, keys) or needs the whole
 * node-set (last()).
 */
static bool
pin_expr_item_only (const char *s, size_t len)
{
    static const char *bad[] = {
	"..", "::", "//", "last(", "key(", "document(", "id(", NULL
    };
    char prev = ' ';

    for (size_t i = 0; i < len; i++) {
	char c = s[i];

	if (c == '\'' || c == '"') {
	    const char *q = memchr(s + i + 1, c, len - i - 1);
	    if (q == NULL)
		return false;
	    i = (size_t) (q - s);
	    prev = c;
	    continue;
	}

	for (int b = 0; bad[b]; b++) {
	    size_t blen = strlen(bad[b]);
	    if (i + blen <= len && strncmp(s + i, bad[b], blen) == 0)
		return false;
	}

	/* An absolute path: '/' that doesn't follow a step */
	if (c == '/' && strchr(" \t\n(,[=<>|+-!", prev) != NULL)
	    return false;

	prev = c;
    }

    return true;
}

/*
 * Return true if every node in node's subtree can run against one
 * streamed for-each item: see pin_expr_item_only().  A nested reader,
 * call, sort or xsl:number (which counts preceding siblings) needs
 * more than the item, so it disqualifies the loop.
 */
static bool
pin_subtree_item_only (xmlNodePtr node)
{
    for (xmlNodePtr child = node->children; child; child = child->next) {
	if (child->type != XML_ELEMENT_NODE)
	    continue;
	if (pin_is_xsl(child, "for-each")
		|| pin_is_xsl(child, "apply-templates")
		|| pin_is_xsl(child, "call-template")
		|| pin_is_xsl(child, "sort")
		|| pin_is_xsl(child, "number"))
	    return false;
	if (!pin_node_exprs_ok(child, pin_expr_item_only))
	    return false;
	if (!pin_subtree_item_only(child))
	    return false;
    }

    return true;
}

/*
 * Return true if node is an extension element: its namespace is named
 * by an extension-element-prefixes attribute on an enclosing element
 * (unqualified on the xsl:stylesheet, xsl:-qualified elsewhere), like
 * slax:trace.  It is an instruction, not a literal result element.
 */
static bool
pin_is_extension_element (xmlNodePtr node)
{
    if (node->ns == NULL || node->ns->href == NULL)
	return false;

    for (xmlNodePtr np = node; np && np->type == XML_ELEMENT_NODE;
	    np = np->parent) {
	xmlChar *list = pin_is_xsl(np, NULL)
	    ? xmlGetNoNsProp(np, (const xmlChar *) "extension-element-prefixes")
	    : xmlGetNsProp(np, (const xmlChar *) "extension-element-prefixes",
			   (const xmlChar *) PIN_XSL_URI);
	if (list == NULL)
	    continue;

	bool found = false;
	char *save = NULL;
	for (char *tok = strtok_r((char *) list, " \t\n", &save); tok;
		tok = strtok_r(NULL, " \t\n", &save)) {
	    const xmlChar *prefix = strcmp(tok, "#default") == 0
		? NULL : (const xmlChar *) tok;
	    xmlNsPtr ns = xmlSearchNs(node->doc, node, prefix);
	    if (ns && ns->href && xmlStrEqual(ns->href, node->ns->href)) {
		found = true;
		break;
	    }
	}
	xmlFree(list);
	if (found)
	    return true;
    }

    return false;
}

/*
 * Return true if s (len bytes) is exactly "@name" or "$name": the AVT
 * forms BIA_ can expand outside a for-each, where there is no context
 * node, only the matched element's attribute string.
 */
static bool
pin_expr_attr_or_var (const char *s, size_t len)
{
    if (len < 2 || (s[0] != '@' && s[0] != '$'))
	return false;
    for (size_t i = 1; i < len; i++) {
	if (!isalnum((unsigned char) s[i]) && s[i] != '_' && s[i] != '-'
		&& s[i] != '.')
	    return false;
    }

    return true;
}

/*
 * Return true if every node in node's subtree, skipping the subtree of
 * skip, is one BIA_ runs without the context element's children, which
 * a streaming for-each doesn't retain: literal elements and text,
 * xsl:text, xsl:value-of of an attribute or literal, xsl:if/choose on
 * attributes, and the output instructions around them.  AVTs must be
 * "{@name}" or "{$name}".  Anything else (variables, copy-of, general
 * expressions) returns false, and the template keeps buffering.
 */
static bool
pin_subtree_attrs_only (xmlNodePtr node, xmlNodePtr skip)
{
    for (xmlNodePtr child = node->children; child; child = child->next) {
	if (child->type != XML_ELEMENT_NODE || child == skip)
	    continue;

	if (!pin_is_xsl(child, NULL)) {
	    if (pin_is_extension_element(child)
		    || !pin_avts_ok(child, pin_expr_attr_or_var))
		return false;
	} else if (pin_is_xsl(child, "value-of")) {
	    xmlChar *sel = xmlGetProp(child, (const xmlChar *) "select");
	    const char *ss = sel ? (const char *) sel : "";
	    size_t slen = strlen(ss);
	    bool ok = (ss[0] == '@' && pin_expr_attr_or_var(ss, slen))
		|| (slen >= 2 && (ss[0] == '\'' || ss[0] == '"')
		    && ss[slen - 1] == ss[0]
		    && memchr(ss + 1, ss[0], slen - 2) == NULL);
	    if (sel)
		xmlFree(sel);
	    if (!ok)
		return false;
	} else if (pin_is_xsl(child, "if") || pin_is_xsl(child, "when")) {
	    /* BIA_IF's filter can't read variables */
	    xmlChar *test = xmlGetProp(child, (const xmlChar *) "test");
	    bool var = (test && strchr((const char *) test, '$') != NULL);
	    if (test)
		xmlFree(test);
	    if (var || !pin_node_exprs_ok(child, pin_expr_attrs_only))
		return false;
	} else if (pin_is_xsl(child, "element")
		   || pin_is_xsl(child, "attribute")) {
	    if (!pin_avts_ok(child, pin_expr_attr_or_var))
		return false;
	} else if (!pin_is_xsl(child, "text")
		   && !pin_is_xsl(child, "choose")
		   && !pin_is_xsl(child, "otherwise")
		   && !pin_is_xsl(child, "comment")
		   && !pin_is_xsl(child, "message")) {
	    return false;
	}

	if (!pin_subtree_attrs_only(child, skip))
	    return false;
    }

    return true;
}

/*
 * Return the xsl:for-each node at depth >= 1 in node's subtree, outside
 * any other for-each, or NULL.  Used once pin_count_readers() has said
 * there's exactly one reader.
 */
static xmlNodePtr
pin_find_reader (xmlNodePtr node)
{
    for (xmlNodePtr child = node->children; child; child = child->next) {
	if (child->type != XML_ELEMENT_NODE)
	    continue;
	if (pin_is_xsl(child, "for-each"))
	    return child;
	if (pin_is_xsl(child, "apply-templates"))
	    continue;
	xmlNodePtr found = pin_find_reader(child);
	if (found)
	    return found;
    }

    return NULL;
}

/*
 * R5 "stream where we can": if the match template tmpl's only reader
 * of the context element's children is an unsorted xsl:for-each over
 * one child name, return that for-each.  It can then run as each
 * selected child arrives (PBMODE_FOR_EACH_STREAM), keeping one item
 * at a time rather than the whole element.  This needs:
 *
 *   - no top-level call-template and exactly one reader;
 *   - select is a bare element name, with no xsl:sort;
 *   - the loop body looks only inside the item (no "..", absolute
 *     paths, last(), nested readers, or calls);
 *   - the rest of the template reads only the context element's
 *     attributes, since its children are not retained.
 *
 * Return NULL otherwise (the template buffers, as before).
 */
static xmlNodePtr
pin_template_stream_foreach (xmlNodePtr tmpl)
{
    if (tmpl == NULL || !pin_is_xsl(tmpl, "template"))
	return NULL;

    xmlChar *match = xmlGetProp(tmpl, (const xmlChar *) "match");
    if (match == NULL)
	return NULL;
    /*
     * "/" and "/name" are root rules (prb_root_rule), whose context
     * isn't handled by the streaming path; a union or path could match
     * anywhere, which is fine, but keep anything anchored out.
     */
    bool root = (match[0] == '/');
    xmlFree(match);
    if (root)
	return NULL;

    if (pin_has_toplevel_call(tmpl) || pin_count_readers(tmpl) != 1)
	return NULL;

    xmlNodePtr fe = pin_find_reader(tmpl);
    if (fe == NULL)
	return NULL;

    xmlChar *sel = xmlGetProp(fe, (const xmlChar *) "select");
    if (sel == NULL)
	return NULL;
    const char *s = (const char *) sel;
    bool name_ok = (isalpha((unsigned char) s[0]) || s[0] == '_');
    for (const char *cp = s; name_ok && *cp; cp++) {
	if (!isalnum((unsigned char) *cp) && *cp != '_' && *cp != '-'
		&& *cp != '.')
	    name_ok = false;
    }
    xmlFree(sel);
    if (!name_ok)
	return NULL;

    if (!pin_subtree_item_only(fe))
	return NULL;
    if (!pin_subtree_attrs_only(tmpl, fe))
	return NULL;

    return fe;
}

/*
 * Scan a template body for the first simple xsl:for-each select="name".
 * Compiles the for-each body into a body instruction list and returns a
 * new rulebook state (select_name->body, default->DISCARD), or the null id
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

	/*
	 * A sorted for-each can't stream: the sort needs the whole set,
	 * so leave it to BIA_, which buffers until the element closes.
	 * Streaming here would silently drop the sort.
	 */
	bool sorted = false;
	for (xmlNodePtr sp = child->children; sp; sp = sp->next) {
	    if (pin_is_xsl(sp, "sort")) {
		sorted = true;
		break;
	    }
	}
	if (sorted) {
	    xmlFree(select);
	    continue;
	}

	pin_body_instr_id_t body_head = pin_compile_body(child, rb, 1);
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
    int             poc_named;    /* compiling a named template's body */
    int             poc_multi_step; /* for-each may walk a multi-step path */

    /*
     * Compile-time namespace-binding stack, mirroring the real PIN_OP_NS_PUSH
     * / PIN_OP_NS_POP ops emitted into the op sequence, but kept separately
     * here so that xpath expression compilation (pin_op_push_eval) can
     * resolve a prefix to a URI immediately, without replaying ops.
     */
    pin_ns_map_t   *poc_ns_stack;
    uint32_t        poc_ns_cur;
    uint32_t        poc_ns_max;
} pin_op_cursor_t;

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
 * Push one prefix/uri binding: emit a real PIN_OP_NS_PUSH op (so the
 * runtime pes_ns stack stays in sync for future output-side namespace
 * work) and record the same binding on cur's compile-time stack (used by
 * pin_op_push_eval for immediate prefix resolution). 'prefix' may be the
 * null atom, meaning the default (unprefixed) namespace.
 * Returns 0 on success, -1 on failure (OOM); the caller should simply
 * skip counting this binding as pending-to-pop on failure.
 */
static int
pin_compile_ns_push (pin_op_cursor_t *cur, pin_name_id_t prefix, pin_name_id_t uri)
{
    if (cur->poc_ns_cur >= cur->poc_ns_max) {
	uint32_t newsize = cur->poc_ns_max ? cur->poc_ns_max * 2 : 8;
	pin_ns_map_t *np = realloc(cur->poc_ns_stack, newsize * sizeof(*np));
	if (np == NULL)
	    return -1;
	cur->poc_ns_stack = np;
	cur->poc_ns_max = newsize;
    }

    pin_op_t *opp = pin_op_new(cur, NULL);
    if (opp == NULL)
	return -1;
    opp->po_type = PIN_OP_NS_PUSH;
    opp->po_name = prefix;
    opp->po_name2 = uri;

    cur->poc_ns_stack[cur->poc_ns_cur].pnm_prefix = prefix;
    cur->poc_ns_stack[cur->poc_ns_cur].pnm_uri = uri;
    cur->poc_ns_cur += 1;
    return 0;
}

/*
 * Emit a PIN_OP_NS_POP for 'count' bindings previously pushed by
 * pin_compile_ns_push on this same cursor, and pop them off the
 * compile-time stack. 'count' is clamped to the current depth so a
 * mismatched call cannot underflow.
 */
static void
pin_compile_ns_pop (pin_op_cursor_t *cur, uint32_t count)
{
    if (count > cur->poc_ns_cur)
	count = cur->poc_ns_cur;
    if (count == 0)
	return;

    pin_op_t *opp = pin_op_new(cur, NULL);
    if (opp != NULL) {
	opp->po_type = PIN_OP_NS_POP;
	opp->po_count = count;
    }

    cur->poc_ns_cur -= count;
}

/*
 * Copy src's current compile-time namespace bindings into dst, as a
 * snapshot (not shared storage). Used when forking an independent
 * sub-cursor (e.g. for xsl:for-each's body), which needs to resolve
 * prefixes against everything already in scope but must not alias cur's
 * growable array, since the sub-cursor's own pushes/pops must not affect
 * cur after the fork returns. The caller frees dst->poc_ns_stack when done;
 * no PIN_OP_NS_POP is emitted for this copy, since the real ops for these
 * inherited bindings already live in src's own sequence.
 */
static void
pin_compile_ns_clone (pin_op_cursor_t *dst, const pin_op_cursor_t *src)
{
    dst->poc_ns_stack = NULL;
    dst->poc_ns_cur = 0;
    dst->poc_ns_max = 0;

    if (src->poc_ns_cur == 0)
	return;

    pin_ns_map_t *np = malloc(src->poc_ns_cur * sizeof(*np));
    if (np == NULL)
	return;

    memcpy(np, src->poc_ns_stack, src->poc_ns_cur * sizeof(*np));
    dst->poc_ns_stack = np;
    dst->poc_ns_cur = src->poc_ns_cur;
    dst->poc_ns_max = src->poc_ns_cur;
}

/*
 * Walk body_node up through its ancestors to the document root, pushing
 * every xmlns declaration found along the way onto cur's namespace stack,
 * outermost first, so the innermost (closest) declaration ends up on top --
 * matching ordinary XML prefix-shadowing. Call once before compiling a
 * template body: the per-child nsDef walk in pin_compile_ops_r only sees
 * declarations on body_node's own children, so this seeds anything
 * declared on body_node itself (the xsl:template element) or higher
 * (e.g. xmlns:slax on the enclosing xsl:stylesheet), which is the common
 * real-world case. Returns the number of bindings pushed, for the
 * matching pin_compile_ns_pop() once the body has been compiled.
 */
static uint32_t
pin_compile_ns_seed (xmlNodePtr body_node, pin_op_cursor_t *cur)
{
    enum { PIN_NS_SEED_MAX_ANCESTORS = 128 };
    xmlNodePtr ancestors[PIN_NS_SEED_MAX_ANCESTORS];
    uint32_t count = 0;

    for (xmlNodePtr np = body_node; np && count < PIN_NS_SEED_MAX_ANCESTORS;
	 np = np->parent)
	ancestors[count++] = np;

    pin_workspace_t *pwp = cur->poc_rb->prb_workspace;
    uint32_t pushed = 0;

    for (uint32_t i = count; i > 0; i--) {
	xmlNodePtr np = ancestors[i - 1];
	for (xmlNsPtr ns = np->nsDef; ns; ns = ns->next) {
	    if (ns->href == NULL)
		continue;

	    pin_name_id_t prefix_id = ns->prefix
		? pin_namepool_atom(pwp, (const char *) ns->prefix, TRUE)
		: pin_name_id_null_atom();
	    pin_name_id_t uri_id = pin_namepool_atom(pwp,
			(const char *) ns->href, TRUE);

	    if (pin_compile_ns_push(cur, prefix_id, uri_id) == 0)
		pushed += 1;
	}
    }

    return pushed;
}

/*
 * Classify a select/test expression at compile time.
 *
 * SELK_DOWN covers "a", "a/b", "a/b/c" -- all steps descend from context.
 * Everything that does not fit a simpler category and is not a clean
 * downward path is SELK_COMPLEX and causes a BIA_ complexity fallback.
 */
typedef enum {
    SELK_EMPTY,     /* null or empty string */
    SELK_SELF,      /* "." */
    SELK_ATTR,      /* "@name" */
    SELK_LITERAL,   /* "'string'" */
    SELK_NUMBER,    /* "123", "3.14", "-2" */
    SELK_VAR,       /* "$name" */
    SELK_DOWN,      /* "a", "a/b/c" -- descending element path */
    SELK_COMPLEX,   /* "..", "/abs", "//", predicates, functions, etc. */
} pin_sel_kind_t;

/*
 * A bare numeric literal has no quotes to mark it, so it must be
 * distinguished from an element name (SELK_DOWN) by shape: an optional
 * leading sign, digits, and at most one decimal point.
 */
static int
pin_is_numeric_literal (const char *s)
{
    if (*s == '-')
        s += 1;

    if (*s == '\0')
        return 0;

    int seen_dot = 0;

    for ( ; *s != '\0'; s++) {
        if (*s == '.' && !seen_dot) {
            seen_dot = 1;
            continue;
        }
        if (!isdigit((unsigned char) *s))
            return 0;
    }

    return 1;
}

/*
 * True if "s" is one call to an XPath function whose value is always
 * a string, number or boolean, so copy-of of it is the same as
 * value-of.  SLAX makes these for a ternary ("a ? concat(b, c) : d").
 */
static int
pin_select_is_scalar_call (const char *s)
{
    static const char *const names[] = {
	"boolean", "ceiling", "concat", "contains", "count", "false",
	"floor", "local-name", "name", "namespace-uri", "normalize-space",
	"not", "number", "round", "starts-with", "string",
	"string-length", "substring", "substring-after",
	"substring-before", "sum", "translate", "true", NULL
    };

    while (*s == ' ')
	s += 1;

    const char *paren = strchr(s, '(');
    if (paren == NULL)
	return 0;

    size_t nlen = paren - s;
    int found = 0;
    for (int i = 0; names[i]; i++) {
	if (strlen(names[i]) == nlen && strncmp(s, names[i], nlen) == 0) {
	    found = 1;
	    break;
	}
    }
    if (!found)
	return 0;

    /* The call's close paren must end the expression */
    int depth = 0;
    char quote = '\0';
    const char *cp;
    for (cp = paren; *cp; cp++) {
	if (quote) {
	    if (*cp == quote)
		quote = '\0';
	} else if (*cp == '\'' || *cp == '"') {
	    quote = *cp;
	} else if (*cp == '(') {
	    depth += 1;
	} else if (*cp == ')') {
	    depth -= 1;
	    if (depth == 0)
		break;
	}
    }
    if (*cp != ')')
	return 0;

    for (cp += 1; *cp == ' '; cp++)
	continue;

    return (*cp == '\0');
}

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
    if (pin_is_numeric_literal(s))
        return SELK_NUMBER;
    /* Downward: no absolute prefix, no upward steps, no predicates/funcs */
    if (s[0] != '/' && strstr(s, "..") == NULL
	    && strpbrk(s, " \t=<>'\"@[]()*") == NULL)
	return SELK_DOWN;
    return SELK_COMPLEX;
}

/*
 * Return a malloc'd copy of the 'lineno'-th (1-based) line of 'text'
 * (no trailing newline), or NULL if 'text' has fewer lines than that.
 * Used to echo the offending source line in a clang-style diagnostic.
 */
static char *
pin_nth_line (const char *text, unsigned lineno)
{
    if (lineno == 0)
	lineno = 1;

    const char *start = text;
    for (unsigned n = 1; n < lineno; n++) {
	start = strchr(start, '\n');
	if (start == NULL)
	    return NULL;
	start += 1;
    }

    const char *end = strchr(start, '\n');
    size_t len = end ? (size_t) (end - start) : strlen(start);

    char *copy = xo_realloc(NULL, len + 1);
    if (copy == NULL)
	return NULL;

    memcpy(copy, start, len);
    copy[len] = '\0';
    return copy;
}

/*
 * Return a malloc'd copy of the 'lineno'-th (1-based) line of the file
 * named 'filename' (no trailing newline/CR), or NULL if the file can't
 * be opened or has fewer lines than that.  Used to echo the *real*
 * source line -- the whole statement, not just the XPath fragment
 * being compiled -- in a clang-style diagnostic.  Freed with free(),
 * not xo_free(), since it comes from getline().
 */
static char *
pin_read_source_line (const char *filename, unsigned lineno)
{
    if (filename == NULL || lineno == 0)
	return NULL;

    FILE *fp = fopen(filename, "r");
    if (fp == NULL)
	return NULL;

    char *line = NULL;
    size_t cap = 0;
    ssize_t len = -1;

    for (unsigned n = 0; n < lineno; n++) {
	len = getline(&line, &cap, fp);
	if (len < 0)
	    break;
    }

    fclose(fp);

    if (len < 0) {
	free(line);
	return NULL;
    }

    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
	line[--len] = '\0';

    return line;
}

/*
 * Report a failed xo_eval_compile() (from pin_op_push_eval, below) as a
 * clang-style diagnostic: the detailed message xo_eval_compile_fail()
 * built (now retained, since pin's xfdo_error is wired up -- see
 * pin_filter_data_error() in pin_filter.c), the real line/column of the
 * offending token, the echoed source line, and -- for an arity mismatch
 * on a dynamically-resolved function (e.g. bit:and) -- a "consider"
 * hint built from its recorded description/argument-hint.
 *
 * Location mapping: 'expr' was compiled wrapped as "*[expr]" (see the
 * caller's doc comment), which adds no newline, so the failing node's
 * internal line number maps 1:1 onto a line of 'expr' itself, and only
 * the column needs adjusting -- by 2, to remove the "*[" prefix's
 * width -- and only on line 1.  The *file* line is approximated as the
 * XML node's own line number plus however many newlines precede the
 * failure inside 'expr' (0 for the overwhelmingly common single-line
 * expression); libxml2 only annotates nodes with a line, not a column,
 * so there's no way to recover the attribute value's exact byte offset
 * on that line directly from the node.
 *
 * To still show the *complete* source line (e.g. "var $x =
 * slax:value(...);", not just the "slax:value(...)" fragment that was
 * actually compiled) and the right column within it, re-read that line
 * from 'fn' on disk and locate the compiled fragment inside it via a
 * verbatim substring search; 'expr' is lexed straight out of the
 * original source text, so it reliably reappears unchanged in the real
 * line for the common case.  Falls back to echoing 'expr' itself (the
 * prior, narrower behavior) if the file can't be read or the fragment
 * isn't found verbatim (e.g. it was rewritten during compilation).
 */
static void
pin_report_eval_fail (pin_op_cursor_t *cur, const char *expr)
{
    xmlNodePtr nodep = cur->poc_node;
    const char *fn = (nodep && nodep->doc && nodep->doc->URL)
        ? (const char *) nodep->doc->URL : NULL;
    int node_line = nodep ? (int) xmlGetLineNo(nodep) : 0;

    unsigned eline = 0, ecol = 0;
    xo_eval_compile_fail_location(&eline, &ecol);

    int line = node_line + (eline > 1 ? (int) eline - 1 : 0);
    int col = (int) ecol;

    char *source_line = pin_nth_line(expr, eline ? eline : 1);
    char *real_line = (line > 0) ? pin_read_source_line(fn, (unsigned) line)
	: NULL;

    const char *display_line = source_line;
    int disp_col = col;

    if (real_line != NULL) {
	char *match = (source_line != NULL)
	    ? strstr(real_line, source_line) : NULL;
	if (match != NULL) {
	    display_line = real_line;
	    disp_col = (int) (match - real_line) + col;
	}
    }

    const char *msg = pin_filter_last_compile_error();

    const char *fname = NULL, *fdesc = NULL, *farghelp = NULL;
    pin_filter_last_function_help(&fname, &fdesc, &farghelp);
    if (fname != NULL && (fdesc != NULL || farghelp != NULL))
	psu_complain_consider("invoke as %s%s%s%s%s", fname,
			       farghelp ? farghelp : "",
			       fdesc ? " -- " : "", fdesc ? fdesc : "", "");

    if (msg != NULL)
	psu_complain(fn, line, disp_col, 0, display_line, "%s", msg);
    else
	psu_complain(fn, line, disp_col, 0, display_line,
		     "unable to compile XPath expression: %s", expr);

    if (source_line != NULL)
	xo_free(source_line);
    if (real_line != NULL)
	free(real_line);

    if (cur->poc_rb && cur->poc_rb->prb_workspace)
        cur->poc_rb->prb_workspace->pw_errors += 1;
}

/*
 * Compile expr as a full XPath expression via libxo's XPath parser and
 * xo_eval compiler, store the resulting program on the rulebook, and turn
 * *push (already allocated by the caller) into a PIN_OP_EVAL referencing
 * it.  po_name holds a plain array index into prb_eval_progs[] (see
 * pin_rulebook_eval_prog_add), not a namepool atom.  Returns 0 on success,
 * -1 on a parse/compile failure (reported via pin_error against the node
 * currently being compiled).
 *
 * xo_filter_parse_expr() parses a full XPath expression (rather than
 * libxo's usual union of location paths) and returns its root, which
 * is what xo_eval_compile() wants for 'root'.
 */
static int
pin_op_push_eval (pin_op_cursor_t *cur, pin_op_t *push, const char *expr)
{
    pin_rulebook_t *rb = cur->poc_rb;
    xo_filter_t *xfp = pin_rulebook_get_eval_filter(rb);
    struct xo_xparse_data_s *xdp = xo_filter_xparse_data(NULL, xfp);

    ssize_t root = xo_filter_parse_expr(NULL, xfp, expr);
    if (root < 0) {
	pin_error(cur->poc_rb, cur->poc_node,
		  "unable to parse XPath expression: %s", expr);
	return -1;
    }

    xo_filter_data_t *dp = xo_filter_get_data_ptr(xfp);
    xo_filter_data_ops_t *ops = xo_filter_get_data_ops(xfp);
    pin_filter_set_compile_ns(xfp, cur->poc_ns_stack, cur->poc_ns_cur);
    pin_filter_clear_compile_error();
    xo_eval_prog_t *prog = xo_eval_compile(xdp, root, dp, ops);
    if (prog == NULL) {
	pin_report_eval_fail(cur, expr);
	return -1;
    }

    uint32_t idx = pin_rulebook_eval_prog_add(rb, prog, cur->poc_ns_stack,
					       cur->poc_ns_cur);
    if (idx == UINT32_MAX) {
	xo_eval_prog_destroy(prog);
	pin_error(cur->poc_rb, cur->poc_node,
		  "out of memory compiling XPath expression: %s", expr);
	return -1;
    }

    push->po_type = PIN_OP_EVAL;
    push->po_name = pin_name_id(idx);
    return 0;
}

/*
 * Emit the op that best represents an xsl:if / xsl:when test expression,
 * leaving the result on the value stack for a following PUSH_BOOL + IF
 * to consume.
 *
 * Supported atomic forms get a dedicated PUSH_* op (@attr, ., $var,
 * simple-name, number); anything else -- binary comparisons, arithmetic,
 * function calls, predicates -- is compiled once via xo_eval and replayed
 * at runtime by a single PIN_OP_EVAL.
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
    case SELK_NUMBER:
	push->po_type = PIN_OP_PUSH_STRING;
	push->po_name = pin_namepool_atom(pwp, test, TRUE);
	break;
    default:
	if (pin_op_push_eval(cur, push, test) < 0)
	    return NULL;
	break;
    }
    return push;
}

/*
 * Emit the op that pushes the value of a select expression.  This is
 * the test form, plus string literals.
 */
static pin_op_t *
pin_op_push_for_select (pin_op_cursor_t *cur, const char *sel)
{
    size_t slen = strlen(sel);

    /*
     * SLAX passes a body-built argument as "ext:node-set($tmp)".  Our
     * fragments are already node sets, so that's just "$tmp".
     */
    const char *cp = strstr(sel, ":node-set(");
    if (cp && sel[slen - 1] == ')') {
	const char *vp = cp + sizeof(":node-set(") - 1;
	size_t vlen = (sel + slen - 1) - vp;
	char *var = strndup(vp, vlen);
	pin_op_t *push = NULL;

	if (var && pin_classify_select(var) == SELK_VAR)
	    push = pin_op_push_for_select(cur, var);
	free(var);
	if (push)
	    return push;
    }

    if (pin_classify_select(sel) == SELK_LITERAL && slen >= 2
	    && sel[slen - 1] == sel[0]) {
	pin_workspace_t *pwp = cur->poc_rb->prb_workspace;
	pin_op_t *push = pin_op_new(cur, NULL);
	if (push == NULL)
	    return NULL;

	char *str = strndup(sel + 1, slen - 2);
	if (str == NULL)
	    return NULL;

	push->po_type = PIN_OP_PUSH_STRING;
	push->po_name = pin_namepool_atom(pwp, str, TRUE);
	free(str);
	return push;
    }

    return pin_op_push_for_test(cur, sel);
}

/*
 * An mvar's initial value is the fourth argument of the
 * slax:mvar-init() call in its select.  Returns that argument (to be
 * freed), or NULL if there isn't one, meaning the mvar starts empty.
 */
static char *
pin_compile_mvar_init_arg (const char *sel)
{
    static const char prefix[] = "slax:mvar-init(";
    const char *cp = sel + sizeof(prefix) - 1;
    const char *arg = NULL;
    unsigned nargs = 1, depth = 0;
    char quote = 0;

    for (; *cp; cp++) {
	if (quote) {
	    if (*cp == quote)
		quote = 0;
	    continue;
	}

	if (*cp == '"' || *cp == '\'') {
	    quote = *cp;
	} else if (*cp == '(' || *cp == '[') {
	    depth += 1;
	} else if (*cp == ')' || *cp == ']') {
	    if (depth == 0)
		break;
	    depth -= 1;
	} else if (*cp == ',' && depth == 0) {
	    nargs += 1;
	    if (nargs == 4)
		arg = cp + 1;
	    else if (nargs == 5)
		break;
	}
    }

    if (arg == NULL)
	return NULL;

    while (*arg == ' ' || *arg == '\t' || *arg == '\n')
	arg += 1;

    const char *ep = cp;
    while (ep > arg && (ep[-1] == ' ' || ep[-1] == '\t' || ep[-1] == '\n'))
	ep -= 1;

    return (ep > arg) ? strndup(arg, ep - arg) : NULL;
}

static void
pin_compile_ops_r (xmlNodePtr body_node, pin_op_cursor_t *cur);

/*
 * Compile body_node's children as content built for a value: the ops
 * that make it run between BUILD_OPEN and BUILD_CLOSE, which leaves
 * the fragment on the value stack.  Returns 0 or -1.
 */
static int
pin_compile_build (xmlNodePtr body_node, pin_op_cursor_t *cur)
{
    pin_op_t *open = pin_op_new(cur, NULL);
    if (open == NULL)
	return -1;
    open->po_type = PIN_OP_BUILD_OPEN;

    pin_compile_ops_r(body_node, cur);

    pin_op_t *close = pin_op_new(cur, NULL);
    if (close == NULL)
	return -1;
    close->po_type = PIN_OP_BUILD_CLOSE;

    return 0;
}

/*
 * Emit "type" (STORE_VAR to declare, SET_VAR to assign) for "name".
 * Returns 0 or -1.
 */
static int
pin_compile_store_var (pin_op_cursor_t *cur, const char *name,
		       pin_op_type_t type)
{
    pin_op_t *store = pin_op_new(cur, NULL);
    if (store == NULL)
	return -1;

    store->po_type = type;
    store->po_name = pin_namepool_atom(cur->poc_rb->prb_workspace, name, TRUE);
    return 0;
}

/*
 * Compile slax:append-to-variable or slax:set-variable, which SLAX
 * makes for "append $x += ..." and "set $x = ...".  The value is the
 * select, or else the body, built as content.  Returns 0 or -1.
 */
static int
pin_compile_mvar_update (xmlNodePtr child, pin_op_cursor_t *cur,
			 int append)
{
    xmlChar *vname = xmlGetProp(child, (const xmlChar *) "name");
    if (vname == NULL || vname[0] == '\0') {
	pin_error(cur->poc_rb, child, "%s: missing or empty name attribute",
		  (const char *) child->name);
	if (vname)
	    xmlFree(vname);
	return 0;
    }

    xmlChar *sel = xmlGetProp(child, (const xmlChar *) "select");
    int rc = -1;

    if (!append) {
	if (sel)
	    rc = pin_op_push_for_select(cur, (const char *) sel) ? 0 : -1;
	else
	    rc = pin_compile_build(child, cur);

	if (rc == 0)
	    rc = pin_compile_store_var(cur, (const char *) vname,
				       PIN_OP_SET_VAR);
	goto out;
    }

    pin_name_id_t name_id = pin_namepool_atom(cur->poc_rb->prb_workspace,
					      (const char *) vname, TRUE);
    if (sel) {
	if (pin_op_push_for_select(cur, (const char *) sel) == NULL)
	    goto out;

	pin_op_t *value = pin_op_new(cur, NULL);
	if (value == NULL)
	    goto out;
	value->po_type = PIN_OP_APPEND_VALUE;
	value->po_name = name_id;
	rc = 0;
	goto out;
    }

    pin_op_t *open = pin_op_new(cur, NULL);
    if (open == NULL)
	goto out;
    open->po_type = PIN_OP_APPEND_OPEN;
    open->po_name = name_id;

    pin_compile_ops_r(child, cur);

    pin_op_t *close = pin_op_new(cur, NULL);
    if (close == NULL)
	goto out;
    close->po_type = PIN_OP_APPEND_CLOSE;
    rc = 0;

 out:
    if (sel)
	xmlFree(sel);
    xmlFree(vname);
    return rc;
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
    uint32_t pending_ns_pop = 0;

    for (xmlNodePtr child = body_node->children; child; child = child->next) {
	if (pending_ns_pop) {
	    pin_compile_ns_pop(cur, pending_ns_pop);
	    pending_ns_pop = 0;
	}

	cur->poc_src_line = (uint32_t) xmlGetLineNo(child);
	cur->poc_node = child;

	if (child->type == XML_ELEMENT_NODE) {
	    for (xmlNsPtr ns = child->nsDef; ns; ns = ns->next) {
		if (ns->href == NULL)
		    continue;

		pin_name_id_t prefix_id = ns->prefix
		    ? pin_namepool_atom(pwp, (const char *) ns->prefix, TRUE)
		    : pin_name_id_null_atom();
		pin_name_id_t uri_id = pin_namepool_atom(pwp,
			    (const char *) ns->href, TRUE);

		if (pin_compile_ns_push(cur, prefix_id, uri_id) == 0)
		    pending_ns_pop += 1;
	    }
	}

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

	    if (kind == SELK_EMPTY) {
		pin_error(cur->poc_rb, child,
			  "xsl:copy-of: empty select attribute");
		xmlFree(sel);
		continue;
	    }

	    /*
	     * Per XSLT 1.0, copy-of of a non-node-set value inserts its
	     * string value as a text node, so a literal or number needs
	     * no node-set evaluation: compile it exactly as value-of does.
	     */
	    if (kind == SELK_LITERAL || kind == SELK_NUMBER) {
		pin_op_t *push = pin_op_new(cur, NULL);
		if (push == NULL) {
		    xmlFree(sel);
		    return;
		}
		push->po_type = PIN_OP_PUSH_STRING;
		if (kind == SELK_LITERAL) {
		    /* Strip the enclosing quotes in-place; sel is freed below */
		    sel[strlen(s) - 1] = '\0';
		    push->po_name = pin_namepool_atom(pwp, s + 1, TRUE);
		} else {
		    push->po_name = pin_namepool_atom(pwp, s, TRUE);
		}
		xmlFree(sel);

		pin_op_t *emit = pin_op_new(cur, NULL);
		if (emit == NULL)
		    return;
		emit->po_type = PIN_OP_EMIT;
		continue;
	    }

	    if (kind == SELK_COMPLEX && pin_select_is_scalar_call(s)) {
		pin_op_t *push = pin_op_new(cur, NULL);
		if (push == NULL || pin_op_push_eval(cur, push, s) < 0) {
		    xmlFree(sel);
		    return;
		}
		xmlFree(sel);

		pin_op_t *emit = pin_op_new(cur, NULL);
		if (emit == NULL)
		    return;
		emit->po_type = PIN_OP_EMIT;
		continue;
	    }

	    if (kind == SELK_COMPLEX) {
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
	    case SELK_NUMBER:
		push->po_type = PIN_OP_PUSH_STRING;
		push->po_name = pin_namepool_atom(pwp, s, TRUE);
		break;
	    default:  /* SELK_COMPLEX */
		if (pin_op_push_eval(cur, push, s) < 0) {
		    if (sel)
			xmlFree(sel);
		    return;
		}
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
	 * xsl:if -> PUSH_* + PUSH_BOOL + IF + body + JUMP (join point).
	 *
	 *   IF  po_next -> [first body op]   po_alt -> JUMP
	 *   [body ops]  last po_next -> JUMP
	 *   JUMP po_next -> [continuation]
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
	 * xsl:choose -> chain of (PUSH_* + PUSH_BOOL + IF + when-body + GOTO),
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

	    /*
	     * Only downward element paths ("a", "a/b/c"; no predicates,
	     * wildcards, attributes, or axes): pin_op_for_each walks the
	     * retained subtree child-by-child, one step at a time.  A
	     * multi-step path is only taken when poc_multi_step is set: in
	     * a named template's body, or in a match template that buffers
	     * anyway (see pin_count_readers()).  Otherwise a match template
	     * falls back to BIA_ for it, whose streaming for-each
	     * (pin_compile_foreach) needs no retained subtree.
	     */
	    if (pin_classify_select(s) != SELK_DOWN
		    || strpbrk(s, ".:") != NULL
		    || (!cur->poc_multi_step && strchr(s, '/') != NULL)) {
		cur->poc_complexity |= (1ULL << PIN_OP_COMPLEX_EXPR);
		xmlFree(sel);
		continue;
	    }

	    /* Collect xsl:sort children into a newline-separated spec string */
	    xo_buffer_t spec;
	    xo_buf_init(&spec);
	    bool sort_ok = pin_compile_sort_spec(cur->poc_rb, child, &spec,
						 cur->poc_named);

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
		.poc_named    = cur->poc_named,
		.poc_multi_step = cur->poc_multi_step,
	    };
	    pin_compile_ns_clone(&sub, cur);
	    pin_compile_ops_r(child, &sub);
	    free(sub.poc_ns_stack);

	    if (sub.poc_complexity) {
		cur->poc_complexity |= sub.poc_complexity;

		/*
		 * PIN_OP_APPLY alone is not a reason to neutralise: it now
		 * has a real op-engine implementation (pin_op_apply), and
		 * runs the same way whether reached directly or, as here,
		 * from inside a for-each's sub-sequence.  Any OTHER complex
		 * bit (PUSH_NODE's copy-of stub, COMPLEX_EXPR) still means
		 * "body too complex for op-dispatch", so fall back for those.
		 */
		if (sub.poc_complexity & ~(1ULL << PIN_OP_APPLY))
		    fe->po_type = PIN_OP_NONE;
	    }
	    continue;
	}

	/*
	 * xsl:variable name="x" select="expr"  -- compile expr, emit STORE_VAR.
	 * xsl:variable name="x">text body</xsl:variable> -- push literal text.
	 * xsl:variable name="x">content</xsl:variable> -- build a fragment.
	 *
	 * An mvar's select is a slax:mvar-init() call; its initial value
	 * is the call's fourth argument.
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
	    xmlChar *mvar = xmlGetProp(child, (const xmlChar *) "mvarname");

	    /* An mvar's shadow ("slax-x") variable isn't needed here */
	    if (mvar && sel == NULL && child->children == NULL) {
		xmlFree(mvar);
		xmlFree(vname);
		continue;
	    }
	    if (mvar)
		xmlFree(mvar);

	    /*
	     * An mvar starts with its initial value, or else empty.  A
	     * body-built value is in the variable named by "ivarname".
	     */
	    const char *expr = (const char *) sel;
	    char *init = NULL;
	    if (sel && strncmp(expr, "slax:mvar-init(", 15) == 0) {
		xmlChar *ivar = xmlGetProp(child, (const xmlChar *) "ivarname");
		if (ivar) {
		    size_t len = strlen((const char *) ivar) + 2;
		    init = malloc(len);
		    if (init)
			snprintf(init, len, "$%s", (const char *) ivar);
		    xmlFree(ivar);
		} else {
		    init = pin_compile_mvar_init_arg(expr);
		}
		expr = init;
	    }

	    int rc = 0;
	    if (expr) {
		if (pin_op_push_for_select(cur, expr) == NULL)
		    rc = -1;
	    } else if (pin_has_element_child(child)) {
		rc = pin_compile_build(child, cur);
	    } else {
		/*
		 * A text-only body is a string.  So is an mvar with no
		 * initial value, which libslax makes an empty string.
		 */
		const char *text = NULL;
		for (xmlNodePtr gc = child->children; gc; gc = gc->next) {
		    if (gc->type == XML_TEXT_NODE && gc->content
			    && !xmlIsBlankNode(gc)) {
			text = (const char *) gc->content;
			break;
		    }
		}

		pin_op_t *push = pin_op_new(cur, NULL);
		if (push == NULL) {
		    rc = -1;
		} else {
		    push->po_type = PIN_OP_PUSH_STRING;
		    push->po_name = pin_namepool_atom(pwp, text ? text : "",
						      TRUE);
		}
	    }

	    if (rc == 0)
		rc = pin_compile_store_var(cur, (const char *) vname,
					   PIN_OP_STORE_VAR);

	    free(init);
	    if (sel)
		xmlFree(sel);
	    xmlFree(vname);
	    if (rc < 0)
		return;
	    continue;
	}

	if (pin_is_slax(child, "append-to-variable")
		|| pin_is_slax(child, "set-variable")) {
	    if (pin_compile_mvar_update(child, cur,
			pin_is_slax(child, "append-to-variable")) < 0)
		return;
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
		case SELK_NUMBER:
		    push_def->po_type = PIN_OP_PUSH_STRING;
		    push_def->po_name = pin_namepool_atom(pwp, s, TRUE);
		    break;
		default:
		    if (pin_op_push_eval(cur, push_def, s) < 0) {
			xmlFree(pname);
			xmlFree(psel);
			return;
		    }
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
		 *   IF.po_alt   -> DISCARD (false branch: param not provided)
		 *   GOTO.po_alt -> JUMP (join after true branch)
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
	 * xsl:call-template name="x" -- possibly with xsl:with-param children.
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

		/* Compile value expression, or build the body's content */
		int rc;
		if (wpsel == NULL && pin_has_element_child(wpc))
		    rc = pin_compile_build(wpc, cur);
		else
		    rc = pin_op_push_for_select(cur,
				wpsel ? (const char *) wpsel : ".") ? 0 : -1;
		if (rc < 0) {
		    xmlFree(wpname);
		    if (wpsel)
			xmlFree(wpsel);
		    xmlFree(tname);
		    return;
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
	    pin_rulebook_pending_call_add(cur->poc_rb, call_op->po_name,
					 cur->poc_src_file, cur->poc_src_line);
	    xmlFree(tname);
	    continue;
	}

	/* xsl:element -> [optional push] + ELEMENT_OPEN + body + ELEMENT_CLOSE */
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
		    /* {$varname} -- strip braces in-place; ename is freed below */
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
		/* tag_id remains null -> ELEMENT_OPEN pops name from stack */
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

	/* xsl:attribute -> value expr on stack + EMIT_ATTRIB(name) */
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

	/* xsl:text -> PUSH_STRING + EMIT */
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

	/* xsl:copy -> PIN_OP_COPY_OPEN + children + PIN_OP_ELEMENT_CLOSE */
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

	/* xsl:message -> PIN_OP_MESSAGE_OPEN + children + PIN_OP_MESSAGE_CLOSE */
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

	/* xsl:comment -> PIN_OP_COMMENT_OPEN + children + PIN_OP_COMMENT_CLOSE */
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

	/* xsl:processing-instruction -> PIN_OP_PI_OPEN + children + PIN_OP_PI_CLOSE */
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

	/* xsl:number -> PIN_OP_NUMBER */
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

    if (pending_ns_pop)
	pin_compile_ns_pop(cur, pending_ns_pop);
}

/*
 * Compile the template body rooted at body_node into a PIN_OP_* sequence
 * in the rulebook's prb_ops pool.  'named' is non-zero for a named
 * template's body, which has no BIA_ fallback.  'multi_step' is non-zero
 * when a for-each may walk a multi-step path (a named template, or a
 * match template that buffers); otherwise that form is left to BIA_.
 * Returns the id of the first op, or the null id for an empty body.
 */
static pin_op_id_t
pin_compile_ops (xmlNodePtr body_node, pin_rulebook_t *rb,
		      pin_name_id_t src_file_id, uint64_t *complexity_out,
		      int named, int multi_step)
{
    pin_op_id_t head = pin_op_id_null_atom();

    /*
     * A node with no children at all can never produce a single op, so
     * skip straight to "no ops" rather than seeding namespace bindings
     * that nothing will use: that seeding emits real PIN_OP_NS_PUSH/POP
     * ops of its own, which would otherwise turn an empty body into a
     * non-null (but functionally inert) op sequence.
     */
    if (body_node->children == NULL) {
	if (complexity_out)
	    *complexity_out = 0;
	return head;
    }

    pin_op_cursor_t cur = {
	.poc_nextp = &head,
	.poc_rb = rb,
	.poc_src_file = src_file_id,
	.poc_src_line = 0,
	.poc_complexity = 0,
	.poc_named = named,
	.poc_multi_step = multi_step,
    };

    uint32_t seeded = pin_compile_ns_seed(body_node, &cur);
    pin_compile_ops_r(body_node, &cur);
    if (seeded)
	pin_compile_ns_pop(&cur, seeded);
    free(cur.poc_ns_stack);

    if (complexity_out)
	*complexity_out = cur.poc_complexity;
    return head;
}

/*
 * Compute the default priority for a single (already-split) match pattern.
 * Rules from XSLT spec section 5.5, applied in order:
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

    /* NCName:* -- one colon, ends with ":*", no other special chars before colon */
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
/*
 * Add ops that build the content of global variable "name" and bind
 * it, to the end of prb_global_ops.
 */
static void
pin_compile_global_build (xmlNodePtr child, pin_rulebook_t *rb,
			  const char *name)
{
    pin_op_id_t head = pin_op_id_null_atom();
    pin_op_cursor_t cur = {
	.poc_nextp = &head,
	.poc_rb = rb,
	.poc_src_file = pin_name_id_null_atom(),
    };

    uint32_t seeded = pin_compile_ns_seed(child, &cur);
    if (pin_compile_build(child, &cur) == 0)
	pin_compile_store_var(&cur, name, PIN_OP_STORE_VAR);
    if (seeded)
	pin_compile_ns_pop(&cur, seeded);
    free(cur.poc_ns_stack);

    if (pin_op_id_is_null(head))
	return;

    if (pin_op_id_is_null(rb->prb_global_ops)) {
	rb->prb_global_ops = head;
	return;
    }

    pin_op_t *opp = pin_op_addr(rb, rb->prb_global_ops);
    while (opp && !pin_op_id_is_null(opp->po_next))
	opp = pin_op_addr(rb, opp->po_next);
    if (opp)
	opp->po_next = head;
}

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
    if ((vsel == NULL || vsel[0] == '\0') && pin_has_element_child(child)) {
	/*
	 * Content needs ops to build it, so the binding is made at run
	 * time, by prb_global_ops.  The empty string registered below
	 * only stands in if that build fails.
	 */
	pin_compile_global_build(child, rb, (const char *) vname);
	vid = pin_namepool_atom(rb->prb_workspace, "", TRUE);
    } else if (vsel && vsel[0]) {
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
	    if (is_num) {
		vid = pin_namepool_atom(rb->prb_workspace, s, TRUE);
	    } else {
		/* Expression (e.g. "$varname"): register name now, set expr below */
		vid = pin_name_id_null_atom();
	    }
	}
    } else {
	/*
	 * No select= (or select=""):
	 * The value is the text content of the element, which may be empty.
	 * Empty string is a valid distinct value; we always intern it so
	 * pin_rulebook_global_find can return a non-null atom even for
	 * parameters whose value is "".
	 *
	 * Text body: <xsl:variable name="x">value</xsl:variable>
	 * Empty:     <xsl:param name="p"/>  or  <xsl:param name="p"></xsl:param>
	 */
	xmlChar *content = xmlNodeGetContent(child);
	const char *cstr = (content && content[0]) ? (const char *) content : "";
	vid = pin_namepool_atom(rb->prb_workspace, cstr, TRUE);
	if (content)
	    xmlFree(content);
    }
    if (!pin_name_id_is_null(nid)) {
	pin_rulebook_global_add(rb, nid, vid);
	/* If the select= was an unresolvable expression, store it for runtime */
	if (vsel && vsel[0] && pin_name_id_is_null(vid)) {
	    pin_name_id_t eid = pin_namepool_atom(rb->prb_workspace,
						  (const char *) vsel, TRUE);
	    pin_rulebook_global_set_expr(rb, nid, eid);
	}
    }
    if (vsel)
	xmlFree(vsel);
    xmlFree(vname);
    return 0;
}

/*
 * Compile one xsl:template node (either named or match-based).
 * Returns 1 if a rule or named template was registered, 0 if the template
 * was skipped or rejected without error, or -1 on a fatal error.
 */
static int
pin_compile_template (xmlNodePtr child, xmlDocPtr docp, xo_filter_t *xfp UNUSED,
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
	pin_op_id_t ops = pin_compile_ops(child, rb, src_file_id, &complexity,
					  1, 1);
	int count = 0;

	/*
	 * Named templates have no BIA_ fallback: only match-based templates
	 * get one (see below).  PIN_OP_APPLY is exempt from this check: it
	 * now has a real op-engine implementation (pin_op_apply, matching
	 * retained children against the apply-templates rule index), so a
	 * named template whose only complexity source is apply-templates
	 * compiles and runs like any other op sequence.  Literal and number
	 * copy-of, downward-path for-each and text/number sorts are op
	 * dispatchable now; what's left is a general node-set expression
	 * (copy-of's PUSH_NODE stub, or COMPLEX_EXPR from a for-each
	 * select or sort key with a variable, function call, predicate,
	 * wildcard or non-child axis).  Those still can't be compiled as
	 * a plain op sequence, so reject them instead of silently
	 * registering nothing and leaving every call to it a silent no-op
	 * at runtime.
	 */
	if (complexity & ~(1ULL << PIN_OP_APPLY)) {
	    pin_error(rb, child,
		      "xsl:template name=\"%s\": copy-of or for-each "
		      "select (or a sort key) needs a node-set expression, "
		      "which named templates do not support yet",
		      (const char *) tname);
	} else if (!pin_op_id_is_null(ops)) {
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
    /*
     * Stream where we can, buffer when we must.  A top-level
     * call-template needs the whole context element, and so do two or
     * more readers of its children (for-each, apply-templates): the
     * first would consume what the second needs.  Such a template
     * buffers: op dispatch runs at the element's close with the
     * subtree retained, and may then walk a multi-step for-each.
     *
     * pin_op_apply handles apply-templates for a top-level call, which
     * BIA_ can't run at all.  For readers alone, op dispatch is used
     * only when it can run the whole body (complexity 0): pin_op_apply
     * ignores select= and finds only rules matching a bare name, so a
     * body with apply-templates keeps its BIA_ fallback.
     */
    /*
     * And stream when we can: a body whose one reader is a for-each
     * that qualifies (see pin_template_stream_foreach()) skips op
     * dispatch, which would buffer, and goes to BIA_, whose
     * BIA_FOR_EACH streams it.  It also skips pin_compile_foreach(),
     * which streams too but drops the rest of the body.  The loop
     * body itself is compiled to ops (it must need nothing more), and
     * runs once per item as each one closes.
     */
    bool call = pin_has_toplevel_call(child);
    bool readers = pin_count_readers(child) >= 2;
    bool stream = false;

    xmlNodePtr stream_fe = pin_template_stream_foreach(child);
    if (stream_fe != NULL) {
	uint64_t fe_complexity = 0;
	pin_op_id_t fe_ops = pin_compile_ops(stream_fe, rb, src_file_id,
					     &fe_complexity, 0, 0);
	if (fe_complexity == 0 && !pin_op_id_is_null(fe_ops)) {
	    rb->prb_stream_fe = stream_fe;
	    rb->prb_stream_ops = fe_ops;
	    stream = true;
	}
    }

    uint64_t complexity = 0;
    if (!stream)
	prp->pr_close_ops = pin_compile_ops(child, rb, src_file_id,
					     &complexity, 0, call || readers);

    if (complexity == (1ULL << PIN_OP_APPLY) && call)
	complexity = 0;

    if (complexity != 0 || pin_op_id_is_null(prp->pr_close_ops)) {
	pin_rstate_id_t foreach_sid = stream ? pin_rstate_id_null_atom()
	    : pin_compile_foreach(child, rb);
	if (!pin_rstate_id_is_null(foreach_sid)) {
	    prp->pr_action = PIA_SAVE;
	    prp->pr_new_state = foreach_sid;
	    prp->pr_close_ops = pin_op_id_null_atom();
	} else {
	    pin_body_instr_id_t body_head = pin_compile_body(child, rb, 0);
	    rb->prb_stream_fe = NULL;
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
     *
     * A simple absolute single-step path (e.g. match="/authors") is an
     * anchored pattern too: it can only ever match the document's single
     * root element, so it must never be added to the shared per-element
     * xo_filter alongside unanchored apply-templates candidate patterns
     * (e.g. "author[life-span/born]").  That filter reports XO_STATUS_FULL
     * for "the whole matched subtree", and once an absolute-path pattern
     * like "/authors" goes FULL at depth 0, that status (and its action id)
     * would incorrectly persist for every descendant element, masking the
     * real per-name/per-predicate dispatch decisions apply-templates needs
     * to make for its children.  So treat it the same as match="/": store
     * it (plus the expected root name) in prb_root_rule/prb_root_name and
     * fire it manually in pin_parse, rather than registering it as a
     * streaming filter pattern.
     */
    if (match_str[0] == '/' && match_str[1] == '\0') {
	rb->prb_root_rule = rid;
	rb->prb_root_name = pin_name_id_null_atom();
	xmlFree(match);
	return 1;
    }

    if (match_str[0] == '/' && match_str[1] != '\0') {
	const char *namep = match_str + 1;
	size_t i;
	psu_boolean_t simple = TRUE;
	for (i = 0; namep[i] != '\0'; i++) {
	    int ch = (unsigned char) namep[i];
	    if (ch == '/' || ch == '[' || ch == ']' || ch == '|'
		    || ch == '@' || ch == '*' || ch == '(' || ch == ')') {
		simple = FALSE;
		break;
	    }
	}
	if (simple && i > 0) {
	    rb->prb_root_rule = rid;
	    rb->prb_root_name = pin_namepool_atom(rb->prb_workspace, namep, TRUE);
	    xmlFree(match);
	    return 1;
	}
    }

    /*
     * Register the pattern by (mode, priority, import_prec) instead of
     * compiling it into a filter directly; actual xpath syntax validation
     * is deferred to pin_compile_build_mode_filters, once every template in
     * the stylesheet (and its includes/imports) has been walked and modes
     * are fully known.
     */
    int rc = pin_rulebook_mode_register(rb, mode_id, match_str, rid,
					priority, import_prec);
    if (rc < 0) {
	pin_error(rb, child, "xsl:template: could not register match pattern: %s",
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
 * Walks one stylesheet document's top-level children, compiling templates,
 * variables, includes/imports (recursively), and output-related elements.
 * This is the shared body used both by the public pin_compile entry point
 * and by pin_compile_include/pin_compile_import for nested documents; only
 * the outermost pin_compile call goes on to call
 * pin_compile_build_mode_filters, once every include/import in the whole
 * compilation unit has contributed its match patterns.
 */
static int
pin_compile_walk (xmlDocPtr docp, xo_filter_t *xfp, pin_rulebook_t *rb,
		  pin_action_type_t action, int16_t import_prec);

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
    if (href == NULL) {
	pin_error(rb, child, "xsl:include: missing href attribute");
	return 0;
    }
    xmlChar *url = xmlBuildURI(href, docp->URL);
    if (url == NULL) {
	pin_error(rb, child, "xsl:include: unable to resolve href '%s'",
		  (const char *) href);
	xmlFree(href);
	return 0;
    }
    xmlFree(href);
    xmlDocPtr inc = xmlReadFile((const char *) url, NULL,
				XML_PARSE_NOENT | XML_PARSE_NONET);
    if (inc == NULL) {
	pin_error(rb, child, "xsl:include: unable to load '%s'",
		  (const char *) url);
	xmlFree(url);
	return 0;
    }
    xmlFree(url);
    int count = pin_compile_walk(inc, xfp, rb, action, import_prec);
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
    if (href == NULL) {
	pin_error(rb, child, "xsl:import: missing href attribute");
	return 0;
    }
    xmlChar *url = xmlBuildURI(href, docp->URL);
    if (url == NULL) {
	pin_error(rb, child, "xsl:import: unable to resolve href '%s'",
		  (const char *) href);
	xmlFree(href);
	return 0;
    }
    xmlFree(href);
    xmlDocPtr imp = xmlReadFile((const char *) url, NULL,
				XML_PARSE_NOENT | XML_PARSE_NONET);
    if (imp == NULL) {
	pin_error(rb, child, "xsl:import: unable to load '%s'",
		  (const char *) url);
	xmlFree(url);
	return 0;
    }
    xmlFree(url);
    int count = pin_compile_walk(imp, xfp, rb, action, (int16_t)(import_prec - 1));
    xmlFreeDoc(imp);
    return (count > 0) ? count : 0;
}

static void
pin_compile_output_attribute (pin_workspace_t *pwp, xmlNodePtr child,
			      const char *name, pin_name_id_t *idp)
{
    if (!pin_name_id_is_null(*idp)) /* Already set */
	return;

    xmlChar *value = xmlGetProp(child, (const xmlChar *) name);
    if (value == NULL)
	return;

    *idp = pin_namepool_atom(pwp, (const char *) value, TRUE);
    xmlFree(value);
}

static int
pin_compile_output_yes (xmlNodePtr child, const char *name, int *valuep)
{
    xmlChar *value = xmlGetProp(child, (const xmlChar *) name);
    if (value) {
	*valuep = (strcmp((const char *) value, "yes") == 0) ? 1 : 0;
	xmlFree(value);
	return 1;
    }

    *valuep = 0;		/* Just to be safe */
    return 0;
}

/*
 * Compile an xsl:output element: read its attributes and store them
 * in rb->prb_output.  Higher import-precedence sheets' output settings
 * take priority; since pin_compile calls us outermost-first, later
 * calls are from lower-precedence included/imported sheets, so we only
 * overwrite settings that have not been set yet.
 */
static int
pin_compile_strip_space (xmlNodePtr child, pin_rulebook_t *rb)
{
    int val;
    int shallow = pin_compile_output_yes(child, "shallow", &val) && val;

    xmlChar *elements = xmlGetProp(child, (const xmlChar *) "elements");
    if (elements == NULL)
	return 0;

    if (strcmp((const char *) elements, "*") == 0) {
	if (shallow)
	    rb->prb_shallow_all = 1;
	else
	    rb->prb_strip_all = 1;
    } else {
	const char *p = (const char *) elements;
	while (*p) {
	    while (*p && isspace((unsigned char) *p))
		p += 1;
	    if (*p == '\0')
		break;
	    const char *start = p;
	    while (*p && !isspace((unsigned char) *p))
		p += 1;
	    size_t namelen = (size_t) (p - start);
	    char buf[256];
	    if (namelen < sizeof(buf)) {
		memcpy(buf, start, namelen);
		buf[namelen] = '\0';
		pin_name_id_t nid = pin_namepool_atom(rb->prb_workspace, buf, TRUE);
		pin_rulebook_strip_name_add(rb, nid, shallow);
	    }
	}
    }
    xmlFree(elements);
    return 0;
}

/*
 * xsl:preserve-space: for now only logs a note; full element-list tracking
 * can be added when needed.
 */
static int
pin_compile_preserve_space (xmlNodePtr child UNUSED, pin_rulebook_t *rb UNUSED)
{
    return 0;
}

static int
pin_compile_output (xmlNodePtr child, pin_rulebook_t *rb)
{
    pin_workspace_t *pwp = rb->prb_workspace;
    pin_output_settings_t *op = &rb->prb_output;

    pin_compile_output_attribute(pwp, child, "method", &op->pos_method);
    pin_compile_output_attribute(pwp, child, "encoding", &op->pos_encoding);

    int val;

    if (!op->pos_omit_xml_decl_set
	    && pin_compile_output_yes(child, "omit-xml-declaration", &val)) {
	op->pos_omit_xml_decl = val;
	op->pos_omit_xml_decl_set = 1;
    }

    if (!op->pos_indent_set
	    && pin_compile_output_yes(child, "indent", &val)) {
	op->pos_indent = val;
	op->pos_indent_set = 1;
    }

    pin_compile_output_attribute(pwp, child, "doctype-public",
				 &op->pos_doctype_public);
    pin_compile_output_attribute(pwp, child, "doctype-system",
				 &op->pos_doctype_system);
    pin_compile_output_attribute(pwp, child, "media-type", &op->pos_media_type);

    return 0;
}

static int
pin_compile_walk (xmlDocPtr docp, xo_filter_t *xfp, pin_rulebook_t *rb,
		  pin_action_type_t action, int16_t import_prec)
{
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
	else if (pin_is_xsl(child, "output"))
	    rc = pin_compile_output(child, rb);
	else if (pin_is_xsl(child, "strip-space"))
	    rc = pin_compile_strip_space(child, rb);
	else if (pin_is_xsl(child, "preserve-space"))
	    rc = pin_compile_preserve_space(child, rb);
	else
	    continue;
	if (rc < 0)
	    return -1;
	count += rc;
    }

    return count;
}

/*
 * Drains rb->prb_mode_entries (populated by pin_rulebook_mode_register while
 * walking xsl:template match= patterns) into actual xo_filter_t instances:
 * default-mode entries go into the caller-supplied default_xfp, and each
 * distinct named mode gets its own freshly created filter, reused across all
 * entries that share that mode and recorded via pin_rulebook_set_mode_filter
 * so pin_parse can find it later by mode id.
 *
 * xpath syntax validation was deferred until now (pin_rulebook_mode_register
 * itself does not validate); a bad pattern is reported here via pin_error
 * (which increments pw_errors) and compilation continues so that every bad
 * pattern in the stylesheet is reported, not just the first. Allocation
 * failures (out of memory) are fatal and return -1 immediately.
 */
static int
pin_compile_build_mode_filters (pin_rulebook_t *rb, pin_workspace_t *pwp,
				xo_filter_t *default_xfp)
{
    pin_mode_entry_t *ep = NULL;

    while ((ep = pin_rulebook_mode_next(rb, ep)) != NULL) {
	xo_filter_t *xfp;

	if (pin_name_id_is_null(ep->pme_mode)) {
	    xfp = default_xfp;
	} else {
	    xfp = pin_rulebook_get_mode_filter(rb, ep->pme_mode);
	    if (xfp == NULL) {
		xfp = pin_filter_create(NULL, pwp);
		if (xfp == NULL) {
		    psu_log("pin_compile: pin_filter_create failed for mode '%s'",
			    pin_namepool_string(pwp, ep->pme_mode));
		    return -1;
		}
		if (pin_rulebook_set_mode_filter(rb, ep->pme_mode, xfp) < 0) {
		    xo_filter_destroy_standalone(xfp);
		    return -1;
		}
	    }
	}

	const char *xpath = pin_namepool_string(pwp, ep->pme_xpath_id);
	int rc = pin_filter_add_with_action(xfp, xpath, ep->pme_rule,
					    (double) ep->pme_priority,
					    ep->pme_import_prec);
	if (rc < 0) {
	    pin_rule_t *prp = pin_rulebook_rule(rb, ep->pme_rule);
	    pin_error(rb, NULL,
		     "xsl:template: unsupported match pattern: %s (%s:%u)",
		     xpath,
		     prp ? pin_namepool_string(pwp, prp->pr_src_file) : "?",
		     prp ? prp->pr_src_line : 0);
	}
    }

    return 0;
}

/*
 * Checks every recorded xsl:call-template site against the named-template
 * registry, now that the whole compilation unit (all includes/imports) has
 * finished compiling.  A name that still doesn't resolve is a genuine
 * error -- a misspelled name, or a target that was rejected for having an
 * unsupported body -- and the caller needs to see it, not have the call
 * silently do nothing at runtime.  Like pin_compile_build_mode_filters,
 * every bad call is reported, not just the first.
 */
static void
pin_compile_validate_calls (pin_rulebook_t *rb)
{
    pin_workspace_t *pwp = rb->prb_workspace;

    for (uint32_t i = 0; i < rb->prb_pending_call_count; i++) {
	pin_pending_call_t *pcp = &rb->prb_pending_calls[i];
	if (pin_rulebook_named_exists(rb, pcp->pc_name))
	    continue;

	pin_error(rb, NULL,
		 "xsl:call-template: no template named '%s' (%s:%u)",
		 pin_namepool_string(pwp, pcp->pc_name),
		 pin_namepool_string(pwp, pcp->pc_src_file),
		 pcp->pc_src_line);
    }
}

int
pin_compile (xmlDocPtr docp, xo_filter_t *xfp, pin_rulebook_t *rb,
		  pin_action_type_t action, int16_t import_prec)
{
    if (docp == NULL || xfp == NULL || rb == NULL)
	return -1;

    int count = pin_compile_walk(docp, xfp, rb, action, import_prec);
    if (count < 0)
	return -1;

    if (pin_compile_build_mode_filters(rb, rb->prb_workspace, xfp) < 0)
	return -1;

    pin_compile_validate_calls(rb);

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
