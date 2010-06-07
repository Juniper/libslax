/*
 * $Id: slaxwriter.c,v 1.3 2006/11/01 21:27:20 phil Exp $
 *
 * Copyright (c) 2006-2008, Juniper Networks, Inc.
 * All rights reserved.
 * See ./Copyright for the status of this software
 */

#include "slaxinternals.h"
#include <libslax/slax.h>
#include "slaxparser.h"

#define BUF_EXTEND 2048		/* Bump the buffer by this amount */

typedef struct slaxWriter_s {
    const char *sw_filename;	/* Filename being parsed */
    slaxWriterFunc_t sw_write;	/* Client callback */
    void *sw_data;		/* Client data */
    int sw_indent;		/* Indentation count */
    char *sw_buf;		/* Output buffer */
    int sw_cur;			/* Current index in output buffer */
    int sw_bufsiz;		/* Size of the buffer in sw_buf[] */
    int sw_errors;		/* Errors reading or writing data */
    unsigned sw_flags;		/* Flags for this instance (SWF_*) */
} slaxWriter_t;

#define SWF_BLANKLINE	(1<<0)	/* Just wrote a blank line */

/* Forward function declarations */
static void slaxWriteChildren(slaxWriter_t *, xmlDocPtr, xmlNodePtr, int);
static void slaxWriteXslElement(slaxWriter_t *swp, xmlDocPtr docp,
				xmlNodePtr nodep, int *statep);

static void slaxWriteCommentStatement (slaxWriter_t *, xmlDocPtr, xmlNodePtr);

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
slaxWriteNewline (slaxWriter_t *swp, int change)
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
slaxWriteBlankline (slaxWriter_t *swp)
{
    if (swp->sw_flags & SWF_BLANKLINE)
	return 0;

    return slaxWriteNewline(swp, 0);
}

static char *
slaxWriteRealloc (slaxWriter_t *swp, int need)
{
    char *cp;

    cp = xmlRealloc(swp->sw_buf, need);
    if (cp == NULL) {
	slaxTrace("memory allocation failure");
	swp->sw_errors += 1;
	return NULL;
    }
    
    swp->sw_buf = cp;
    swp->sw_bufsiz = need;
    return cp;
}

static void
slaxWrite (slaxWriter_t *swp, const char *fmt, ...)
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
nsIsMember (const xmlChar *prefix, xmlChar *list)
{
    const char delims[] = " \t\n";
    xmlChar *cp, *next;
    int len = strlen((const char *) prefix);

    for (cp = list; *cp; cp = next) {
	next = cp + strcspn((char *) cp, delims);
	if (len == next - cp && *prefix == *cp
	        && strncmp((const char *) prefix,
			   (const char *) cp, next - cp) == 0)
	    return TRUE;

	/*
	 * If we're not at the end of the string, more over any whitespace
	 */
	if (*next)
	    next += strspn((char *) next, delims);
    }

    return FALSE;
}

#if 0
static int
isNewLine (const xmlChar *str)
{
    for ( ; *str; str++)
	if (*str == '\r'  || *str == '\n')
	    return TRUE;

    return FALSE;
}
#endif

static int
isWhiteString (const xmlChar *str)
{
    for ( ; *str; str++)
	if (*str != ' ' && *str != '\t' && *str != '\r'  && *str != '\n')
	    return FALSE;

    return TRUE;
}

#if 0
static int
slaxPrefixNeedsQuotes (const xmlChar *prefix)
{
    const unsigned char *cp = (const unsigned char *) prefix;

    for ( ; *cp; cp++)
	if (*cp < 0x20 || *cp > 0x7F)
	    return TRUE;
    return FALSE;
}
#endif

static void
slaxWriteAllNs (slaxWriter_t *swp, xmlDocPtr docp UNUSED, xmlNodePtr nodep)
{
    xmlChar *excludes  = xmlGetProp(nodep,
			     (const xmlChar *) ATT_EXCLUDE_RESULT_PREFIXES);
    xmlChar *extensions = xmlGetProp(nodep,
			     (const xmlChar *) ATT_EXTENSION_ELEMENT_PREFIXES);
    int hit = 0;

    xmlNsPtr cur;

    for (cur = nodep->nsDef; cur; cur = cur->next) {
	if (streq((const char *) cur->prefix, XSL_PREFIX))
	    continue;

	if (cur->prefix) {
	    const char *tag1 = "";
	    const char *tag2 = "";

	    slaxWrite(swp, "ns %s ", cur->prefix);
	    if (excludes && nsIsMember(cur->prefix, excludes))
		tag1 = "exclude ";
	    if (extensions && nsIsMember(cur->prefix, extensions))
		tag2 = "extension ";
	    slaxWrite(swp, "%s%s= \"%s\";", tag1, tag2, cur->href);
	} else slaxWrite(swp, "ns \"%s\";", cur->href);

	slaxWriteNewline(swp, 0);
	hit += 1;
    }

    if (hit)
	slaxWriteNewline(swp, 0);

    xmlFreeAndEasy(excludes);
    xmlFreeAndEasy(extensions);
}

static char *
slaxWriteCheckRoom (slaxWriter_t *swp, int space)
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
slaxWriteEscapedChar (slaxWriter_t *swp, const char *inp, unsigned flags)
{
    char *outp;
    int ch = *inp;

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
slaxWriteEscaped (slaxWriter_t *swp, const char *str, unsigned flags)
{
    const char *inp;
    char *outp;

    outp = slaxWriteCheckRoom(swp, 1); /* Save room for trailing NUL */
    if (outp == NULL)
	return;

    for (inp = str; *inp; inp++)
	inp = slaxWriteEscapedChar(swp, inp, flags);
}

static void slaxWriteExpr (slaxWriter_t *swp, xmlChar *content,
			   int initializer)
{
    slaxWrite(swp, "%s\"", initializer ? "" : "expr ");
    slaxWriteEscaped(swp, (char *) content, SEF_TEXT);
    slaxWrite(swp, "\";");
    slaxWriteNewline(swp, 0);
}

static void
slaxWriteText (slaxWriter_t *swp, xmlDocPtr docp UNUSED, xmlNodePtr nodep)
{
    xmlNodePtr childp;

    for (childp = nodep->children; childp; childp = childp->next) {
	if (childp->type == XML_TEXT_NODE)
	    slaxWriteExpr(swp, childp->content, FALSE);
    }
}

/*
 * isDisableOutputEscaping:
 *   See if the "disable-output-escaping" attribute is set to "yes".
 * SLAX uses quoted text to represent text data (i.e. "foo") and
 * inline XPath expressions.  But if the disable-output-escaping attribute
 * is set, we don't want to do this, since it'll wreck everyone's fun.
 * So we test for it and the caller should do something sensible if we
 * return TRUE.
 */
static int
isDisableOutputEscaping (xmlNodePtr nodep)
{
    int rc;
    xmlChar *dis = xmlGetProp(nodep,
			   (const xmlChar *) ATT_DISABLE_OUTPUT_ESCAPING);
    if (dis == NULL)
	return FALSE;

    rc = streq((char *) dis, "yes");

    xmlFreeAndEasy(dis);
    return rc;
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
	    if (!isWhiteString(childp->content))
		if (hits++)
		    return TRUE;
	    continue;
	}

	if (childp->type == XML_ELEMENT_NODE
		&& childp->ns && childp->ns->prefix
	    && streq((const char *) childp->ns->prefix, XSL_PREFIX)) {

	    if (streq((const char *) childp->name, ELT_VALUE_OF)
		    || streq((const char *) childp->name, ELT_TEXT)) {
		if (isDisableOutputEscaping(childp))
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
	    if (isWhiteString(childp->content))
		continue;
	    if (count++)
		return TRUE;
	}

	if (childp->type == XML_ELEMENT_NODE) {
	    /*
	     * If this is an XSLT element, we need to bust it out
	     */
	    if (childp->ns && childp->ns->href
		&& streq((const char *) childp->ns->href, XSL_NS))
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

static void
slaxWriteValue (slaxWriter_t *swp, const char *value)
{
    static const char concat[] = "concat(";
    int in_quotes = 0;
    int parens = 1;
    const char *cp;
    const char *sp = NULL;

    if (strncmp(concat, value, sizeof(concat) - 1) != 0) {
	if (value[0] == '\'' || value[0] == '\"') {
	    /*
	     * The initializer is a static string in quotes.  Nuke the
	     * quotes and emit it as an escaped string.
	     */
	    int len = strlen(value + 1);
	    char *mine = alloca(len);
	    memcpy(mine, value + 1, len - 1);
	    mine[len - 1] = '\0';

	    slaxWrite(swp, "\"");
	    slaxWriteEscaped(swp, mine, SEF_ATTRIB);
	    slaxWrite(swp, "\"");

	} else slaxWrite(swp, "%s", value);
	return;
    }

    /*
     * Tease a concat() invocation into the "_" form.
     */
    for (cp = value + sizeof(concat) - 1, sp = cp; *cp; cp++) {
	if (in_quotes) {
	    if (*cp == '\\') {	/* Skip anything backslashed */
		cp += 1;
		continue;
	    }

	    if (*cp != in_quotes) /* Skip is we're in quotes */
		continue;

	    in_quotes = 0;

	} else if (*cp == '\'' || *cp == '\"') {
	    in_quotes = *cp;

	} else if (*cp == ',' && parens == 1) {
	    if (sp) {
		slaxWrite(swp, "%.*s", cp - sp, sp);
		sp = cp + 1;
	    }

	    slaxWrite(swp, " _ ");

	} else if (*cp == ' ' || *cp == '\t') {
	    if (cp == sp)
		sp += 1;	/* Trim leading whitespace */

	} else if (*cp == '(' || *cp == '[') {
	    parens += 1;

	} else if (*cp == ']' || *cp == ')') {
	    parens -= 1;
	    if (parens == 0)
		break;
	}
    }

    /* If there's anything left over in the concat() call, write it out */
    if (sp != cp) {
	slaxWrite(swp, "%.*s", cp - sp, sp);
    }

    /*
     * If there's anything left over from _outside_ the concat() call
     * (besides white space), write it out.
     */
    if (*cp) {
	cp += 1;
	cp += strspn(cp, " \t\n");
	if (*cp)
	    slaxWrite(swp, " _ %s", cp);
    }

    if (slaxDebug)
	slaxWrite(swp, "/*%s*/", value);
}

/*
 * Turn an XPath expression into a SLAX one
 * Returns a freshly allocated string, or NULL
 */
static char *
slaxMakeExpression (slaxWriter_t *swp, xmlNodePtr nodep, const char *xpath)
{
    slax_data_t sd;
    int rc;
    xmlParserCtxtPtr ctxt;
    xmlNodePtr fakep = NULL;
    char *buf;

    if (xpath == NULL || *xpath == '\0')
	return (char *) xmlCharStrdup("\"\"");

    ctxt = xmlNewParserCtxt();
    if (ctxt == NULL)
	return NULL;

    bzero(&sd, sizeof(sd));
    sd.sd_parse = sd.sd_ttype = M_PARSE_XPATH; /* We want to parse XPath */
    sd.sd_ctxt = ctxt;
    sd.sd_flags |= SDF_NO_SLAX_KEYWORDS;

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

    sd.sd_xsl_ns = xmlNewNs(fakep, (const xmlChar *) XSL_NS,
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

    xmlFreeParserCtxt(ctxt);
    ctxt = NULL;

    if (sd.sd_errors) {
	swp->sw_errors += sd.sd_errors;
	xmlParserError(ctxt, "%s: %d error%s detected during parsing\n",
		sd.sd_filename, sd.sd_errors, (sd.sd_errors == 1) ? "" : "s");
	goto fail;
    }

    if (sd.sd_xpath == NULL) {
	/*
	 * Something's wrong so we punt and return the original string
	 */
	slaxTrace("slax: xpath conversion failed: nothing returned");
	swp->sw_errors += 1;
	goto fail;
    }

    buf = slaxStringAsChar(sd.sd_xpath, SSF_QUOTES);

    if (buf == NULL) {
	slaxTrace("slax: xpath conversion failed: no buffer");
	goto fail;
    }

    slaxTrace("slax: xpath conversion: %s", buf);
    slaxStringFree(sd.sd_xpath);
    xmlFreeNode(fakep);

    return buf;

 fail:
    if (ctxt)
	xmlFreeParserCtxt(ctxt);
    if (fakep)
	xmlFreeNode(fakep);

    /*
     * SLAX XPath expressions are completely backwards compatible
     * with XSLT, so we can make an ugly version of the expression
     * by simply returning a copy of our input.  You get "and" instead
     * of "&&", but it's safe enough.
     */
    return (char *) xmlCharStrdup(xpath);
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
slaxMakeAttribValueTemplate (slaxWriter_t *swp, xmlNodePtr nodep,
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
slaxWriteContent (slaxWriter_t *swp, xmlDocPtr docp UNUSED, xmlNodePtr nodep)
{
    xmlNodePtr childp;
    int first = TRUE;

    for (childp = nodep->children; childp; childp = childp->next) {

	if (childp->type == XML_TEXT_NODE) {
	    if (!isWhiteString(childp->content)) {
		if (!first)
		    slaxWrite(swp, " _ ");
		else first = FALSE;
		slaxWrite(swp, "\"");
		slaxWriteEscaped(swp, (char *) childp->content, SEF_ATTRIB);
		slaxWrite(swp, "\"");
	    }
	    continue;
	}

	if (childp->type == XML_ELEMENT_NODE
	    && childp->ns && childp->ns->prefix
	    && streq((const char *) childp->ns->prefix, XSL_PREFIX)) {

	    if (streq((const char *) childp->name, ELT_VALUE_OF)) {
		char *sel = (char *) xmlGetProp(childp,
					  (const xmlChar *) ATT_SELECT);
		char *expr = slaxMakeExpression(swp, nodep, sel);

		if (!first)
		    slaxWrite(swp, " _ ");
		else first = FALSE;
		slaxWriteValue(swp, expr ?: UNKNOWN_EXPR);
		xmlFreeAndEasy(expr);
		xmlFreeAndEasy(sel);

	    } else if (streq((const char *) childp->name, ELT_TEXT)) {
		xmlNodePtr gcp;

		for (gcp = childp->children; gcp; gcp = gcp->next)
		    if (gcp->type == XML_TEXT_NODE
			&& !isWhiteString(gcp->content)) {
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
slaxWriteElement (slaxWriter_t *swp, xmlDocPtr docp, xmlNodePtr nodep)
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
slaxWriteElementSimple (slaxWriter_t *swp, xmlDocPtr docp UNUSED,
			xmlNodePtr nodep)
{
    slaxWrite(swp, "%s;", nodep->name);
    slaxWriteNewline(swp, 0);
}

static void
slaxWriteImport (slaxWriter_t *swp, xmlDocPtr docp UNUSED, xmlNodePtr nodep)
{
    char *href = (char *) xmlGetProp(nodep, (const xmlChar *) ATT_HREF);

    slaxWrite(swp, "%s \"%s\";", nodep->name, href ? href : "????");
    slaxWriteNewline(swp, 0);

    xmlFreeAndEasy(href);
}

static void
slaxWriteNamedTemplateParams (slaxWriter_t *swp, xmlDocPtr docp,
			      xmlNodePtr nodep, int all, int complex)
{
    int first = TRUE;
    xmlNodePtr childp;

    for (childp = nodep->children; childp; childp = childp->next) {
	char *name, *rname;
	char *sel;

	if (childp->type != XML_ELEMENT_NODE
	    || childp->ns == NULL || childp->ns->prefix == NULL
	    || !streq((const char *) childp->ns->prefix, XSL_PREFIX)
	    || !streq((const char *) childp->name, ELT_PARAM))
	    continue;

	sel = (char *) xmlGetProp(childp, (const xmlChar *) ATT_SELECT);
	if (sel || childp->children == NULL) {
	    if (complex && !all) {
		xmlFreeAndEasy(sel);
		continue;
	    }

	    name = (char *) xmlGetProp(childp, (const xmlChar *) ATT_NAME);
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

	    name = (char *) xmlGetProp(childp, (const xmlChar *) ATT_NAME);
	    rname = (name && *name == '$') ? name + 1 : name;

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
 * @nodep: the node
 * Returns TRUE if the element should be preceeded by a blank line
 */
static int
slaxNeedsBlankline (xmlNodePtr nodep)
{
    if (nodep->prev == NULL)
	return TRUE;

    /* Allow whitespace-only text nodes */
    for (nodep = nodep->prev; nodep && nodep->type == XML_TEXT_NODE;
	 nodep = nodep->prev)
	if (!isWhiteString(nodep->content))
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
slaxWriteTemplate (slaxWriter_t *swp, xmlDocPtr docp, xmlNodePtr nodep)
{
    char *name = (char *) xmlGetProp(nodep, (const xmlChar *) ATT_NAME);
    char *match = (char *) xmlGetProp(nodep, (const xmlChar *) ATT_MATCH);
    char *priority = (char *) xmlGetProp(nodep,
					 (const xmlChar *) ATT_PRIORITY);
    char *mode = (char *) xmlGetProp(nodep, (const xmlChar *) ATT_MODE);
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

#if 0
    slaxWriteNewline(swp, 0);
#endif

    xmlFreeAndEasy(mode);
    xmlFreeAndEasy(priority);
    xmlFreeAndEasy(match);
    xmlFreeAndEasy(name);
}

static void
slaxWriteValueOf (slaxWriter_t *swp, xmlDocPtr docp UNUSED, xmlNodePtr nodep)
{
    char *sel = (char *) xmlGetProp(nodep, (const xmlChar *) ATT_SELECT);
    char *expr = slaxMakeExpression(swp, nodep, sel);

    slaxWrite(swp, "expr ");
    slaxWriteValue(swp, expr ?: UNKNOWN_EXPR);
    slaxWrite(swp, ";");
    slaxWriteNewline(swp, 0);
    xmlFreeAndEasy(expr);
    xmlFreeAndEasy(sel);
}

/**
 * isSimpleElement:
 *   determine if node contains just a single simple element, allowing
 *   us to avoid using braces for the value
 *
 * @node: node to test
 *
 * Returns TRUE if the node contains a single simple element
 */
static int
isSimpleElement (xmlNodePtr nodep)
{
    int hit = 0;

    for ( ; nodep; nodep = nodep->next) {
	if (nodep->type == XML_ELEMENT_NODE) {
	    if (nodep->ns && nodep->ns->href
		    && streq((const char *) nodep->ns->href, XSL_NS))
		return FALSE;

	    if (hit++)
		return FALSE;

	} else if (nodep->type == XML_TEXT_NODE) {
	    if (!isWhiteString(nodep->content))
		return FALSE;

	} else {
	    return FALSE;
	}
    }

    if (hit == 1)
	return TRUE;

    return FALSE;
}

static void
slaxWriteVariable (slaxWriter_t *swp, xmlDocPtr docp, xmlNodePtr nodep)
{
    char *name = (char *) xmlGetProp(nodep, (const xmlChar *) ATT_NAME);
    char *sel = (char *) xmlGetProp(nodep, (const xmlChar *) ATT_SELECT);
    const char *tag = (nodep->name[0] == 'v') ? "var" : "param";

    if (nodep->children) {
	xmlNodePtr childp = nodep->children;

	if (childp->next == NULL && childp->type == XML_TEXT_NODE) {
	    /*
	     * If there's only one child and it's text, we can emit
	     * a simple string initializer.
	     */
	    slaxWrite(swp, "%s $%s = \"", tag, name);
	    slaxWriteEscaped(swp, (char *) childp->content, SEF_DOUBLEQ);
	    slaxWrite(swp, "\";");
	    slaxWriteNewline(swp, 0);

	} else if (isSimpleElement(childp)) {
	    slaxWrite(swp, "%s $%s = ", tag, name);
	    slaxWriteChildren(swp, docp, nodep, FALSE);

	} else {
	    slaxWrite(swp, "%s $%s = {", tag, name);
	    slaxWriteNewline(swp, NEWL_INDENT);

	    slaxWriteChildren(swp, docp, nodep, FALSE);

	    slaxWrite(swp, "}");
	    slaxWriteNewline(swp, NEWL_OUTDENT);
	}

    } else if (sel) {
	char *expr = slaxMakeExpression(swp, nodep, sel);

	slaxWrite(swp, "%s $%s = ", tag, name);
	slaxWriteValue(swp, expr);
	slaxWrite(swp, ";");
	slaxWriteNewline(swp, 0);
	xmlFreeAndEasy(expr);

    } else {
	slaxWrite(swp, "%s $%s;", tag, name);
	slaxWriteNewline(swp, 0);
    }

    xmlFreeAndEasy(name);
    xmlFreeAndEasy(sel);
}

static void
slaxWriteApplyTemplates (slaxWriter_t *swp, xmlDocPtr docp, xmlNodePtr nodep)
{
    char *mode = (char *) xmlGetProp(nodep, (const xmlChar *) ATT_MODE);
    char *sel = (char *) xmlGetProp(nodep, (const xmlChar *) ATT_SELECT);
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

	    name = (char *) xmlGetProp(childp, (const xmlChar *) ATT_NAME);
	    sel = (char *) xmlGetProp(childp, (const xmlChar *) ATT_SELECT);

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

	} else if (childp->ns && childp->ns->prefix
		   && streq((const char *) childp->ns->prefix, XSL_PREFIX)) {
	    slaxWriteXslElement(swp, docp, childp, NULL);

	} else {
	    slaxWriteElement(swp, docp, childp);
	}
    }

    slaxWrite(swp, "}");
    slaxWriteNewline(swp, NEWL_OUTDENT);
}

static void
slaxWriteCallTemplate (slaxWriter_t *swp, xmlDocPtr docp, xmlNodePtr nodep)
{
    char *name = (char *) xmlGetProp(nodep, (const xmlChar *) ATT_NAME);
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

	name = (char *) xmlGetProp(childp, (const xmlChar *) ATT_NAME);
	sel = (char *) xmlGetProp(childp, (const xmlChar *) ATT_SELECT);

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

	sel = (char *) xmlGetProp(childp, (const xmlChar *) ATT_SELECT);
	if (sel) {
	    xmlFree(sel);
	    continue;
	}

	name = (char *) xmlGetProp(childp, (const xmlChar *) ATT_NAME);
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
slaxWriteIf (slaxWriter_t *swp, xmlDocPtr docp, xmlNodePtr nodep)
{
    char *test = (char *) xmlGetProp(nodep, (const xmlChar *) ATT_TEST);
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
slaxWriteForEach (slaxWriter_t *swp, xmlDocPtr docp, xmlNodePtr nodep)
{
    char *sel = (char *) xmlGetProp(nodep, (const xmlChar *) ATT_SELECT);
    char *expr = slaxMakeExpression(swp, nodep, sel);

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
slaxWriteCopyOf (slaxWriter_t *swp, xmlDocPtr docp UNUSED, xmlNodePtr nodep)
{
    char *sel = (char *) xmlGetProp(nodep, (const xmlChar *) ATT_SELECT);

    if (sel) {
	char *expr = slaxMakeExpression(swp, nodep, sel);
	slaxWrite(swp, "copy-of %s;", expr ?: UNKNOWN_EXPR);
	xmlFreeAndEasy(expr);
	xmlFree(sel);
	slaxWriteNewline(swp, 0);
    }
}

static void
slaxWriteChoose (slaxWriter_t *swp, xmlDocPtr docp, xmlNodePtr nodep)
{
    xmlNodePtr childp;
    int first = TRUE;

    for (childp = nodep->children; childp; childp = childp->next) {

	if (childp->type != XML_ELEMENT_NODE)
	    continue;

	if (streq((const char *) childp->name, ELT_WHEN)) {
	    char *test = (char *) xmlGetProp(childp,
					     (const xmlChar *) ATT_TEST);
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
slaxWriteStatementElements (slaxWriter_t *swp, xmlDocPtr docp UNUSED,
		    xmlNodePtr nodep, const char *name)
{
    char *elements = (char *) xmlGetProp(nodep,
					 (const xmlChar *) ATT_ELEMENTS);
    
    slaxWrite(swp, "%s %s;", name, elements);
    slaxWriteNewline(swp, 0);
    xmlFreeAndEasy(elements);
}

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
slaxWriteXslElement (slaxWriter_t *swp, xmlDocPtr docp,
		     xmlNodePtr nodep, int *statep)
{
    const char *name = (const char *) nodep->name;

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

    if (streq(name, "apply-imports")) {
	slaxWriteElementSimple(swp, docp, nodep);
	return;
    }

    if (streq(name, "apply-templates")) {
	slaxWriteApplyTemplates(swp, docp, nodep);
	return;
    }

    if (streq(name, "call-template")) {
	slaxWriteCallTemplate(swp, docp, nodep);
	return;
    }

    if (streq(name, "choose")) {
	slaxWriteChoose(swp, docp, nodep);
	return;
    }

    if (streq(name, "copy-of")) {
	slaxWriteCopyOf(swp, docp, nodep);
	return;
    }

    if (streq(name, "comment")) {
	slaxWriteCommentStatement(swp, docp, nodep);
	return;
    }

    if (streq(name, "for-each")) {
	slaxWriteForEach(swp, docp, nodep);
	return;
    }

    if (streq(name, "if")) {
	slaxWriteIf(swp, docp, nodep);
	return;
    }

    if (streq(name, "import")|| streq(name, "include")) {
	slaxWriteImport(swp, docp, nodep);
	return;
    }

    if (streq(name, "param")) {
	if (swp->sw_indent > 2) {
	    slaxWrite(swp,
		      "/* 'param' statement is inappropriately nested */");
	    slaxWriteNewline(swp, 0);
	}

	/*
	 * If indent equals 1, we are in a template. For templates,
	 * slaxWriteNamedTemplateParams() will have emitted the parameter
	 * list already, so we must not repeat it here.
	 */
	if (swp->sw_indent != 1)
	    slaxWriteVariable(swp, docp, nodep);
	return;
    }

    if (streq(name, "preserve-space")) {
	if (swp->sw_indent == 0)
	    slaxWriteStatementElements(swp, docp, nodep, name);
	return;
    }

    if (streq(name, "strip-space")) {
	if (swp->sw_indent == 0)
	    slaxWriteStatementElements(swp, docp, nodep, name);
	return;
    }

    if (streq(name, "template")) {
	slaxWriteTemplate(swp, docp, nodep);
	return;
    }

    /*
     * If disable-output-escaping is "yes", we don't want to write
     * the SLAX-friendly versions of xsl:text and xsl:value-of.
     */
    if (!isDisableOutputEscaping(nodep)) {
	if (streq(name, "text")) {
	    slaxWriteText(swp, docp, nodep);
	    return;
	}

	if (streq(name, "value-of")) {
	    slaxWriteValueOf(swp, docp, nodep);
	    return;
	}
    }

    if (streq(name, "variable")) {
	slaxWriteVariable(swp, docp, nodep);
	return;
    }

    slaxWriteElement(swp, docp, nodep);
}

static void
slaxWriteComment (slaxWriter_t *swp, xmlDocPtr docp UNUSED, xmlNodePtr nodep)
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
slaxWriteCommentStatement (slaxWriter_t *swp, xmlDocPtr docp, xmlNodePtr nodep)
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
 * @swp: output stream writer
 * @docp: current document
 * @nodep: current node
 * @initializer: are we called to emit the value of an initializer?
 */
static void
slaxWriteChildren (slaxWriter_t *swp, xmlDocPtr docp, xmlNodePtr nodep,
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
	    if (!isWhiteString(childp->content)) {
		slaxWriteExpr(swp, childp->content, initializer);
		state = STATE_PAST_DECLS;
	    }
	    break;

	case XML_ELEMENT_NODE:
	    if (childp->ns && childp->ns->prefix
		&& streq((const char *) childp->ns->prefix, XSL_PREFIX)) {
		slaxWriteXslElement(swp, docp, childp, &state);

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
slaxWriteCleanup (slaxWriter_t *swp)
{
    if (swp->sw_buf) {
	xmlFree(swp->sw_buf);
	swp->sw_buf = NULL;
    }
}

/**
 * slaxWriteDoc:
 * Write an XSLT document in SLAX format
 * @func: fprintf-like callback function to write data
 * @data: data passed to callback
 * @docp: source document (XSLT stylesheet)
 */
int
slaxWriteDoc (slaxWriterFunc_t func, void *data, xmlDocPtr docp)
{
    xmlNodePtr nodep;
    xmlNodePtr childp;
    slaxWriter_t sw;

    bzero(&sw, sizeof(sw));
    sw.sw_write = func;
    sw.sw_data = data;

    nodep = xmlDocGetRootElement(docp);
    if (nodep == NULL || nodep->name == NULL)
	return 1;

    slaxWrite(&sw, "/* Machine Crafted with Care (tm) by slaxWriter */");
    slaxWriteNewline(&sw, 0);

    slaxWrite(&sw, "version %s;", "1.0");
    slaxWriteNewline(&sw, 0);
    slaxWriteNewline(&sw, 0);

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
	    slaxDebug = TRUE;
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

    slaxWriteDoc((slaxWriterFunc_t) fprintf, stdout, docp);

    return 0;
}

#endif /* UNIT_TEST */
