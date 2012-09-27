/*
 * $Id: slaxwriter.c,v 1.3 2006/11/01 21:27:20 phil Exp $
 *
 * Copyright (c) 2006-2011, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * This file contains code that recursively descends an XSLT document,
 * emitting SLAX code that is functionaly equivalent.  Most of the work
 * is dull and most of the arguments are identical (and are often
 * unused), so the level of comments here is decidedly lower.
 *
 * One big item to be aware of is that the SWF_VERS_10 is used to
 * limit slaxwriter to avoid 1.1 (and later) features.
 */

#include "slaxinternals.h"
#include <libslax/slax.h>
#include <libexslt/exslt.h>
#include "slaxparser.h"

#define BUF_EXTEND 2048		/* Bump the buffer by this amount */

typedef struct slax_writer_s {
    const char *sw_filename;	/* Filename being parsed */
    slaxWriterFunc_t sw_write;	/* Client callback */
    void *sw_data;		/* Client data */
    int sw_indent;		/* Indentation count */
    char *sw_buf;		/* Output buffer */
    int sw_cur;			/* Current index in output buffer */
    int sw_bufsiz;		/* Size of the buffer in sw_buf[] */
    int sw_errors;		/* Errors reading or writing data */
    unsigned sw_flags;		/* Flags for this instance (SWF_*) */
} slax_writer_t;

/* Flags for sw_flags */
#define SWF_BLANKLINE	(1<<0)	/* Just wrote a blank line */
#define SWF_FORLOOP	(1<<1)	/* Just wrote a "for" loop */
#define SWF_VERS_10	(1<<2)	/* Version 1.0 features only */

static const char slaxVarNsCall[] = EXT_PREFIX ":node-set(";
static char slaxForVariablePrefix[] = FOR_VARIABLE_PREFIX;
static char slaxTernaryPrefix[] = SLAX_TERNARY_PREFIX;

static inline int
slaxV10 (slax_writer_t *swp)
{
    return (swp->sw_flags & SWF_VERS_10) ? TRUE : FALSE;
}

/* Forward function declarations */
static void slaxWriteChildren(slax_writer_t *, xmlDocPtr, xmlNodePtr, int);
static void slaxWriteXslElement(slax_writer_t *swp, xmlDocPtr docp,
				xmlNodePtr nodep, int *statep);
static void slaxWriteSort (slax_writer_t *, xmlDocPtr, xmlNodePtr);
static void slaxWriteCommentStatement (slax_writer_t *, xmlDocPtr, xmlNodePtr);

static int slaxIndent = 4;
static const char *slaxSpacesAroundAttributeEquals = "";

#define NEWL_INDENT	1
#define NEWL_OUTDENT	-1

void
slaxSetIndent (int indent)
{
    slaxIndent = indent;
}

void
slaxSetSpacesAroundAttributeEquals (int spaces)
{
    slaxSpacesAroundAttributeEquals = spaces ? " " : "";
}

static int
slaxWriteNewline (slax_writer_t *swp, int change)
{
    int rc;

    if (swp->sw_buf == NULL)
	return 0;

    if (swp->sw_cur == 0)
	swp->sw_flags |= SWF_BLANKLINE;
    else swp->sw_flags &= ~SWF_BLANKLINE;

    if (change < 0)
	swp->sw_indent += change;
    rc = (*swp->sw_write)(swp->sw_data, "%*s%s\n",
			  swp->sw_indent * slaxIndent, "", swp->sw_buf);
    if (rc < 0)
	swp->sw_errors += 1;

    if (change > 0) {
	swp->sw_indent += change;
	swp->sw_flags |= SWF_BLANKLINE; /* Indenting counts as a blank line */
    }

    swp->sw_cur = 0;
    swp->sw_buf[0] = '\0';
    return rc;
}

static int
slaxWriteBlankline (slax_writer_t *swp)
{
    if (swp->sw_flags & SWF_BLANKLINE)
	return 0;

    return slaxWriteNewline(swp, 0);
}

static char *
slaxWriteRealloc (slax_writer_t *swp, int need)
{
    char *cp;

    cp = xmlRealloc(swp->sw_buf, need);
    if (cp == NULL) {
	slaxLog("memory allocation failure");
	swp->sw_errors += 1;
	return NULL;
    }
    
    swp->sw_buf = cp;
    swp->sw_bufsiz = need;
    return cp;
}

static int
slaxIsXsl (xmlNodePtr nodep)
{
    if (nodep && nodep->ns && nodep->ns->href
		&& streq((const char *) nodep->ns->href, XSL_URI))
	return TRUE;
    return FALSE;
}

static void
slaxWrite (slax_writer_t *swp, const char *fmt, ...)
{
    int rc = 0, len;
    char *cp;
    va_list vap;

    for (;;) {
	if (swp->sw_bufsiz != 0) {
	    len = swp->sw_bufsiz - swp->sw_cur;
	    cp = swp->sw_buf + swp->sw_cur;

	    va_start(vap, fmt);
	    rc = vsnprintf(cp, len, fmt, vap);
	    va_end(vap);

	    if (rc <= len) {
		swp->sw_cur += rc;
		break;
	    }
	}

	len = swp->sw_cur + rc + BUF_EXTEND;
	if (slaxWriteRealloc(swp, len) == NULL)
	    return;
    }
}

/*
 * Search a space seperated list of namespaces for a given one
 */
static int
slaxNsIsMember (const xmlChar *prefix_xmlchar, const char *list)
{
    const char *prefix = (const char *) prefix_xmlchar;
    static const char delims[] = " \t\n";
    const char *cp, *next;
    int len = strlen(prefix);

    for (cp = list; *cp; cp = next) {
	next = cp + strcspn(cp, delims);
	if (len == next - cp && *prefix == *cp
	        && strncmp(prefix, cp, next - cp) == 0)
	    return TRUE;

	/*
	 * If we're not at the end of the string, more over any whitespace
	 */
	if (*next)
	    next += strspn(next, delims);
    }

    return FALSE;
}

static int
slaxIsWhiteString (const xmlChar *str)
{
    for ( ; *str; str++)
	if (*str != ' ' && *str != '\t' && *str != '\r'  && *str != '\n')
	    return FALSE;

    return TRUE;
}

/*
 * We reserve all prefixes, valiable names, etc starting with 'slax'
 */
static int
slaxIsReserved (const char *name)
{
    return (name
	    && name[0] == 's'
	    && name[1] == 'l'
	    && name[2] == 'a'
	    && name[3] == 'x');
}

static void
slaxWriteNamespaceAlias (slax_writer_t *swp, xmlDocPtr docp UNUSED,
			 xmlNodePtr nodep)
{
    char *alias = slaxGetAttrib(nodep, ATT_STYLESHEET_PREFIX);
    char *results = slaxGetAttrib(nodep, ATT_RESULT_PREFIX);

    slaxWrite(swp, "ns-alias %s %s;",
	      alias ?: UNKNOWN_EXPR, results ?: UNKNOWN_EXPR);
    slaxWriteNewline(swp, 0);

    xmlFreeAndEasy(alias);
    xmlFreeAndEasy(results);
}

static void
slaxWriteAllNs (slax_writer_t *swp, xmlDocPtr docp UNUSED, xmlNodePtr nodep)
{
    char *excludes = slaxGetAttrib(nodep, ATT_EXCLUDE_RESULT_PREFIXES);
    char *extensions = slaxGetAttrib(nodep, ATT_EXTENSION_ELEMENT_PREFIXES);
    int hit = 0;
    xmlNodePtr childp;
    xmlNsPtr cur;

    for (cur = nodep->nsDef; cur; cur = cur->next) {
	if (cur->href && streq((const char *) cur->href, XSL_URI))
	    continue;

	if (slaxIsReserved((const char *) cur->prefix))
	    continue;

	if (cur->prefix) {
	    const char *tag1 = "";
	    const char *tag2 = "";

	    slaxWrite(swp, "ns %s ", cur->prefix);
	    if (excludes && slaxNsIsMember(cur->prefix, excludes))
		tag1 = "exclude ";
	    if (extensions && slaxNsIsMember(cur->prefix, extensions))
		tag2 = "extension ";
	    slaxWrite(swp, "%s%s= \"%s\";", tag1, tag2, cur->href);
	} else slaxWrite(swp, "ns \"%s\";", cur->href);

	slaxWriteNewline(swp, 0);
	hit += 1;
    }

    if (!slaxV10(swp)) {
	for (childp = nodep->children; childp; childp = childp->next) {
	    if (childp->type ==  XML_ELEMENT_NODE
		&& slaxIsXsl(childp)
		&& streq((const char *) childp->name, "namespace-alias")) {

		slaxWriteNamespaceAlias(swp, docp, childp);
		hit += 1;
	    }
	}
    }

    if (hit)
	slaxWriteNewline(swp, 0);

    xmlFreeAndEasy(excludes);
    xmlFreeAndEasy(extensions);
}

/*
 * slaxIsDisableOutputEscaping:
 *   See if the "disable-output-escaping" attribute is set to "yes".
 * SLAX uses quoted text to represent text data (i.e. "foo") and
 * inline XPath expressions.  But if the disable-output-escaping attribute
 * is set, we don't want to do this, since it'll wreck everyone's fun.
 * So we test for it and the caller should do something sensible if we
 * return TRUE.
 */
static int
slaxIsDisableOutputEscaping (xmlNodePtr nodep)
{
    int rc;
    char *dis = slaxGetAttrib(nodep, ATT_DISABLE_OUTPUT_ESCAPING);
    if (dis == NULL)
	return FALSE;

    rc = streq(dis, "yes");

    xmlFreeAndEasy(dis);
    return rc;
}

static char *
slaxWriteCheckRoom (slax_writer_t *swp, int space)
{
    if (swp->sw_cur + space >= swp->sw_bufsiz)
	if (slaxWriteRealloc(swp, swp->sw_cur + space + BUF_EXTEND) == NULL)
	    return NULL;

    return swp->sw_buf + swp->sw_cur;
}

#define SEF_NEWLINE	(1<<0)	/* Escape newlines */
#define SEF_DOUBLEQ	(1<<1)	/* Escape double quotes */
#define SEF_SINGLEQ	(1<<2)	/* Escape single quotes */

#define SEF_ATTRIB	(SEF_NEWLINE | SEF_DOUBLEQ)
#define SEF_TEXT	(SEF_NEWLINE | SEF_DOUBLEQ)

/*
 * Write an escaped character to the output file.  Note that this
 * is "slax-escaped", which means C-style backslashed characters.
 */
static const char *
slaxWriteEscapedChar (slax_writer_t *swp, const char *inp, unsigned flags)
{
    char *outp;
    unsigned char ch = *inp;

    switch (ch) {
    case '\n':
	if (!(flags & SEF_NEWLINE))
	    goto escape_one;
	ch = 'n';
	goto escape_two;

    case '\r':
	ch = 'r';
	goto escape_two;

    case '\"':
	if (!(flags & SEF_DOUBLEQ))
	    goto escape_one;
	/* fallthru */

    case '\\':
    escape_two:
	outp = slaxWriteCheckRoom(swp, 2);
	if (outp == NULL)
	    return NULL;
	*outp++ = '\\';
	*outp++ = ch;
	swp->sw_cur += 2;
	break;

    case '\'':
	if (flags & SEF_SINGLEQ)
	    goto escape_two;

    default:
    escape_one:
	outp = slaxWriteCheckRoom(swp, 1);
	if (outp == NULL)
	    return NULL;
	*outp++ = ch;
	swp->sw_cur += 1;
    }

    return inp;
}

static void
slaxWriteEscaped (slax_writer_t *swp, const char *str, unsigned flags)
{
    const char *inp;
    char *outp;

    outp = slaxWriteCheckRoom(swp, 1); /* Save room for trailing NUL */
    if (outp == NULL)
	return;

    for (inp = str; *inp; inp++)
	inp = slaxWriteEscapedChar(swp, inp, flags);
}

static void
slaxWriteExpr (slax_writer_t *swp, xmlChar *content,
	       int initializer, int disable_escaping)
{
    slaxWrite(swp, "%s\"",
	      initializer ? ""
	      : (!slaxV10(swp) && disable_escaping) ? "uexpr " : "expr ");
    slaxWriteEscaped(swp, (char *) content, SEF_TEXT);
    slaxWrite(swp, "\";");
    slaxWriteNewline(swp, 0);
}

static void
slaxWriteText (slax_writer_t *swp, xmlDocPtr docp UNUSED, xmlNodePtr nodep)
{
    xmlNodePtr childp;
    int disable_escaping = slaxIsDisableOutputEscaping(nodep);

    for (childp = nodep->children; childp; childp = childp->next) {
	if (childp->type == XML_TEXT_NODE)
	    slaxWriteExpr(swp, childp->content, FALSE, disable_escaping);
    }
}

static int
slaxNeedsBraces (xmlNodePtr nodep)
{
    xmlNodePtr childp;
    int hits = 0;

    if (nodep->nsDef)
	return TRUE;

    for (childp = nodep->children; childp; childp = childp->next) {
	if (childp->type == XML_TEXT_NODE) {
	    if (!slaxIsWhiteString(childp->content))
		if (hits++)
		    return TRUE;
	    continue;
	}

	if (childp->type == XML_ELEMENT_NODE && slaxIsXsl(childp)) {

	    if (streq((const char *) childp->name, ELT_VALUE_OF)
		    || streq((const char *) childp->name, ELT_TEXT)) {
		if (slaxIsDisableOutputEscaping(childp))
		    return TRUE;

		if (hits++)
		    return TRUE;
		continue;
	    }
	}

	/*
	 * Anything else requires a level of braces
	 */
	return TRUE;
    }

    return FALSE;
}

/*
 * slaxNeedsBlock:
 *   Determine if the node needs a containing block (braces)
 * nodep: the node to inspect
 * Returns TRUE if the node needs a block
 */
static int
slaxNeedsBlock (xmlNodePtr nodep)
{
    xmlNodePtr childp;
    int count = 0;

    if (nodep->nsDef)
	return TRUE;

    for (childp = nodep->children; childp; childp = childp->next) {
	if (childp->type == XML_TEXT_NODE) {
	    if (slaxIsWhiteString(childp->content))
		continue;
	    if (count++)
		return TRUE;
	}

	if (childp->type == XML_ELEMENT_NODE) {
	    /*
	     * If this is an XSLT element, we need to bust it out
	     */
	    if (slaxIsXsl(childp))
		return TRUE;

	    /*
	     * If there are multiple elements, they'll need a block
	     */
	    if (count++)
		return TRUE;
	}
    }

    return FALSE;
}

static inline void
slaxWriteValue (slax_writer_t *swp, const char *value)
{
    /*
     * This is a remnant from when slaxWriteValue handled concat()
     * expansion, and should shortly be removed altogether.
     */
    slaxWrite(swp, "%s", value);
}

/*
 * Turn an XPath expression into a SLAX one.  Returns a freshly
 * allocated string, or NULL.
 */
static slax_string_t *
slaxMakeExpressionString (slax_writer_t *swp, xmlNodePtr nodep,
			  const char *xpath)
{
    slax_data_t sd;
    int rc;
    xmlParserCtxtPtr ctxt;
    xmlNodePtr fakep = NULL;
    char *buf;

    if (xpath == NULL || *xpath == '\0')
	return NULL;

    ctxt = xmlNewParserCtxt();
    if (ctxt == NULL)
	return NULL;

    bzero(&sd, sizeof(sd));
    sd.sd_parse = sd.sd_ttype = M_PARSE_XPATH; /* We want to parse XPath */
    sd.sd_ctxt = ctxt;
    sd.sd_flags |= SDF_NO_SLAX_KEYWORDS;

    /* sd_nodep is used for ternary expression, which aren't in SLAX-1.0 */
    if (!slaxV10(swp))
	sd.sd_nodep = nodep;

    ctxt->version = xmlCharStrdup(XML_DEFAULT_VERSION);
    ctxt->userData = &sd;

    /*
     * Fake up an inputStream so the error mechanisms will work
     */
    xmlSetupParserForBuffer(ctxt, (const xmlChar *) "", swp->sw_filename);
    if (nodep)
	sd.sd_line = nodep->line;

    fakep = xmlNewNode(NULL, (const xmlChar *) ELT_STYLESHEET);
    if (fakep == NULL)
	goto fail;

    sd.sd_xsl_ns = xmlNewNs(fakep, (const xmlChar *) XSL_URI,
			    (const xmlChar *) XSL_PREFIX);
    xmlSetNs(fakep, sd.sd_xsl_ns); /* Noop if NULL */

    nodePush(ctxt, fakep);

    sd.sd_len = strlen(xpath);
    buf = alloca(sd.sd_len + 1);
    if (buf == NULL)		/* Should not occur */
	goto fail;
    memcpy(buf, xpath, sd.sd_len + 1);
    sd.sd_buf = buf;

    rc = slaxParse(&sd);

    fakep = nodePop(ctxt);
    if (fakep)
	xmlFreeNode(fakep);

    xmlFreeParserCtxt(ctxt);
    ctxt = NULL;

    if (sd.sd_errors) {
	swp->sw_errors += sd.sd_errors;
	xmlParserError(ctxt, "%s: %d error%s detected during parsing (%d)\n",
	     sd.sd_filename, sd.sd_errors, (sd.sd_errors == 1) ? "" : "s", rc);
	goto fail;
    }

    if (sd.sd_xpath == NULL) {
	/*
	 * Something's wrong so we punt and return the original string
	 */
	slaxLog("slax: xpath conversion failed: nothing returned");
	swp->sw_errors += 1;
	goto fail;
    }

    return sd.sd_xpath;

fail:
    if (ctxt)
	xmlFreeParserCtxt(ctxt);

    return NULL;
}

/*
 * Turn an XPath expression into a SLAX one.  Returns a freshly
 * allocated buffer, or NULL.
 */
static char *
slaxMakeExpression (slax_writer_t *swp, xmlNodePtr nodep, const char *xpath)
{
    slax_string_t *ssp;
    char *buf;

    ssp = slaxMakeExpressionString(swp, nodep, xpath);
    if (ssp == NULL) {
fail:
	/*
	 * SLAX XPath expressions are completely backwards compatible
	 * with XSLT, so we can make an ugly version of the expression
	 * by simply returning a copy of our input.  You get "and" instead
	 * of "&&", but it's safe enough.
	 */
	return (char *) xmlCharStrdup(xpath);
    }

    buf = slaxStringAsChar(ssp, SSF_QUOTES | SSF_ESCAPE);

    if (buf == NULL) {
	slaxLog("slax: xpath conversion failed: no buffer");
	goto fail;
    }

    slaxLog("slax: xpath conversion: %s", buf);
    slaxStringFree(ssp);

    return buf;
}

/*
 * Turn an attribute value template into a SLAX expression.  The
 * template can contain three things:
 *    literal strings:  "foo"
 *    XPath expressions in curly braces:  "{$foo+one}"
 *    Escaped braces: "{{" == "{", "}}" = "}"
 * Put this together and you get: "one{$two}{three[four = five]}{'six'}{{}}"
 */
static char *
slaxMakeAttribValueTemplate (slax_writer_t *swp, xmlNodePtr nodep,
			     const char *value)
{
    slax_string_t *first = NULL, **tail = &first;

    char *buf = NULL, *endp, *sbuf;
    size_t slen = 0, blen;
    char *start, *cur;

    blen = strlen(value);
    if (blen == 0)
	return (char *) xmlCharStrdup("\"\"");

    buf = alloca(blen + 1);
    if (buf == NULL)
	return NULL;

    endp = buf + blen;
    memcpy(buf, value, blen + 1);

    for (start = cur = buf; cur < endp; cur++) {
	if (*cur != '{')
	    continue;

	if (cur[1] == '{') {
	    memmove(cur + 1, cur + 2, endp-- - (cur + 2));
	    continue;
	}

	/*
	 * We're at the start of a sub-expression, and 'cur' points
	 * to the open brace.  Turn this into a string segment at
	 * the end of the string we're building.
	 */
	if (*start != '\0' && start != cur) {
	    if (slaxStringAddTail(&tail, first, start, cur - start, T_QUOTED))
		goto fail;
	}

	start = ++cur;		/* Save the start of the expression */

	/*
	 * Look for the closing brace.  This is not easy, since the spec
	 * says:
	 *
	 *    When an attribute value template is instantiated, a double
	 *    left or right curly brace outside an expression will be
	 *    replaced by a single curly brace. It is an error if a right
	 *    curly brace occurs in an attribute value template outside
	 *    an expression without being followed by a second right
	 *    curly brace. A right curly brace inside a Literal in an
	 *    expression is not recognized as terminating the expression.
	 *
	 * We could pass the whole expression into the XPath parser and
	 * let it find the closing brace, but that's an emmense pain.
	 * Instead, we track quotes and avoid terminating the expression
	 * for quoted close braces.
   	 */
	for (; cur < endp; cur++) {
	    if (*cur == '}') /* Found the close brace */
		break;

	    if (*cur == '\'' || *cur == '\"') {
		int quotes = *cur;

		for (cur++; cur < endp; cur++)
		    if (*cur == quotes)
			break;
	    }
	}

	/*
	 * We're now looking at the end of a sub-expression, with
	 * 'cur' pointing at the end and 'start' pointing at the
	 * beginning.
	 *
	 * At this point, we know we're looking at an XPath expression.
	 * We make a copy of this expression (so we can NUL terminate it)
	 * and call slaxMakeExpression to rebuild the expression in
	 * SLAX style.
	 */
        slen = cur - start;
	sbuf = alloca(slen + 1);
	memcpy(sbuf, start, slen);
	sbuf[slen] = '\0';

	sbuf = slaxMakeExpression(swp, nodep, sbuf);

	if (sbuf == NULL
	    || slaxStringAddTail(&tail, first, sbuf, strlen(sbuf), M_XPATH))
	    goto fail;

	xmlFree(sbuf);

	start = cur + 1;
    }

    /* If there's any trailing text, add it */
    if (*start != '\0' && start != cur) {

	/* Look for escaped closing braces */
	for (cur = start; cur < endp; cur++)
	    if (cur[0] == '}' && cur[1] == '}')
		memmove(cur + 1, cur + 2, endp-- - (cur + 2));

	if (start != endp
	    && slaxStringAddTail(&tail, first, start, endp - start, T_QUOTED))
	    goto fail;
    }

    buf = slaxStringAsChar(first, SSF_QUOTES);

    slaxStringFree(first);

    return buf;
    
 fail:

    if (first)
	slaxStringFree(first);

    return NULL;
}

static void
slaxWriteContent (slax_writer_t *swp, xmlDocPtr docp UNUSED, xmlNodePtr nodep)
{
    xmlNodePtr childp;
    int first = TRUE;

    for (childp = nodep->children; childp; childp = childp->next) {

	if (childp->type == XML_TEXT_NODE) {
	    if (!slaxIsWhiteString(childp->content)) {
		if (!first)
		    slaxWrite(swp, " _ ");
		else first = FALSE;
		slaxWrite(swp, "\"");
		slaxWriteEscaped(swp, (char *) childp->content, SEF_ATTRIB);
		slaxWrite(swp, "\"");
	    }
	    continue;
	}

	if (childp->type == XML_ELEMENT_NODE && slaxIsXsl(childp)) {

	    if (streq((const char *) childp->name, ELT_VALUE_OF)) {
		char *sel = slaxGetAttrib(childp, ATT_SELECT);
		if (sel) {
		    char *expr;

		    /* Expand the XSLT-style XPath to SLAX-style */
		    expr = slaxMakeExpression(swp, nodep, sel);

		    if (!first)
			slaxWrite(swp, " _ ");
		    else first = FALSE;
		    slaxWriteValue(swp, expr ?: UNKNOWN_EXPR);
		    xmlFreeAndEasy(expr);
		    xmlFreeAndEasy(sel);
		}

	    } else if (streq((const char *) childp->name, ELT_TEXT)) {
		xmlNodePtr gcp;

		for (gcp = childp->children; gcp; gcp = gcp->next)
		    if (gcp->type == XML_TEXT_NODE
			&& !slaxIsWhiteString(gcp->content)) {
			if (!first)
			    slaxWrite(swp, " _ ");
			else first = FALSE;
			slaxWrite(swp, "\"%s\"", gcp->content);
		    }
	    }
	}
    }
}

static void
slaxWriteElement (slax_writer_t *swp, xmlDocPtr docp, xmlNodePtr nodep)
{
    const char *pref = NULL;

    if (nodep->ns && nodep->ns->prefix)
	pref = (const char *) nodep->ns->prefix;

    slaxWrite(swp, "<%s%s%s", pref ?: "", pref ? ":" : "", nodep->name);

    if (nodep->properties) {
	xmlAttrPtr attrp;
	for (attrp = nodep->properties; attrp; attrp = attrp->next) {
	    if (attrp->children && attrp->children->content) {
		char *content = (char *) attrp->children->content;

		/*
		 * The attribute might be an attribute value template,
		 * which xslt uses as a simple concatenation and xpath
		 * interpretation mechanism.  We need to recode it
		 * according to SLAX syntax.
		 */
		content = slaxMakeAttribValueTemplate(swp, nodep, content);

		pref = (attrp->ns && attrp->ns->prefix)
		    ? (const char *) attrp->ns->prefix : NULL;
		slaxWrite(swp, " %s%s%s%s=%s", pref ?: "", pref ? ":" : "",
			  attrp->name, slaxSpacesAroundAttributeEquals,
			  slaxSpacesAroundAttributeEquals);
		slaxWrite(swp, "%s", content ?: UNKNOWN_EXPR);
		xmlFreeAndEasy(content);
	    }
	}
    }

    slaxWrite(swp, ">");

    if (nodep->children == NULL) {
	slaxWrite(swp, ";");
	slaxWriteNewline(swp, 0);
	return;
    }
	
    slaxWrite(swp, " ");

    if (!slaxNeedsBraces(nodep)) {
	slaxWriteContent(swp, docp, nodep);
	slaxWrite(swp, ";");
	slaxWriteNewline(swp, 0);
	return;
    }

    slaxWrite(swp, "{");
    slaxWriteNewline(swp, NEWL_INDENT);

    slaxWriteAllNs(swp, docp, nodep);
    slaxWriteChildren(swp, docp, nodep, FALSE);

    slaxWrite(swp, "}");
    slaxWriteNewline(swp, NEWL_OUTDENT);
}

static void
slaxWriteElementSimple (slax_writer_t *swp, xmlDocPtr docp UNUSED,
			xmlNodePtr nodep)
{
    slaxWrite(swp, "%s;", nodep->name);
    slaxWriteNewline(swp, 0);
}

static void
slaxWriteImport (slax_writer_t *swp, xmlDocPtr docp UNUSED, xmlNodePtr nodep)
{
    char *href = slaxGetAttrib(nodep, ATT_HREF);

    slaxWrite(swp, "%s \"%s\";", nodep->name, href ? href : "????");
    slaxWriteNewline(swp, 0);

    xmlFreeAndEasy(href);
}

static void
slaxWriteNamedTemplateParams (slax_writer_t *swp, xmlDocPtr docp,
			      xmlNodePtr nodep, int all, int complex)
{
    int first = TRUE;
    xmlNodePtr childp;

    for (childp = nodep->children; childp; childp = childp->next) {
	char *name, *rname;
	char *sel;

	if (childp->type != XML_ELEMENT_NODE
	    || !slaxIsXsl(childp)
	    || !streq((const char *) childp->name, ELT_PARAM))
	    continue;

	sel = slaxGetAttrib(childp, ATT_SELECT);
	if (sel || childp->children == NULL) {
	    if (complex && !all) {
		xmlFreeAndEasy(sel);
		continue;
	    }

	    name = slaxGetAttrib(childp, ATT_NAME);
	    rname = (name && *name == '$') ? name + 1 : name;

	    if (all) {
		slaxWrite(swp, "param $%s%s", 
			  rname, sel ? " = " : "");
		if (sel) {
		    char *expr = slaxMakeExpression(swp, childp, sel);
		    slaxWriteValue(swp, expr);
		    xmlFreeAndEasy(expr);
		}
		slaxWrite(swp, ";");
		slaxWriteNewline(swp, 0);

	    } else {
		slaxWrite(swp, "%s$%s%s", first ? "" : ", ",
			  rname, sel ? " = " : "");
		first = FALSE;
		if (sel) {
		    char *expr = slaxMakeExpression(swp, childp, sel);
		    slaxWriteValue(swp, expr);
		    xmlFreeAndEasy(expr);
		}
	    }

	} else {
	    if (!complex) {
		xmlFreeAndEasy(sel);
		continue;
	    }

	    name = slaxGetAttrib(childp, ATT_NAME);
	    rname = (name && *name == '$') ? name + 1 : name;

	    if (!slaxV10(swp)
		&& strncmp(name, slaxTernaryPrefix + 1,
			   sizeof(slaxTernaryPrefix) - 2) == 0) {
		xmlFreeAndEasy(name);
		xmlFreeAndEasy(sel);
		continue;
	    }

	    slaxWrite(swp, "param $%s = {", rname);
	    slaxWriteNewline(swp, NEWL_INDENT);

	    slaxWriteChildren(swp, docp, childp, FALSE);
	    
	    slaxWrite(swp, "}");
	    slaxWriteNewline(swp, NEWL_OUTDENT);
	}

	xmlFreeAndEasy(name);
	xmlFreeAndEasy(sel);
    }
}

/**
 * slaxNeedsBlankline:
 *
 *    determine if we should emit a blank line before this xsl element
 *
 * @param nodep the node
 * @return TRUE if the element should be preceeded by a blank line
 */
static int
slaxNeedsBlankline (xmlNodePtr nodep)
{
    if (nodep->prev == NULL)
	return TRUE;

    /* Allow whitespace-only text nodes */
    for (nodep = nodep->prev; nodep && nodep->type == XML_TEXT_NODE;
	 nodep = nodep->prev)
	if (!slaxIsWhiteString(nodep->content))
	    return TRUE;

    /*
     * This is the real test: is the preceeding node a comment?
     * If so, we assume that it's related to the element (a header
     * comment for element) and want to emit it immediately
     * preceeding the element, with no interloping blank line.
     * If not, we assume it's unrelated to the element and want
     * to emit a blank line.
     */
    if (nodep && nodep->type != XML_COMMENT_NODE)
	return TRUE;

    return FALSE;
}

/**
 * slaxWriteTemplate:
 *
 * Emit a template (named or match) by descending thru the given node's
 * hierarchy.
 */
static void
slaxWriteTemplate (slax_writer_t *swp, xmlDocPtr docp, xmlNodePtr nodep)
{
    char *name = slaxGetAttrib(nodep, ATT_NAME);
    char *match = slaxGetAttrib(nodep, ATT_MATCH);
    char *priority = slaxGetAttrib(nodep, ATT_PRIORITY);
    char *mode = slaxGetAttrib(nodep, ATT_MODE);
    int hit = 0;

    if (slaxNeedsBlankline(nodep))
	slaxWriteBlankline(swp);

    if (name) {
	slaxWrite(swp, "template %s%s%s (", name, match ? " match " : "",
		  match ?: "");
	slaxWriteNamedTemplateParams(swp, docp, nodep, FALSE, FALSE);
	slaxWrite(swp, ")");

    } else if (match) {
	char *expr = slaxMakeExpression(swp, nodep, match);
	slaxWrite(swp, "match %s", expr ?: UNKNOWN_EXPR);
	xmlFreeAndEasy(expr);

    } else {
	/* XXX error */
    }

    if (nodep->children || priority || mode || nodep->nsDef) {
	slaxWrite(swp, " {");
	slaxWriteNewline(swp, NEWL_INDENT);

	if (mode) {
	    slaxWrite(swp, "mode \"%s\";", mode);
	    slaxWriteNewline(swp, 0);
	    hit += 1;
	}

	if (priority) {
	    slaxWrite(swp, "priority %s;", priority);
	    slaxWriteNewline(swp, 0);
	    hit += 1;
	}

	/*
	 * If we emitted a mode or priority statement and we need
	 * to emit some content, emit a blank line first.
	 */
	if (hit && nodep->children)
	    slaxWriteBlankline(swp);

	slaxWriteNamedTemplateParams(swp, docp, nodep, name == NULL, TRUE);

	slaxWriteAllNs(swp, docp, nodep);
	slaxWriteChildren(swp, docp, nodep, FALSE);

	slaxWrite(swp, "}");
	slaxWriteNewline(swp, NEWL_OUTDENT);

    } else {
	slaxWrite(swp, " { }");
	slaxWriteNewline(swp, 0);
    }

    xmlFreeAndEasy(mode);
    xmlFreeAndEasy(priority);
    xmlFreeAndEasy(match);
    xmlFreeAndEasy(name);
}

static void
slaxWriteFunctionResult (slax_writer_t *swp, xmlNodePtr nodep, char *sel)
{
    char *expr = slaxMakeExpression(swp, nodep, sel);

    slaxWrite(swp, "result ");
    slaxWriteValue(swp, expr ?: UNKNOWN_EXPR);
    slaxWrite(swp, ";");
    slaxWriteNewline(swp, 0);
    xmlFreeAndEasy(expr);
}

static int
slaxWriteFunctionResultNeedsBraces (slax_writer_t *swp UNUSED, xmlNodePtr nodep)
{
    xmlNsPtr cur;

    for (cur = nodep->nsDef; cur; cur = cur->next) {
	if (cur->href && streq((const char *) cur->href, XSL_URI))
	    continue;
	if (slaxIsReserved((const char *) cur->prefix))
	    continue;
	return TRUE;
    }

    return FALSE;
}

static void
slaxWriteFunctionElement (slax_writer_t *swp, xmlDocPtr docp, xmlNodePtr nodep)
{
    const char *name = (const char *) nodep->name;
    char *sel = NULL, *fn;

    if (streq(name, "function")) {
	fn = slaxGetAttrib(nodep, ATT_NAME);

	if (fn == NULL) {
	    slaxLog("slax: function with out a name");
	    return;
	}

	if (slaxNeedsBlankline(nodep))
	    slaxWriteBlankline(swp);

	slaxWrite(swp, "function %s (", fn);
	slaxWriteNamedTemplateParams(swp, docp, nodep, FALSE, FALSE);
	slaxWrite(swp, ") {");
	slaxWriteNewline(swp, NEWL_INDENT);

	slaxWriteNamedTemplateParams(swp, docp, nodep, fn == NULL, TRUE);

	xmlFree(fn);

    } else if (streq(name, "result")) {
	sel = slaxGetAttrib(nodep, ATT_SELECT);

	/*
	 * If we have a select attribute, then this attribute is the
	 * expresion on the "result" statement.
	 */
	if (sel && !slaxWriteFunctionResultNeedsBraces(swp, nodep)) {
	    slaxWriteFunctionResult(swp, nodep, sel);
	    xmlFree(sel);
	    return;
	}

	/*
	 * Otherwise we need to put the contents of this <xx:result> element
	 * into a "result" statement.
	 */
	slaxWrite(swp, "result {");
	slaxWriteNewline(swp, NEWL_INDENT);

    } else {
	slaxLog("slax: unknown element: %s", name);
	return;
    }

    slaxWriteAllNs(swp, docp, nodep);

    if (sel) {
	slaxWriteFunctionResult(swp, nodep, sel);
	xmlFree(sel);

    } else {
	slaxWriteChildren(swp, docp, nodep, FALSE);
    }

    slaxWrite(swp, "}");
    slaxWriteNewline(swp, NEWL_OUTDENT);
}

static void
slaxWriteValueOf (slax_writer_t *swp, xmlDocPtr docp UNUSED, xmlNodePtr nodep)
{
    char *sel = slaxGetAttrib(nodep, ATT_SELECT);
    char *expr = slaxMakeExpression(swp, nodep, sel);
    int disable_escaping = slaxIsDisableOutputEscaping(nodep);

    slaxWrite(swp, disable_escaping ? "uexpr " : "expr ");
    slaxWriteValue(swp, expr ?: UNKNOWN_EXPR);
    slaxWrite(swp, ";");
    slaxWriteNewline(swp, 0);

    xmlFreeAndEasy(expr);
    xmlFreeAndEasy(sel);
}

/**
 * slaxIsSimpleElement:
 *   determine if node contains just a single simple element, allowing
 *   us to avoid using braces for the value
 *
 * @param node node to test
 * @return TRUE if the node contains a single simple element
 */
static int
slaxIsSimpleElement (xmlNodePtr nodep)
{
    int hit = 0;

    for ( ; nodep; nodep = nodep->next) {
	if (nodep->type == XML_ELEMENT_NODE) {
	    if (nodep->ns && nodep->ns->href) {
		/* Two special namespaces mean this is not simple */
		if (slaxIsXsl(nodep))
		    return FALSE;

		if (streq((const char *) nodep->ns->href, SLAX_URI))
		    return FALSE;
	    }

	    if (hit++)
		return FALSE;

	} else if (nodep->type == XML_TEXT_NODE) {
	    if (!slaxIsWhiteString(nodep->content))
		return FALSE;

	} else {
	    return FALSE;
	}
    }

    if (hit == 1)
	return TRUE;

    return FALSE;
}

static xmlNodePtr
slaxWriterFindNext (xmlNodePtr start, const char *name,
		    const char *uri, int first)
{
    xmlNodePtr cur;

    for (cur = start; cur; cur = cur->next) {
	if (cur->type != XML_ELEMENT_NODE)
	    continue;

	if (cur->ns && streq((const char *) cur->name, name)
		    && streq((const char *) cur->ns->href, uri))
	    return cur;

	if (first)
	    return NULL;
    }

    return NULL;
}

/*
 * We are looking at a for loop, which turns:
 *
 *         for $i (item) {
 *
 * into:
 *
 *      <xsl:variable name="slax-dot-3" select="."/>
 *      <xsl:for-each select="item">
 *        <xsl:variable name="i" select="."/>
 *        <xsl:for-each select="$slax-dot-3">
 *
 * We need to ensure this is really what we're looking at, and encode it
 * as proper SLAX.  If it isn't a valid "for" loop, then we return TRUE
 * to clue the caller in.
 */
static int
slaxWriteForLoop (slax_writer_t *swp, xmlDocPtr docp, xmlNodePtr outer_var,
		  const char *vname, const char *vselect)
{
    xmlNodePtr outer_for;
    xmlNodePtr inner_for;
    xmlNodePtr inner_var;
    xmlNodePtr cur;
    char *cp, *expr;

    /* Must be 'select="."' */
    if (vselect[0] != '.' || vselect[1] != '\0')
	return TRUE;

    outer_for = slaxWriterFindNext(outer_var->next,
				   ELT_FOR_EACH, XSL_URI, TRUE);
    if (outer_for == NULL)
	return TRUE;

    inner_var = slaxWriterFindNext(outer_for->children,
				   ELT_VARIABLE, XSL_URI, FALSE);
    if (inner_var == NULL)
	return TRUE;

    inner_for = slaxWriterFindNext(inner_var->next,
				   ELT_FOR_EACH, XSL_URI, TRUE);
    if (inner_for == NULL)
	return TRUE;

    /* Some final checks */
    /* Check: nothing else in for loop */
    for (cur = inner_for->next; cur; cur = cur->next) {
	if (cur->type == XML_ELEMENT_NODE)
	    return TRUE;
    }

    /* Check: First inner variable select must be "." */
    cp = slaxGetAttrib(inner_var, ATT_SELECT);
    if (cp[0] != '.' || cp[1] != '\0') {
	xmlFree(cp);
	return TRUE;
    }
    xmlFree(cp);

    /* Check: The variable names must match */
    cp = slaxGetAttrib(inner_for, ATT_SELECT);
    if (cp[0] != '$' || !streq(cp + 1, vname)) {
	xmlFree(cp);
	return TRUE;
    }
    xmlFree(cp);

    /* We have all the pieces; now we start writing */
    cp = slaxGetAttrib(outer_for, ATT_SELECT);
    expr = slaxMakeExpression(swp, outer_for, cp);
    xmlFree(cp);

    cp = slaxGetAttrib(inner_var, ATT_NAME);

    slaxWriteBlankline(swp);
    slaxWrite(swp, "for $%s (%s) {", cp, expr);
    slaxWriteNewline(swp, NEWL_INDENT);

    xmlFree(cp);
    xmlFree(expr);

    /* Now we need to check for any "sort" statements */
    for (cur = outer_for->children; cur; cur = cur->next) {
	if (cur == inner_var)
	    break;

	if (cur->type != XML_ELEMENT_NODE)
	    continue;

	if (cur->ns && streq((const char *) cur->name, ELT_SORT)
		&& slaxIsXsl(cur))
	    slaxWriteSort(swp, docp, cur);
    }

    slaxWriteChildren(swp, docp, inner_for, FALSE);

    slaxWrite(swp, "}");
    slaxWriteNewline(swp, NEWL_OUTDENT);

    /*
     * Turn on the SWF_FORLOOP flag but only _after_ we've written
     * our children.  This tell slaxWriteForEach that we've already
     * done his job.
     */
    swp->sw_flags |= SWF_FORLOOP;

    return FALSE;
}

/*
 * Extract the name of the real variable from a call to slax:node-set()
 * during the deconstruction of the assignment operator (":=").  The
 * caller must free the string.
 */
static char *
slaxVarAssignName (const char *name)
{
    const char *cp, *start, *endp;
    char *vname;

    if (name == NULL
	    || strncmp(name, slaxVarNsCall, sizeof(slaxVarNsCall) - 1) != 0)
	return NULL;

    cp = name + sizeof(slaxVarNsCall) - 1;
    if (*cp++ != '$')
	return NULL;

    start = cp;		/* Save variable name starting point */
    for ( ; *cp; cp++) {
	if (*cp == ')') {

	    /* Detect the temporary name part added in slaxAvoidRtf() */
	    /* XXX handle if -temp- is part of the user name; use "slax-" */
	    endp = strstr(start, "-temp-");
	    if (endp == NULL)
		return NULL;

	    vname = xmlMalloc(endp - start + 1);
	    if (vname == NULL) /* Cannot happen, but feels good anyway */
		return FALSE;

	    memcpy(vname, start, endp - start);
	    vname[endp - start ] = '\0'; 

	    return vname;
	}

	if (!slaxIsVarChar(*cp))
	    return FALSE;
    }

    return NULL;
}

/*
 * Was this variable the second part of the expansion of an assignment
 * operator?  In order to avoid RTFs, we turn "var $x := <this>;" into
 * a temporary variable that holds the RTF and then make the real
 * variable with a call to slax:node-set().  To undo this, we need to
 * notice the temp variable and unroll the logic.
 */
static int
slaxVarWasAssign (slax_writer_t *swp UNUSED, xmlNodePtr nodep,
		  char *name UNUSED, char *sel)
{
    char *cp, *vname;
    xmlNodePtr cur;

    vname = slaxVarAssignName(sel);
    if (vname == NULL)
	return FALSE;

    /* Find the previous variable and look at the variable name */
    for (cur = nodep->prev; cur; cur = cur->prev) {
	if (cur->type == XML_ELEMENT_NODE) {
	    /*
	     * XXX this can't be right; 'nodep' should be 'cur', but
	     * making that change breaks 'make test'.  Likely the
	     * entire loop should be removed and only nodep should be
	     * tested.
	     */
	    cp = slaxGetAttrib(nodep, ATT_NAME);
	    if (cp && streq(cp, vname)) {
		xmlFree(cp);
		xmlFree(vname);
		return TRUE;
	    }

	    xmlFreeAndEasy(cp);
	    xmlFree(vname);
	    return FALSE;	/* Otherwise, it's not our's */
	}
    }

    xmlFree(vname);
    return FALSE;
}

/*
 * Determine if this is the second part of the assignment expansion.
 * See slaxVarWasAssign() for details.
 */
static char *
slaxVarIsAssign (slax_writer_t *swp UNUSED, xmlNodePtr nodep, char *name UNUSED)
{
    char *cp, *vname;
    xmlNodePtr cur;

    /* Find the next variable and look at the select attribute */
    for (cur = nodep->next; cur; cur = cur->next) {
	if (cur->type == XML_ELEMENT_NODE) {
	    cp = slaxGetAttrib(cur, ATT_SELECT);
	    vname = slaxVarAssignName(cp);
	    xmlFree(cp);

	    if (vname)
		return vname;

	    return NULL;	/* Otherwise, it's not our's */
	}
    }

    return NULL;
}

static char *
slaxVarAssignContents (const char *expr)
{
    const char *cp, *endp;
    char *res;

    if (expr == NULL
	    || strncmp(expr, slaxVarNsCall, sizeof(slaxVarNsCall) - 1) != 0)
	return NULL;

    cp = expr + sizeof(slaxVarNsCall) - 1;
    if (*cp == '$')		/* Variable reference */
	return NULL;

    endp = cp + strlen(cp) - 1;
    if (*endp != ')')
	return NULL;

    res = xmlMalloc(endp - cp + 1);
    memcpy(res, cp, endp - cp);
    res[endp - cp] = '\0';

    return res;
}

static void
slaxWriteVariable (slax_writer_t *swp, xmlDocPtr docp, xmlNodePtr nodep)
{
    char *name;
    char *sel;
    char *mvarname = slaxGetAttrib(nodep, ATT_MVARNAME);
    char *svarname;
    const char *tag = (nodep->name[0] == 'v') ? "var" : "param";
    xmlNodePtr vnode = nodep;
    char *aname;
    const char *operator;
    int is_mvar = FALSE;

    if (mvarname) {
	/*
	 * If this variable has an 'mvarname' attribute, then we are
	 * looking at the shadow variable of a mutable variable (aka
	 * "mvar").  Since shadow variables aren't seen in real SLAX
	 * code, we skip it, knowing that we'll see the real part
	 * later.
	 */
	xmlFree(mvarname);
	return;
    }

    name = slaxGetAttrib(nodep, ATT_NAME);
    sel = slaxGetAttrib(nodep, ATT_SELECT);
    svarname = slaxGetAttrib(nodep, ATT_SVARNAME);

    if (svarname) {
	/*
	 * If this variable has an 'svarname' attribute, then we are
	 * looking at the real part of a mutable variable (aka
	 * "mvar").  We know that if the mvar is initialized with an
	 * RTF, then the shadow variable has the contents and the real
	 * variable has a select attribute containing a call to
	 * "slax:mvar-init()" with the svarname passed as the only
	 * parameter.  But the select could also have a scalar value.
	 * If the select refers to the svar, we need to go find it and
	 * use that value as the initializer for the mvar.
	 */
	const char mvar_init[] = SLAX_PREFIX ":" FUNC_MVAR_INIT "(";
	tag = "mvar";

	is_mvar = TRUE;

	if (sel && strncmp(sel, mvar_init, strlen(mvar_init)) == 0) {
	    for (vnode = nodep->prev; vnode; vnode = vnode->prev)
		if (vnode->type == XML_ELEMENT_NODE)
		    break;
	    if (vnode == NULL || vnode->children == NULL)
		vnode = nodep;	/* Revert */
	}
	xmlFree(svarname);
    }

    if (name && sel && !slaxV10(swp)
	&& strncmp(name, slaxForVariablePrefix + 1,
		   sizeof(slaxForVariablePrefix) - 2) == 0) {
	if (!slaxWriteForLoop(swp, docp, nodep, name, sel)) {
	    xmlFree(sel);
	    xmlFree(name);
	    return;
	}
	/* Otherwise it wasn't a "real" "for" loop, so continue */
    }

    /*
     * Skim ternary variables
     */
    if (name && sel == NULL && !slaxV10(swp)
	&& strncmp(name, slaxTernaryPrefix + 1,
		   sizeof(slaxTernaryPrefix) - 2) == 0) {
	xmlFree(name);
	return;
    }

    /*
     * If this is the second part of an assignment operator (":="),
     * skip it.
     */
    if (slaxVarWasAssign(swp, nodep, name, sel)) {
	xmlFree(sel);
	xmlFree(name);
	return;
    }

    /*
     * If this is the first part of assignment, use the name of
     * the real variable.
     */
    aname = slaxVarIsAssign(swp, nodep, name);
    if (aname) {
	operator = ":=";
    } else {
	operator = "=";
	aname = name;
    }

    if (vnode->children) {
	xmlNodePtr childp = vnode->children;

	if (childp->next == NULL && childp->type == XML_TEXT_NODE) {
	    /*
	     * If there's only one child and it's text, we can emit
	     * a simple string initializer.
	     */
	    slaxWrite(swp, "%s $%s %s \"", tag, aname, operator);
	    slaxWriteEscaped(swp, (char *) childp->content, SEF_DOUBLEQ);
	    slaxWrite(swp, "\";");
	    slaxWriteNewline(swp, 0);

	} else if (slaxIsSimpleElement(childp)) {
	    slaxWrite(swp, "%s $%s %s ", tag, aname, operator);
	    slaxWriteChildren(swp, docp, vnode, FALSE);

	} else {
	    slaxWrite(swp, "%s $%s %s {", tag, aname, operator);
	    slaxWriteNewline(swp, NEWL_INDENT);

	    slaxWriteChildren(swp, docp, vnode, FALSE);

	    slaxWrite(swp, "}");
	    slaxWriteNewline(swp, NEWL_OUTDENT);
	}

    } else if (sel) {
	char *selval = sel;

	if (is_mvar) {
	    /*
	     * We are looking at an initialization that is a call to
	     * mvar-init().  We know the real expression is the optional
	     * fourth argument.  The other args are the mvar name,
	     * the shadow variable name, and the shadow variable value.
	     *    $x = slax:mvar-init("x", "slax-x", $slax-x, real/value);
	     *                           a         b        c
	     * So we skip over the first three commas and use anything
	     * else as the initial value.  If there is no fourth
	     * argument, there's no initial value.
	     */
	    do {
		char *cp = strchr(sel, ','); /* Find 'a' */
		if (cp == NULL)
		    break;
		cp = strchr(cp + 1, ','); /* Find 'b' */
		if (cp == NULL)
		    break;
		cp = strchr(cp + 1, ','); /* Find 'c' */
		if (cp == NULL)
		    goto emit_simple;

		selval = cp + 1;
		cp = selval + strlen(selval) - 1;
		if (*cp == ')')
		    *cp = '\0';
	    } while (0);
	}

	/*
	 * The select might be an assignment if this is a function.
	 */
	char *expr = slaxMakeExpression(swp, nodep, selval);
	char *contents = slaxVarAssignContents(expr);

	if (contents) {
	    xmlFreeAndEasy(expr);
	    expr = contents;
	    operator = ":=";
	}

	slaxWrite(swp, "%s $%s %s ", tag, aname, operator);
	slaxWriteValue(swp, expr);
	slaxWrite(swp, ";");
	slaxWriteNewline(swp, 0);
	xmlFreeAndEasy(expr);

    } else {

    emit_simple:
	slaxWrite(swp, "%s $%s;", tag, aname);
	slaxWriteNewline(swp, 0);
    }

    if (name != aname) /* Could have been allocated by slaxVarIsAssign */
	xmlFreeAndEasy(aname);

    xmlFreeAndEasy(name);
    xmlFreeAndEasy(sel);
}

static void
slaxWriteTraceStmt (slax_writer_t *swp, xmlDocPtr docp UNUSED,
		    xmlNodePtr nodep)
{
    char *sel = slaxGetAttrib(nodep, ATT_SELECT);

    if (nodep->children) {
	xmlNodePtr childp = nodep->children;

	if (childp->next == NULL && childp->type == XML_TEXT_NODE) {
	    /*
	     * If there's only one child and it's text, we can emit
	     * a simple string value.
	     */
	    slaxWrite(swp, "trace \"");
	    slaxWriteEscaped(swp, (char *) childp->content, SEF_DOUBLEQ);
	    slaxWrite(swp, "\";");
	    slaxWriteNewline(swp, 0);

	} else {
	    slaxWrite(swp, "trace {");
	    slaxWriteNewline(swp, NEWL_INDENT);

	    slaxWriteChildren(swp, docp, nodep, FALSE);

	    slaxWrite(swp, "}");
	    slaxWriteNewline(swp, NEWL_OUTDENT);
	}

    } else if (sel) {
	char *expr = slaxMakeExpression(swp, nodep, sel);

	slaxWrite(swp, "trace ");
	slaxWriteValue(swp, expr);
	slaxWrite(swp, ";");
	slaxWriteNewline(swp, 0);
	xmlFreeAndEasy(expr);

    } else {
	slaxWrite(swp, "trace { }");
	slaxWriteNewline(swp, 0);
    }

    xmlFreeAndEasy(sel);
}

static void
slaxWriteWhileStmt (slax_writer_t *swp, xmlDocPtr docp UNUSED,
		    xmlNodePtr nodep)
{
    char *tst = slaxGetAttrib(nodep, ATT_TEST);
    char *expr = slaxMakeExpression(swp, nodep, tst);

    slaxWrite(swp, "while (");
    slaxWriteValue(swp, expr);
    slaxWrite(swp, ") {");
    slaxWriteNewline(swp, NEWL_INDENT);

    xmlFreeAndEasy(expr);
    xmlFreeAndEasy(tst);

    slaxWriteChildren(swp, docp, nodep, FALSE);

    slaxWrite(swp, "}");
    slaxWriteNewline(swp, NEWL_OUTDENT);
}

static void
slaxWriteMvarStmt (slax_writer_t *swp, xmlDocPtr docp UNUSED,
		   xmlNodePtr nodep, int append)
{
    char *name = slaxGetAttrib(nodep, ATT_NAME);
    char *sel = slaxGetAttrib(nodep, ATT_SELECT);
    const char *sn = append ? "append" : "set";
    const char *op = append ? "+=" : "=";

    if (nodep->children) {
	xmlNodePtr childp = nodep->children;

	if (childp->next == NULL && childp->type == XML_TEXT_NODE) {
	    /*
	     * If there's only one child and it's text, we can emit
	     * a simple string value.
	     */
	    slaxWrite(swp, "%s $%s %s \"", sn, name, op);
	    slaxWriteEscaped(swp, (char *) childp->content, SEF_DOUBLEQ);
	    slaxWrite(swp, "\";");
	    slaxWriteNewline(swp, 0);

	} else if (slaxIsSimpleElement(childp)) {
	    slaxWrite(swp, "%s $%s %s ", sn, name, op);
	    slaxWriteChildren(swp, docp, nodep, FALSE);

	} else {
	    slaxWrite(swp, "%s $%s %s {", sn, name, op);
	    slaxWriteNewline(swp, NEWL_INDENT);

	    slaxWriteChildren(swp, docp, nodep, FALSE);

	    slaxWrite(swp, "}");
	    slaxWriteNewline(swp, NEWL_OUTDENT);
	}

    } else if (sel) {
	char *expr = slaxMakeExpression(swp, nodep, sel);

	slaxWrite(swp, "%s $%s %s ", sn, name, op);
	slaxWriteValue(swp, expr);
	slaxWrite(swp, ";");
	slaxWriteNewline(swp, 0);
	xmlFreeAndEasy(expr);

    } else {
	slaxWrite(swp, "%s $%s %s { }", sn, name, op);
	slaxWriteNewline(swp, 0);
    }

    xmlFreeAndEasy(sel);
    xmlFreeAndEasy(name);
}

static void
slaxWriteMvarSetStmt (slax_writer_t *swp, xmlDocPtr docp, xmlNodePtr nodep)
{
    slaxWriteMvarStmt(swp, docp, nodep, FALSE);
}

static void
slaxWriteMvarAppendStmt (slax_writer_t *swp, xmlDocPtr docp, xmlNodePtr nodep)
{
    slaxWriteMvarStmt(swp, docp, nodep, TRUE);
}

static void
slaxWriteParam (slax_writer_t *swp, xmlDocPtr docp UNUSED, xmlNodePtr nodep)
{
    if (swp->sw_indent > 2) {
	slaxWrite(swp, "/* 'param' statement is inappropriately nested */");
	slaxWriteNewline(swp, 0);
    }

    /*
     * If indent equals 1, we are in a template. For templates,
     * slaxWriteNamedTemplateParams() will have emitted the parameter
     * list already, so we must not repeat it here.
     */
    if (swp->sw_indent != 1)
	slaxWriteVariable(swp, docp, nodep);
}

static void
slaxWriteSlaxElement (slax_writer_t *swp, xmlDocPtr docp,
		     xmlNodePtr nodep)
{
    if (streq((const char *) nodep->name, ELT_TRACE))
	slaxWriteTraceStmt(swp, docp, nodep);

    else if (streq((const char *) nodep->name, ELT_WHILE))
	slaxWriteWhileStmt(swp, docp, nodep);

    else if (streq((const char *) nodep->name, ELT_SET_VARIABLE))
	slaxWriteMvarSetStmt(swp, docp, nodep);

    else if (streq((const char *) nodep->name, ELT_APPEND_TO_VARIABLE))
	slaxWriteMvarAppendStmt(swp, docp, nodep);
    /*
     * Add new slax elements here
     */

    /* XXX : need to add append, set, etc) */

    else
	slaxWriteElement(swp, docp, nodep);
}

static void
slaxWriteApplyTemplates (slax_writer_t *swp, xmlDocPtr docp, xmlNodePtr nodep)
{
    char *mode = slaxGetAttrib(nodep, ATT_MODE);
    char *sel = slaxGetAttrib(nodep, ATT_SELECT);
    xmlNodePtr childp;

    slaxWrite(swp, "apply-templates");
    if (sel) {
	char *expr = slaxMakeExpression(swp, nodep, sel);
	
	slaxWrite(swp, " %s", expr ?: UNKNOWN_EXPR);
	xmlFreeAndEasy(expr);
	xmlFreeAndEasy(sel);
    }

    if (nodep->children == NULL && mode == NULL && nodep->nsDef == NULL) {
	slaxWrite(swp, ";");
	slaxWriteNewline(swp, 0);
	return;
    }

    slaxWrite(swp, " {");
    slaxWriteNewline(swp, NEWL_INDENT);

    if (mode) {
	slaxWrite(swp, "mode \"%s\";", mode);
	slaxWriteNewline(swp, 0);
	xmlFree(mode);
    }

    slaxWriteAllNs(swp, docp, nodep);

    for (childp = nodep->children; childp; childp = childp->next) {
	char *name;

	if (childp->type != XML_ELEMENT_NODE)
	    continue;

	if (streq((const char *) childp->name, "with-param")) {

	    name = slaxGetAttrib(childp, ATT_NAME);
	    sel = slaxGetAttrib(childp, ATT_SELECT);

	    if (sel && sel[0] == '$'
		    && streq(sel + 1, name)) {
		slaxWrite(swp, "with $%s;", name);
		slaxWriteNewline(swp, 0);

	    } else {
		slaxWrite(swp, "with $%s = ", name);
		if (sel) {
		    char *expr = slaxMakeExpression(swp, childp, sel);

		    slaxWrite(swp, "%s;", expr ?: UNKNOWN_EXPR);
		    slaxWriteNewline(swp, 0);
		    xmlFreeAndEasy(expr);

		} else if (childp->children) {
		    int need_braces = slaxNeedsBlock(childp);

		    if (need_braces) {
			slaxWrite(swp, "{");
			slaxWriteNewline(swp, NEWL_INDENT);
		    }

		    slaxWriteChildren(swp, docp, childp, TRUE);

		    if (need_braces) {
			slaxWrite(swp, "}");
			slaxWriteNewline(swp, NEWL_OUTDENT);
		    }
		}
	    }

	    xmlFreeAndEasy(sel);
	    xmlFreeAndEasy(name);

	} else if (slaxIsXsl(childp)) {
	    slaxWriteXslElement(swp, docp, childp, NULL);

	} else if (childp->ns && childp->ns->href && !slaxV10(swp)
		   && streq((const char *) childp->ns->href, SLAX_URI)) {
	    slaxWriteSlaxElement(swp, docp, childp);

	} else {
	    slaxWriteElement(swp, docp, childp);
	}
    }

    slaxWrite(swp, "}");
    slaxWriteNewline(swp, NEWL_OUTDENT);
}

static void
slaxWriteCallTemplate (slax_writer_t *swp, xmlDocPtr docp, xmlNodePtr nodep)
{
    char *name = slaxGetAttrib(nodep, ATT_NAME);
    char *sel;
    xmlNodePtr childp;
    int need_braces = FALSE;
    int first = TRUE;

    slaxWrite(swp, "call %s(", name ?: "?????");

    xmlFreeAndEasy(name);

    if (nodep->children == NULL) {
	slaxWrite(swp, ");");
	slaxWriteNewline(swp, 0);
	return;
    }

    for (childp = nodep->children; childp; childp = childp->next) {
	if (childp->type != XML_ELEMENT_NODE)
	    continue;

	if (childp->children) {
	    need_braces = TRUE;
	    continue;
	}

	name = slaxGetAttrib(childp, ATT_NAME);
	sel = slaxGetAttrib(childp, ATT_SELECT);

	if (!first)
	    slaxWrite(swp, ", ");
	first = FALSE;

	slaxWrite(swp, "$%s", name);
	if (!(name && sel && *sel == '$' && streq(name, sel + 1))) {
	    slaxWrite(swp, " = ");
	    if (sel) {
		char *expr = slaxMakeExpression(swp, childp, sel);

		slaxWriteValue(swp, expr ?: UNKNOWN_EXPR);
		xmlFree(expr);

	    } else slaxWrite(swp, "''");
	}

	xmlFreeAndEasy(sel);
	xmlFreeAndEasy(name);
    }

    slaxWrite(swp, ")");

    if (!need_braces) {
	slaxWrite(swp, ";");
	slaxWriteNewline(swp, 0);
	return;
    }

    slaxWrite(swp, " {");
    slaxWriteNewline(swp, NEWL_INDENT);

    for (childp = nodep->children; childp; childp = childp->next) {
	if (childp->type != XML_ELEMENT_NODE)
	    continue;
	if (childp->children == NULL)
	    continue;

	sel = slaxGetAttrib(childp, ATT_SELECT);
	if (sel) {
	    xmlFree(sel);
	    continue;
	}

	name = slaxGetAttrib(childp, ATT_NAME);
	slaxWrite(swp, "with $%s = {", name ?: "????");
	slaxWriteNewline(swp, NEWL_INDENT);
	xmlFreeAndEasy(name);

	slaxWriteChildren(swp, docp, childp, FALSE);

	slaxWrite(swp, " }");
	slaxWriteNewline(swp, NEWL_OUTDENT);
    }

    slaxWrite(swp, "}");
    slaxWriteNewline(swp, NEWL_OUTDENT);
}

static void
slaxWriteIf (slax_writer_t *swp, xmlDocPtr docp, xmlNodePtr nodep)
{
    char *test = slaxGetAttrib(nodep, ATT_TEST);
    char *expr = slaxMakeExpression(swp, nodep, test);

    slaxWrite(swp, "if (%s) {", expr ?: UNKNOWN_EXPR);
    xmlFreeAndEasy(expr);
    xmlFreeAndEasy(test);

    slaxWriteNewline(swp, NEWL_INDENT);

    slaxWriteChildren(swp, docp, nodep, FALSE);

    slaxWrite(swp, "}");
    slaxWriteNewline(swp, NEWL_OUTDENT);
}

static void
slaxWriteForEach (slax_writer_t *swp, xmlDocPtr docp, xmlNodePtr nodep)
{
    char *sel, *expr;

    /* If we just rendered a for-each loop as a 'for' loop, then we're done */
    if (swp->sw_flags & SWF_FORLOOP) {
	swp->sw_flags &= ~SWF_FORLOOP;
	return;
    }

    sel = slaxGetAttrib(nodep, ATT_SELECT);
    expr = slaxMakeExpression(swp, nodep, sel);

    slaxWriteBlankline(swp);
    slaxWrite(swp, "for-each (%s) {", expr ?: UNKNOWN_EXPR);
    xmlFreeAndEasy(expr);
    xmlFreeAndEasy(sel);

    slaxWriteNewline(swp, NEWL_INDENT);

    slaxWriteChildren(swp, docp, nodep, FALSE);

    slaxWrite(swp, "}");
    slaxWriteNewline(swp, NEWL_OUTDENT);
}

static void
slaxWriteCopyOf (slax_writer_t *swp, xmlDocPtr docp UNUSED, xmlNodePtr nodep)
{
    char *sel = slaxGetAttrib(nodep, ATT_SELECT);

    if (sel) {
	char *expr = slaxMakeExpression(swp, nodep, sel);
	slaxWrite(swp, "copy-of %s;", expr ?: UNKNOWN_EXPR);
	xmlFreeAndEasy(expr);
	xmlFree(sel);
	slaxWriteNewline(swp, 0);
    }
}

/*
 * Indicate whether slaxWriteAllAttributes() would write any
 * output, that is, if there are any attributes for the given
 * node that are _not_ in the skip list.
 */
static int
slaxHasOtherAttributes (xmlNodePtr nodep, const char **skip)
{
    xmlAttrPtr attrp;
    const char **cpp;

    for (attrp = nodep->properties; attrp; attrp = attrp->next) {
	if (attrp->children && attrp->children->content) {

	    /* If the attribute appears in the "skip" list, skip it */
	    for (cpp = skip; cpp && *cpp; cpp++)
		if (streq((const char *) attrp->name, *cpp))
		    break;
	    if (cpp && *cpp)
		continue;

	    return TRUE;
	}
    }

    return FALSE;
}

static void
slaxWriteAllAttributes (slax_writer_t *swp, xmlNodePtr nodep,
			const char **skip, int avt)
{
    xmlAttrPtr attrp;
    const char *pref = NULL;
    char *content;
    const char **cpp;

    for (attrp = nodep->properties; attrp; attrp = attrp->next) {
	if (attrp->children && attrp->children->content) {

	    /* If the attribute appears in the "skip" list, skip it */
	    for (cpp = skip; cpp && *cpp; cpp++)
		if (streq((const char *) attrp->name, *cpp))
		    break;
	    if (cpp && *cpp)
		continue;

	    content = (char *) attrp->children->content;

	    /*
	     * The attribute might be an attribute value template,
	     * which xslt uses as a simple concatenation and xpath
	     * interpretation mechanism.  We need to recode it
	     * according to SLAX syntax.
	     */
	    if (avt)
		content = slaxMakeAttribValueTemplate(swp, nodep, content);

	    pref = (attrp->ns && attrp->ns->prefix)
		? (const char *) attrp->ns->prefix : NULL;

	    slaxWrite(swp, "%s%s%s %s;", pref ?: "", pref ? ":" : "",
		      attrp->name, content ?: UNKNOWN_EXPR);
	    slaxWriteNewline(swp, 0);

	    if (avt)
		xmlFreeAndEasy(content);
	}
    }
}

static void
slaxWriteStatementWithAttributes (slax_writer_t *swp, xmlDocPtr docp UNUSED,
				  xmlNodePtr nodep, const char *sname,
				  const char *aname, int optional, int avt)
{
    const char *skip[] = { aname, NULL };
    char *aval = aname ? slaxGetAttrib(nodep, aname) : NULL;

    slaxWrite(swp, "%s%s%s", sname, (aval || !optional) ? " " : "",
	      aval ?: optional ? "" : UNKNOWN_EXPR);

    if (slaxHasOtherAttributes(nodep, skip)) {
	slaxWrite(swp, " {");
	slaxWriteNewline(swp, NEWL_INDENT);

	slaxWriteAllAttributes(swp, nodep, skip, avt);

	slaxWrite(swp, "}");
	slaxWriteNewline(swp, NEWL_OUTDENT);
    } else {
	slaxWrite(swp, ";");
	slaxWriteNewline(swp, 0);
    }

    xmlFreeAndEasy(aval);
}

static void
slaxWriteSort (slax_writer_t *swp, xmlDocPtr docp,
		       xmlNodePtr nodep)
{
    slaxWriteStatementWithAttributes(swp, docp, nodep,
				     "sort", ATT_SELECT, TRUE, TRUE);
}

#define S1A_NONE	0	/* No special processing */
#define S1A_AVT		1	/* Attribute value template */
#define S1A_XP		2	/* XPath expression */

static void
slaxWriteStatementOneAttribute (slax_writer_t *swp, xmlNodePtr nodep,
				const char *sname, const char *aname,
				int style)
{
    char *cp = slaxGetAttrib(nodep, aname);
    char *content;

    if (cp) {
	if (style == S1A_AVT)
	    content =  slaxMakeAttribValueTemplate(swp, nodep, cp);
	else if (style == S1A_XP)
	    content = slaxMakeExpression(swp, nodep, cp);
	else
	    content = cp;

	slaxWrite(swp, "%s %s;", sname, content ?: UNKNOWN_EXPR);
	slaxWriteNewline(swp, 0);

	if (content != cp)
	    xmlFreeAndEasy(content);
	xmlFree(cp);
    }
}

static void
slaxWriteNumber (slax_writer_t *swp, xmlDocPtr docp UNUSED,
		       xmlNodePtr nodep)
{
    const char *skip[] = { ATT_VALUE, NULL };
    const char *skip2[] = { ATT_VALUE, ATT_FROM, ATT_COUNT, NULL };
    char *aval = slaxGetAttrib(nodep, ATT_VALUE);

    slaxWrite(swp, "number%s%s", aval ? " " : "", aval ?: "");

    if (slaxHasOtherAttributes(nodep, skip)) {
	slaxWrite(swp, " {");
	slaxWriteNewline(swp, NEWL_INDENT);

	slaxWriteAllAttributes(swp, nodep, skip2, TRUE);

	slaxWriteStatementOneAttribute(swp, nodep, "from", ATT_FROM, S1A_XP);
	slaxWriteStatementOneAttribute(swp, nodep, "count", ATT_COUNT, S1A_XP);

	slaxWrite(swp, "}");
	slaxWriteNewline(swp, NEWL_OUTDENT);
    } else {
	slaxWrite(swp, ";");
	slaxWriteNewline(swp, 0);
    }

    xmlFreeAndEasy(aval);
}

static void
slaxWriteCopyNode (slax_writer_t *swp, xmlDocPtr docp UNUSED,
		   xmlNodePtr nodep)
{
    char *value = slaxGetAttrib(nodep, ATT_VALUE);
    char *use = slaxGetAttrib(nodep, ATT_USE_ATTRIBUTE_SETS);

    slaxWrite(swp, "copy-node%s%s", value ? " " : "", value ?: "");

    if (nodep->children || use) {
	slaxWrite(swp, " {");
	slaxWriteNewline(swp, NEWL_INDENT);

	if (use) {
	    slaxWrite(swp, "use-attribute-sets %s;", use);
	    slaxWriteNewline(swp, 0);
	    xmlFree(use);
	}

	slaxWriteAllNs(swp, docp, nodep);
	slaxWriteChildren(swp, docp, nodep, FALSE);

	slaxWrite(swp, "}");
	slaxWriteNewline(swp, NEWL_OUTDENT);
    } else {
	slaxWrite(swp, ";");
	slaxWriteNewline(swp, 0);
    }

    xmlFreeAndEasy(value);
}

static void
slaxWriteDecimalFormat (slax_writer_t *swp, xmlDocPtr docp UNUSED,
		   xmlNodePtr nodep)
{
    const char *skip[] = { ATT_NAME, NULL };
    const char *skip2[] = { ATT_NAME, ATT_NAN, NULL };
    char *aval = slaxGetAttrib(nodep, ATT_NAME);

    slaxWrite(swp, "decimal-format %s", aval ?: UNKNOWN_EXPR);

    if (slaxHasOtherAttributes(nodep, skip)) {
	slaxWrite(swp, " {");
	slaxWriteNewline(swp, NEWL_INDENT);

	slaxWriteAllAttributes(swp, nodep, skip2, TRUE);
	slaxWriteStatementOneAttribute(swp, nodep, "nan", ATT_NAN, S1A_AVT);

	slaxWrite(swp, "}");
	slaxWriteNewline(swp, NEWL_OUTDENT);
    } else {
	slaxWrite(swp, ";");
	slaxWriteNewline(swp, 0);
    }

    xmlFreeAndEasy(aval);
}

static void
slaxWriteOutputMethod (slax_writer_t *swp, xmlDocPtr docp UNUSED,
		       xmlNodePtr nodep)
{
    const char *skip[] = { ATT_METHOD, NULL };
    const char *skip2[] = { ATT_METHOD, ATT_CDATA_SECTION_ELEMENTS, NULL };
    char *aval = slaxGetAttrib(nodep, ATT_METHOD);

    slaxWrite(swp, "output-method%s%s", aval ? " " : "", aval ?: "");

    if (slaxHasOtherAttributes(nodep, skip)) {
	slaxWrite(swp, " {");
	slaxWriteNewline(swp, NEWL_INDENT);

	slaxWriteAllAttributes(swp, nodep, skip2, TRUE);
	slaxWriteStatementOneAttribute(swp, nodep, "cdata-section-elements",
				       ATT_CDATA_SECTION_ELEMENTS, S1A_NONE);

	slaxWrite(swp, "}");
	slaxWriteNewline(swp, NEWL_OUTDENT);
    } else {
	slaxWrite(swp, ";");
	slaxWriteNewline(swp, 0);
    }

    xmlFreeAndEasy(aval);
}

static void
slaxWriteKey (slax_writer_t *swp, xmlDocPtr docp UNUSED,
			 xmlNodePtr nodep)
{
    char *name = slaxGetAttrib(nodep, ATT_NAME);

    if (slaxNeedsBlankline(nodep))
	slaxWriteBlankline(swp);

    slaxWrite(swp, "key %s {", name ?: "???");
    slaxWriteNewline(swp, NEWL_INDENT);

    slaxWriteStatementOneAttribute(swp, nodep, "match", ATT_MATCH, S1A_XP);
    slaxWriteStatementOneAttribute(swp, nodep, "value", ATT_USE, S1A_XP);

    slaxWrite(swp, "}");
    slaxWriteNewline(swp, NEWL_OUTDENT);

    xmlFreeAndEasy(name);
}

static void
slaxWriteAttributeStatement (slax_writer_t *swp, xmlDocPtr docp UNUSED,
			 xmlNodePtr nodep)
{
    char *name = slaxGetAttrib(nodep, ATT_NAME);
    char *expr = slaxMakeAttribValueTemplate(swp, nodep, name);

    slaxWrite(swp, "attribute %s {", expr ?: UNKNOWN_EXPR);
    slaxWriteNewline(swp, NEWL_INDENT);

    slaxWriteStatementOneAttribute(swp, nodep,
				   "ns-template", ATT_NAMESPACE, S1A_AVT);

    slaxWriteAllNs(swp, docp, nodep);
    slaxWriteChildren(swp, docp, nodep, FALSE);

    slaxWrite(swp, "}");
    slaxWriteNewline(swp, NEWL_OUTDENT);

    xmlFreeAndEasy(expr);
    xmlFreeAndEasy(name);
}

static void
slaxWriteAttributeSetStatement (slax_writer_t *swp, xmlDocPtr docp UNUSED,
				xmlNodePtr nodep)
{
    char *name = slaxGetAttrib(nodep, ATT_NAME);
    char *asets = slaxGetAttrib(nodep, ATT_USE_ATTRIBUTE_SETS);

    if (slaxNeedsBlankline(nodep))
	slaxWriteBlankline(swp);

    slaxWrite(swp, "attribute-set %s", name);
    if (nodep->children || nodep->nsDef || asets) {
	slaxWrite(swp, " {");
	slaxWriteNewline(swp, NEWL_INDENT);

	slaxWriteAllNs(swp, docp, nodep);
	if (asets) {
	    slaxWrite(swp, "use-attribute-sets %s;", asets);
	    slaxWriteNewline(swp, 0);
	}
	slaxWriteChildren(swp, docp, nodep, FALSE);

	slaxWrite(swp, "}");
	slaxWriteNewline(swp, NEWL_OUTDENT);

    } else {
	/* This should be an error */
	slaxWrite(swp, ";");
	slaxWriteNewline(swp, 0);
    }

    xmlFreeAndEasy(asets);
    xmlFreeAndEasy(name);
}

static void
slaxWriteElementStatement (slax_writer_t *swp, xmlDocPtr docp UNUSED,
			 xmlNodePtr nodep)
{
    char *name = slaxGetAttrib(nodep, ATT_NAME);
    char *ns = slaxGetAttrib(nodep, ATT_NAMESPACE);
    char *asets = slaxGetAttrib(nodep, ATT_USE_ATTRIBUTE_SETS);
    char *expr = slaxMakeAttribValueTemplate(swp, nodep, name);

    slaxWrite(swp, "element %s", expr);
    if (nodep->children || nodep->nsDef || asets || ns) {
	slaxWrite(swp, " {");
	slaxWriteNewline(swp, NEWL_INDENT);

	if (ns) {
	    char *content =  slaxMakeAttribValueTemplate(swp, nodep, ns);

	    slaxWrite(swp, "ns-template %s;", content ?: UNKNOWN_EXPR);
	    slaxWriteNewline(swp, 0);
	    xmlFree(content);
	    xmlFree(ns);
	}

	/* Then write all other (xmlns:) namespaces */
	slaxWriteAllNs(swp, docp, nodep);

	if (asets) {
	    slaxWrite(swp, "use-attribute-sets %s;", asets);
	    slaxWriteNewline(swp, 0);
	}

	slaxWriteChildren(swp, docp, nodep, FALSE);

	slaxWrite(swp, "}");
	slaxWriteNewline(swp, NEWL_OUTDENT);

    } else {
	slaxWrite(swp, ";");
	slaxWriteNewline(swp, 0);
    }

    xmlFreeAndEasy(expr);
    xmlFreeAndEasy(asets);
    xmlFreeAndEasy(name);
}

static void
slaxWriteChoose (slax_writer_t *swp, xmlDocPtr docp, xmlNodePtr nodep)
{
    xmlNodePtr childp;
    int first = TRUE;

    for (childp = nodep->children; childp; childp = childp->next) {

	if (childp->type != XML_ELEMENT_NODE)
	    continue;

	if (streq((const char *) childp->name, ELT_WHEN)) {
	    char *test = slaxGetAttrib(childp, ATT_TEST);
	    char *expr = slaxMakeExpression(swp, nodep, test);

	    if (!first)
		slaxWriteNewline(swp, NEWL_OUTDENT);
		
	    slaxWrite(swp, "%sif (%s) {", first ? "" : "} else ",
		      expr ?: UNKNOWN_EXPR);
	    xmlFreeAndEasy(expr);
	    xmlFreeAndEasy(test);

	    slaxWriteNewline(swp, NEWL_INDENT);

	    slaxWriteChildren(swp, docp, childp, FALSE);

	} else if (streq((const char *) childp->name, ELT_OTHERWISE)) {
	    slaxWriteNewline(swp, NEWL_OUTDENT);
	    slaxWrite(swp, "} else {");
	    slaxWriteNewline(swp, NEWL_INDENT);

	    slaxWriteChildren(swp, docp, childp, FALSE);

	} /* XXX else error */

	first = FALSE;
    }

    if (!first) {
	slaxWrite(swp, "}");
	slaxWriteNewline(swp, NEWL_OUTDENT);
    }
}

static void
slaxWritePreAndStripSpace (slax_writer_t *swp, xmlDocPtr docp UNUSED,
			    xmlNodePtr nodep)
{
    if (swp->sw_indent == 0) {
	const char *name = (const char *) nodep->name;
	char *elements = slaxGetAttrib(nodep, ATT_ELEMENTS);
    
	slaxWrite(swp, "%s %s;", name, elements);
	slaxWriteNewline(swp, 0);
	xmlFreeAndEasy(elements);
    }
}

static void
slaxWriteFallback (slax_writer_t *swp, xmlDocPtr docp UNUSED,
			    xmlNodePtr nodep)
{
    slaxWrite(swp, "fallback {");
    slaxWriteNewline(swp, NEWL_INDENT);

    slaxWriteAllNs(swp, docp, nodep);
    slaxWriteChildren(swp, docp, nodep, FALSE);

    slaxWrite(swp, "}");
    slaxWriteNewline(swp, NEWL_OUTDENT);
}

static void
slaxWriteComplexElement (slax_writer_t *swp, xmlDocPtr docp UNUSED,
			 xmlNodePtr nodep, const char *stmt, const char *arg,
			 int oneliner)
{
    slaxWrite(swp, "%s%s%s", stmt, arg ? " " : "", arg ?: "");

    if (nodep->children == NULL) {
	slaxWrite(swp, ";");
	slaxWriteNewline(swp, 0);
	return;
    }

    if (oneliner && !slaxNeedsBraces(nodep)) {
	if (arg == NULL)
	    slaxWrite(swp, " ");
	slaxWriteContent(swp, docp, nodep);
	slaxWrite(swp, ";");
	slaxWriteNewline(swp, 0);
	return;
    }

    slaxWrite(swp, " {");
    slaxWriteNewline(swp, NEWL_INDENT);

    slaxWriteAllNs(swp, docp, nodep);
    slaxWriteChildren(swp, docp, nodep, FALSE);

    slaxWrite(swp, "}");
    slaxWriteNewline(swp, NEWL_OUTDENT);
}

static void
slaxWriteMessage (slax_writer_t *swp, xmlDocPtr docp UNUSED,
			    xmlNodePtr nodep)
{
    char *term = slaxGetAttrib(nodep, ATT_TERMINATE);
    const char *stmt = (term && streq(term, "yes")) ? "terminate" : "message";

    slaxWriteComplexElement(swp, docp, nodep, stmt, NULL, TRUE);

    xmlFreeAndEasy(term);
}

static void
slaxWriteProcessingInstruction (slax_writer_t *swp, xmlDocPtr docp UNUSED,
			    xmlNodePtr nodep)
{
    char *name = slaxGetAttrib(nodep, ATT_NAME);
    char *content =  slaxMakeAttribValueTemplate(swp, nodep, name);

    slaxWriteComplexElement(swp, docp, nodep,
			    "processing-instruction", content, FALSE);

    xmlFreeAndEasy(content);
    xmlFreeAndEasy(name);
}

typedef void (*slax_func_t)(slax_writer_t *, xmlDocPtr, xmlNodePtr);

typedef struct slax_func_table_s {
    const char *sft_name;	/* Element name (xsl:*) */ 
    slax_func_t sft_func;	/* Function to handle that element */
    unsigned sft_flags;		/* Flags for this statement */
} slax_func_table_t;

/* Flags for sft_flags */
#define SFTF_V11	(1<<0)	/* Statement is 1.1 and later (not 1.0) */
slax_func_table_t slax_func_table[] = {
    { "apply-imports", slaxWriteElementSimple, 0 },
    { "apply-templates", slaxWriteApplyTemplates, 0 },
    { "attribute", slaxWriteAttributeStatement, SFTF_V11 },
    { "attribute-set", slaxWriteAttributeSetStatement, SFTF_V11 },
    { "call-template", slaxWriteCallTemplate, 0 },
    { "choose", slaxWriteChoose, 0 },
    { "copy-of", slaxWriteCopyOf, 0 },
    { "copy", slaxWriteCopyNode, SFTF_V11 },
    { "comment", slaxWriteCommentStatement, 0 },
    { "decimal-format", slaxWriteDecimalFormat, SFTF_V11 },
    { "element", slaxWriteElementStatement, SFTF_V11 },
    { "fallback", slaxWriteFallback, SFTF_V11 },
    { "for-each", slaxWriteForEach, 0 },
    { "if", slaxWriteIf, 0 },
    { "import", slaxWriteImport, 0 },
    { "include", slaxWriteImport, 0 },
    { "key", slaxWriteKey, SFTF_V11 },
    { "message", slaxWriteMessage, SFTF_V11 },
    { "namespace-alias", NULL, 0 }, /* Skip them; handled in slaxWriteNsAll */
    { "number", slaxWriteNumber, SFTF_V11 },
    { "output", slaxWriteOutputMethod, SFTF_V11 },
    { "param", slaxWriteParam, 0 },
    { "preserve-space", slaxWritePreAndStripSpace, 0 },
    { "processing-instruction", slaxWriteProcessingInstruction, 0 },
    { "sort", slaxWriteSort, SFTF_V11 },
    { "strip-space", slaxWritePreAndStripSpace, 0 },
    { "template", slaxWriteTemplate, 0 },
    { "text", slaxWriteText, 0 },
    { "value-of", slaxWriteValueOf, 0 },
    { "variable", slaxWriteVariable, 0 },
    { NULL, NULL, 0 }
};

/*
 * Values for *statep;  This state variable is meant to record
 * where we are in the flow of a block; have we seen the first
 * set of variable or parameter declarations?  have we seen the
 * end of them?  It's just a style nit, but we want to emit a
 * blank line after the first set of variable and parameter
 * declarations.
 */
#define STATE_ZERO	0	/* Initial state; haven't seen anything */
#define STATE_IN_DECLS	1	/* Seen one var or param */
#define STATE_PAST_DECLS 2	/* Seen something after var/params */

static void
slaxWriteXslElement (slax_writer_t *swp, xmlDocPtr docp,
		     xmlNodePtr nodep, int *statep)
{
    const char *name = (const char *) nodep->name;
    slax_func_table_t *sftp;
    slax_func_t func = slaxWriteElement;

    if (statep) {
	if (*statep == STATE_PAST_DECLS) {
	    /* nothing */

	} else if (nodep->type == XML_COMMENT_NODE) {
	    /* nothing */

	} else if (streq(name, "variable") || streq(name, "param")) {
	    *statep = STATE_IN_DECLS;

	} else if (*statep == STATE_IN_DECLS) {
	    if (slaxNeedsBlankline(nodep))
		slaxWriteBlankline(swp);
	    *statep = STATE_PAST_DECLS;
	}
    }

    for (sftp = slax_func_table; sftp->sft_name; sftp++) {
	/* If it's a 1.1 feature and we're writing 1.0 SLAX, skip it */
	if ((sftp->sft_flags & SFTF_V11) && slaxV10(swp))
	    continue;

	if (streq(sftp->sft_name, name))
	    func = sftp->sft_func;
    }

    if (func)
	(*func)(swp, docp, nodep);
}

static void
slaxWriteComment (slax_writer_t *swp, xmlDocPtr docp UNUSED, xmlNodePtr nodep)
{
    char *cp = (char *) nodep->content;
    int first = TRUE;
    char buf[BUFSIZ], *bp = buf, *ep = buf + sizeof(buf);

    if (cp == NULL || *cp == '\0')
	return;

    cp += strspn(cp, " \t");

    if (cp[0] == '/' && cp[1] == '*') {
	cp += 2;
	cp += strspn(cp, " \t");
    }

    for (; *cp; cp++) {
	if (*cp == '\n' || bp + 1 > ep) {
	    /*
	     * Flush the current line out, after trimming any
	     * trailing whitespace.
	     */
	    while (bp > buf && bp[-1] == ' ')
		bp -= 1;
	    *bp = '\0';

	    if (first)
		slaxWriteNewline(swp, 0);
	    slaxWrite(swp, "%s%s%s", first ? "/*" : "",
		      (first && *buf) ? " " : "", buf);
	    slaxWriteNewline(swp, 0);
	    bp = buf;
	    if (cp[1]) {
		int move = strspn(cp + 1, " \t");
		if (move > 0 && cp[move] && cp[move + 1] == '*')
		    move -= 1;
		cp += move;
	    }
		    
	    first = FALSE;
	    continue;

	} else if (*cp == '/') {
	    if (cp[1] == '*') {
		*bp++ = *cp;
		*bp++ = ' ';
		continue;
	    }
	} else if (*cp == '*') {
	    if (cp[1] == '/') {
		int len = strlen(cp + 2);
		int wlen = strspn(cp + 2, " \t\n\r");

		if (len == wlen)
		    break;

		*bp++ = *cp;
		*bp++ = ' ';
		continue;
	    }
	} else if (*cp == '\r') {
	    continue;
	}

	*bp++ = *cp;
    }

    while (bp > buf && bp[-1] == ' ')
	bp -= 1;

    *bp = '\0';
    slaxWrite(swp, "%s%s */", first ? "/* " : "", buf);
    slaxWriteNewline(swp, 0);
}

static void
slaxWriteCommentStatement (slax_writer_t *swp, xmlDocPtr docp, xmlNodePtr nodep)
{
    slaxWrite(swp, "comment");

    if (nodep->children == NULL) {
	slaxWrite(swp, ";");
	slaxWriteNewline(swp, 0);
	return;
    }
	
    slaxWrite(swp, " ");

    if (!slaxNeedsBraces(nodep)) {
	slaxWriteContent(swp, docp, nodep);
	slaxWrite(swp, ";");
	slaxWriteNewline(swp, 0);
	return;
    }

    slaxWrite(swp, "{");
    slaxWriteNewline(swp, NEWL_INDENT);

    slaxWriteAllNs(swp, docp, nodep);
    slaxWriteChildren(swp, docp, nodep, FALSE);

    slaxWrite(swp, "}");
    slaxWriteNewline(swp, NEWL_OUTDENT);
}

/**
 * slaxWriteChildren: recursively write the contents of the children
 * of the current node
 *
 * @param swp output stream writer
 * @param docp current document
 * @param nodep current node
 * @param initializer are we called to emit the initial value of param or var?
 */
static void
slaxWriteChildren (slax_writer_t *swp, xmlDocPtr docp, xmlNodePtr nodep,
		   int initializer)
{
    xmlNodePtr childp;
    int state = 0;

    for (childp = nodep->children; childp; childp = childp->next) {
	/*
	 * Do not recurse if we've already seen write errors
	 */
	if (swp->sw_errors)
	    return;

	switch (childp->type) {
	case XML_TEXT_NODE:
	    if (!slaxIsWhiteString(childp->content)) {
		slaxWriteExpr(swp, childp->content, initializer, FALSE);
		state = STATE_PAST_DECLS;
	    }
	    break;

	case XML_ELEMENT_NODE:
	    if (slaxIsXsl(childp)) {
		slaxWriteXslElement(swp, docp, childp, &state);

	    } else if (!slaxV10(swp) && childp->ns && childp->ns->href
		       && streq((const char *) childp->ns->href,
				(const char *) FUNC_URI)) {
		slaxWriteFunctionElement(swp, docp, childp);


	    } else if (!slaxV10(swp) && childp->ns && childp->ns->href
		       && streq((const char *) childp->ns->href, SLAX_URI)) {
		slaxWriteSlaxElement(swp, docp, childp);

	    } else {
		if (state == STATE_IN_DECLS && slaxNeedsBlankline(childp))
		    slaxWriteBlankline(swp);

		slaxWriteElement(swp, docp, childp);
		state = STATE_PAST_DECLS;
	    }

	    break;

	case XML_COMMENT_NODE:
	    slaxWriteComment(swp, docp, childp);
	    break;

	case XML_PI_NODE:
	default:
	    /* ignore */;
	}
    }
}

static void
slaxWriteCleanup (slax_writer_t *swp)
{
    if (swp->sw_buf) {
	xmlFree(swp->sw_buf);
	swp->sw_buf = NULL;
    }
}

/**
 * slaxWriteDoc:
 * Write an XSLT document in SLAX format
 * @param func fprintf-like callback function to write data
 * @param data data passed to callback
 * @param docp source document (XSLT stylesheet)
 * @param partial Should we write partial (snippet) output?
 * @param version Version number to use
 */
int
slaxWriteDoc (slaxWriterFunc_t func, void *data, xmlDocPtr docp,
	      int partial,  const char *version)
{
    xmlNodePtr nodep;
    xmlNodePtr childp;
    slax_writer_t sw;

    bzero(&sw, sizeof(sw));
    sw.sw_write = func;
    sw.sw_data = data;

    nodep = xmlDocGetRootElement(docp);
    if (nodep == NULL || nodep->name == NULL)
	return 1;

    /* If the user asked for version 1.0, we avoid 1.1 features */
    if (version && streq(version, "1.0"))
	sw.sw_flags |= SWF_VERS_10;

    if (!partial) {
	slaxWrite(&sw, "version %s;", version ?: "1.1");
	slaxWriteNewline(&sw, 0);
	slaxWriteNewline(&sw, 0);
    }

    /*
     * Write out all top-level comments before doing anything else
     */
    for (childp = docp->children; childp; childp = childp->next)
	if (childp->type == XML_COMMENT_NODE)
	    slaxWriteComment(&sw, docp, childp);

    slaxWriteAllNs(&sw, docp, nodep);

    if (streq((const char *) nodep->name, ELT_STYLESHEET)
		|| streq((const char *) nodep->name, ELT_TRANSFORM)) {
	slaxWriteChildren(&sw, docp, nodep, FALSE);

    } else if (partial) {
	slaxWriteElement(&sw, docp, nodep);

    } else {
	/*
	 * The input document isn't an XSLT transform or stylesheet, so
	 * it must be a "iteral result element".  See the XSLT spec:
	 * [7.1.1 Literal Result Elements]).
	 */
	slaxWrite(&sw, "match / {");
	slaxWriteNewline(&sw, NEWL_INDENT);

	slaxWriteElement(&sw, docp, nodep);

	slaxWrite(&sw, "}");
	slaxWriteNewline(&sw, NEWL_OUTDENT);
    }

    slaxWriteCleanup(&sw);

    return (sw.sw_errors == 0);
}


/**
 * See if we need to rewrite a function call into something else.
 * The only current case is "..."/slax:build-sequnce.
 */
int
slaxWriteRedoFunction (slax_data_t *sdp UNUSED, const char *func,
		       slax_string_t *xpath)
{
    slax_string_t *cur, *prev;
    int depth;

    slaxLog("slaxWriteFunction: function %s", func);

    if (xpath && streq(func, SLAX_PREFIX ":" FUNC_BUILD_SEQUENCE)) {
	slaxLog("slaxWriteFunction: %s ... ??? ",
		  xpath ? xpath->ss_token : "");

	for (depth = 0, prev = xpath, cur = xpath->ss_next; cur;
			prev = cur, cur = cur->ss_next) {
	    if (streq(cur->ss_token, ",") && depth == 0) {
		slax_string_t *dotdotdot;

		dotdotdot = slaxStringLiteral("...", L_DOTDOTDOT);

		if (dotdotdot) {
		    /* Rewrite the link of strings */
		    prev->ss_next = dotdotdot;
		    dotdotdot->ss_next = cur->ss_next;
		    cur->ss_next = NULL;
		    slaxStringFree(cur);
		    return TRUE;
		}

	    } else if (streq(cur->ss_token, "(")) {
		depth += 1;
	    } else if (streq(cur->ss_token, ")")) {
		depth -= 1;
	    }
	}
    }

    return FALSE;
}

static slax_string_t *
slaxWriteAddTailLiteral (slax_string_t ***tailp, const char *str, int ttype)
{
    slax_string_t *ssp = slaxStringLiteral(str, ttype);

    if (ssp) {
	**tailp = ssp;
	*tailp = &ssp->ss_next;
    }

    return ssp;
}

static void
slaxWriteAddTailAppend (slax_string_t ***tailp, slax_string_t *ssp)
{
    **tailp = ssp;
    while (ssp->ss_next)	/* Follow to the end of the string */
	ssp = ssp->ss_next;
    *tailp = &ssp->ss_next;
}

static void
slaxWriteTernaryExpression (slax_data_t *sdp, xmlNodePtr cur,
			    const char *xpath, slax_string_t ***tailp)
{
    slax_string_t *expr;
    slax_writer_t sw;

    bzero(&sw, sizeof(sw));
    sw.sw_filename = (const char *) cur->doc->name;

    expr = slaxMakeExpressionString(&sw, cur, xpath);
    sdp->sd_errors += sw.sw_errors;

    if (expr) {
	slaxWriteAddTailAppend(tailp, expr);

    } else {
	slaxStringAddTail(tailp, NULL, xpath, strlen(xpath), M_TERNARY);
    }
}

/*
 * Find a child with the given name under the given parent.
 */
static xmlNodePtr
slaxWriteFind (xmlNodePtr nodep, const char *name)
{
    xmlNodePtr cur;

    for (cur = nodep->children; cur; cur = cur->next) {
	if (cur->type != XML_ELEMENT_NODE)
	    continue;

	if (cur->ns && streq((const char *) cur->name, name)
		&& slaxIsXsl(cur))
	    return cur;
    }

    return NULL;
}

/*
 * Return the ternary expresion as a string, instead of the expand
 * content pointed to by cur.
 */
static slax_string_t *
slaxWriteTernaryAsString (slax_data_t *sdp, xmlNodePtr cur)
{
    slax_string_t *first = NULL, **tail = &first;
    xmlNodePtr varp, choosep, whenp, otherp;
    char *teststr, *whenstr, *otherstr;

    varp = slaxWriteFind(cur, ELT_VARIABLE);
    choosep = slaxWriteFind(cur, ELT_CHOOSE);
    if (choosep == NULL)
	return NULL;

    whenp = slaxWriteFind(choosep, ELT_WHEN);
    otherp = slaxWriteFind(choosep, ELT_OTHERWISE);
    if (whenp == NULL || otherp == NULL)
	return NULL;

    teststr = varp ? slaxGetAttrib(varp, ATT_SELECT)
	: slaxGetAttrib(whenp, ATT_TEST);
    if (teststr == NULL)
	return NULL;

    whenp = slaxWriteFind(whenp, ELT_COPY_OF);
    otherp = slaxWriteFind(otherp, ELT_COPY_OF);
    if (whenp && otherp) {
	otherstr = slaxGetAttrib(otherp, ATT_SELECT);
	whenstr = varp ? NULL : slaxGetAttrib(whenp, ATT_SELECT);
	
	/*
	 * At this point we have the expressions for the three
	 * operands of "?:".
	 */
	slaxWriteTernaryExpression(sdp, varp ?: whenp, teststr, &tail);
	slaxWriteAddTailLiteral(&tail, "?", L_QUESTION);
	if (whenstr)
	    slaxWriteTernaryExpression(sdp, whenp, whenstr, &tail);
	slaxWriteAddTailLiteral(&tail, ":", L_COLON);
	slaxWriteTernaryExpression(sdp, otherp, otherstr, &tail);
    
	xmlFreeAndEasy(whenstr);
	xmlFreeAndEasy(otherstr);
    }

    xmlFreeAndEasy(teststr);

    return first;
}

/*
 * Rewrite a ternary ("?:") expression as a string and return
 * the string.
 */
slax_string_t *
slaxWriteRedoTernary (slax_data_t *sdp, slax_string_t *func)
{
    slax_string_t *var, *newp;
    char *vname;
    xmlNodePtr nodep, cur;

    /* Sanity checks */
    if (sdp->sd_nodep == NULL || func == NULL
	|| func->ss_ttype != T_FUNCTION_NAME
	|| func->ss_next == NULL || func->ss_next->ss_next == NULL)
	return NULL;

    if (!streq(func->ss_token, SLAX_TERNARY_FUNCTION))
	return NULL;

    var = func->ss_next->ss_next;
    vname = var->ss_token + 1;
    nodep = sdp->sd_nodep;
    slaxLog("slaxTernaryCheck: %s", vname);

    for (cur = nodep->prev; cur; cur = cur->prev) {
	if (cur->type == XML_ELEMENT_NODE) {
	    char *cp = slaxGetAttrib(cur, ATT_NAME);
	    if (cp && streq(cp, vname)) {
		xmlFree(cp);
		newp = slaxWriteTernaryAsString(sdp, cur);
		if (newp) {
		    slaxStringFree(func);
		    return newp;
		}
		return NULL;
	    }
	}
    }

    return NULL;
}

/*
 * Rewrite a concat function call expression as a string and return
 * the string.
 */
slax_string_t *
slaxWriteRedoConcat (slax_data_t *sdp UNUSED, slax_string_t *func)
{
    slax_string_t *ssp;
    int level = 0;

    if (func == NULL || func->ss_next == NULL || func->ss_next->ss_next == NULL
	|| !streq(func->ss_token, "concat"))
	return NULL;

    for (ssp = func; ssp->ss_next; ssp = ssp->ss_next) {
	if (ssp->ss_ttype == L_COMMA) {
	    if (level == 1) {
		ssp->ss_ttype = L_UNDERSCORE;
		ssp->ss_token[0] = '_';
	    }
	} else if (ssp->ss_ttype == L_OPAREN) {
	    level += 1;
	} else if (ssp->ss_ttype == L_CPAREN) {
	    level -= 1;
	}
    }

    ssp = func->ss_next;
    func->ss_next = NULL;
    slaxStringFree(func);	/* Free leading concat token */

    return ssp;
}

/*
 * The logic in slaxWriteRedoConcat that unthreads concat() functions
 * into the "_" operator does not know if the resulting expression
 * needs parens.  So it has to leave them (really just leaving the
 * parens from the "concat(...)" invocation).  When the expression
 * is completely, we are called to remove that extraneous set of
 * parens.  We know the first chunk of expr is a L_OPAREN, but need
 * to look at the rest to ensure the parens are truly extraneous.
 * If so, peel them off and return the rest of the expression.
 */
slax_string_t *
slaxWriteRemoveParens (slax_data_t *sdp UNUSED, slax_string_t *expr)
{
    slax_string_t *ssp, *last = NULL;
    int parens = 1;

    for (ssp = expr->ss_next; ssp->ss_next; ssp = ssp->ss_next) {
	if (ssp->ss_ttype == L_OPAREN)
	    parens += 1;
	else if (ssp->ss_ttype == L_CPAREN)
	    if (--parens == 0)
		return expr;
	last = ssp;
    }

    if (ssp->ss_ttype != L_CPAREN || parens != 1 || last == NULL)
	return expr;

    /* Free the trailing L_CPAREN */
    last->ss_next = NULL;
    slaxStringFree(ssp);

    /* Free the leading L_OPAREN) */
    ssp = expr->ss_next;
    expr->ss_next = NULL;
    slaxStringFree(expr);
    return ssp;
}

#ifdef UNIT_TEST

int
main (int argc, char **argv)
{
    xmlDocPtr docp;
    int ch;
    const char *filename = "-";

    while ((ch = getopt(argc, argv, "df:y")) != EOF) {
        switch (ch) {
        case 'd':
	    slaxLogIsEnabled = TRUE;
	    break;

        case 'f':
	    filename = optarg;
	    break;

        case 'y':		/* Ignore this: compatibility w/ slaxloader */
	    break;

	default:
	    fprintf(stderr, "invalid option\n");
	    return -1;
	}
    }

    /*
     * Start the XML API
     */
    xmlInitParser();
    slaxEnable(SLAX_DISABLE);

    docp = xmlReadFile(filename, NULL, XSLT_PARSE_OPTIONS);
    if (docp == NULL) {
	xsltTransformError(NULL, NULL, NULL,
                "xsltParseStylesheetFile : cannot parse %s\n", filename);
        return -1;
    }

    slaxWriteDoc((slaxWriterFunc_t) fprintf, stdout, docp, 0, NULL);

    return 0;
}

#endif /* UNIT_TEST */
