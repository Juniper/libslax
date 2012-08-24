/*
 * $Id: slaxstring.c,v 1.1 2006/11/01 21:27:20 phil Exp $
 *
 * Copyright (c) 2006-2011, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * This module contains functions to build and handle a set of string
 * segments in a linked list.  It is used by both the slax reader and
 * writer code to simplify string construction.
 *
 * Each slax_string_t contains a segment of the string and a pointer
 * to the next slax_string_t.  Very simple, but makes building the
 * XSLT constructs during bison-based parsing fairly easy.
 */

#include "slaxinternals.h"
#include <libslax/slax.h>
#include "slaxparser.h"
#include <ctype.h>

/*
 * If the string has both types of quotes (single and double), then
 * we have some additional work on our future.  Return SSF_BOTHQS
 * to let the caller know.
 */
static int
slaxStringQFlags (slax_string_t *ssp)
{
    const char *cp;
    char quote = 0;		/* The quote we've already hit */
    int rc = 0;

    for (cp = ssp->ss_token; *cp; cp++) {
	if (*cp == '\'')
	    rc |= SSF_SINGLEQ;
	else if (*cp == '\"')
	    rc |= SSF_DOUBLEQ;
	else
	    continue;

	/* If we hit a different quote than we've seen, we've seen both */
	if (quote && *cp != quote)
	    return (SSF_SINGLEQ | SSF_DOUBLEQ | SSF_BOTHQS);

	quote = *cp;
    }

    return rc;
}

static int
slaxHex (unsigned char x)
{
    if ('0' <= x && x <= '9')
	return x - '0';
    if ('a' <= x && x <= 'f')
	return x - 'a' + 10;
    if ('A' <= x && x <= 'F')
	return x - 'A' + 10;
    return -1;
}    

#define SLAX_UTF_INVALID_NUMBER 0xfffd

/*
 * Turn a sequence of UTF-8 bytes into a word-wide integer.  Turns out
 * this is pretty easy, if you know how.  Otherwise, you're dead meat.
 */
static unsigned
slaxUtfWord (const char *cp, int width)
{
    unsigned val;
    int v1 = slaxHex(*cp++);
    int v2 = slaxHex(*cp++);
    int v3 = slaxHex(*cp++);
    int v4 = slaxHex(*cp++);

    if (v1 < 0 || v2 < 0 || v3 < 0 || v4 < 0)
	return SLAX_UTF_INVALID_NUMBER;

    val = (((((v1 << 4) | v2) << 4) | v3) << 4) | v4;

    if (width == SLAX_UTF_WIDTH6) {
	int v5 = slaxHex(*cp++);
	int v6 = slaxHex(*cp++);

	if (v5 < 0 || v6 < 0)
	    return SLAX_UTF_INVALID_NUMBER;

	val = (((val << 4) | v5) << 4) | v6;
    }

    return val;
}

/**
 * Create a string.  Slax strings allow sections of strings (typically
 * tokens returned by the lexer) to be chained together to built
 * longer strings (typically an XPath expression).
 *
 * The string is located in sdp->sd_buf[] between sdp->sd_start
 * and sdp->sd_cur.
 *
 * @param sdp main slax data structure
 * @param ttype token type for the string to be created
 * @return newly allocated string structure
 */
slax_string_t *
slaxStringCreate (slax_data_t *sdp, int ttype)
{
    int len, i, width;
    char *cp;
    const char *start;
    slax_string_t *ssp;

    if (ttype == L_EOS)		/* Don't bother for ";" */
	return NULL;

    len = sdp->sd_cur - sdp->sd_start;
    start = sdp->sd_buf + sdp->sd_start;

    if (ttype == T_QUOTED) {
	len -= 2;		/* Strip the quotes */
	start += 1;
    }

    ssp = xmlMalloc(sizeof(*ssp) + len + 1);
    if (ssp) {
	ssp->ss_token[len] = 0;
	ssp->ss_ttype = ttype;
	ssp->ss_next = ssp->ss_concat = NULL;

	if (ttype != T_QUOTED)
	    memcpy(ssp->ss_token, start, len);
	else {
	    cp = ssp->ss_token;

	    for (i = 0; i < len; i++) {
		unsigned char ch = start[i];

		if (ch == '\\' && !slaxParseIsXpath(sdp)) {
		    if (i == len - 1) {
			slaxError("%s:%d: trailing backslash in string",
				  sdp->sd_filename, sdp->sd_line);
			sdp->sd_errors += 1;
			ch = '\0';
			break;
		    }

		    ch = start[++i];

		    switch (ch) {
		    case 'n':
			ch = '\n';
			break;

		    case 'r':
			ch = '\r';
			break;

		    case 't':
			ch = '\t';
			break;

		    case 'x':
			{
			    int v1 = slaxHex(start[++i]);
			    int v2 = slaxHex(start[++i]);

			    if (v1 >= 0 && v2 >= 0) {
				ch = (v1 << 4) + v2;
				if (ch > 0x7f) {
				    /*
				     * If the value is >0x7f, then we have
				     * a UTF-8 character sequence to generate.
				     */
				    *cp++ = 0xc0 | (ch >> 6);
				    ch = 0x80 | (ch & 0x3f);
				}
			    }
			}
			break;

		    case 'u':
			ch = start[++i];
			if (ch == '+' && len - i >= SLAX_UTF_WIDTH4) {
			    width = SLAX_UTF_WIDTH4;

			} else if (ch == '-' && len - i >= SLAX_UTF_WIDTH6) {
			    width = SLAX_UTF_WIDTH6;

			} else {
			    ch = 'u';
			    i -= 1;
			    break;
			}

			unsigned word = slaxUtfWord(start + i + 1, width);
			i += width + 1;

			if (word <= 0x7f) {
			    ch = word;

			} else if (word <= 0x7ff) {
			    *cp++ = 0xc0 | ((word >> 6) & 0x1f);
			    ch = 0x80 | (word & 0x3f);

			} else if (word <= 0xffff) {
			    *cp++ = 0xe0 | (word >> 12);
			    *cp++ = 0x80 | ((word >> 6) & 0x3f);
			    ch = 0x80 | (word & 0x3f);

			} else {
			    *cp++ = 0xf0 | ((word >> 18) & 0x7);
			    *cp++ = 0x80 | ((word >> 12) & 0x3f);
			    *cp++ = 0x80 | ((word >> 6) & 0x3f);
			    ch = 0x80 | (word & 0x3f);
			}
			break;
		    }
		}

		*cp++ = ch;
	    }
	    *cp = '\0';
	}

	ssp->ss_flags = slaxStringQFlags(ssp);
    }

    return ssp;
}

/**
 * Return a string for a literal string constant.
 *
 * @param str literal string
 * @param ttype token type
 * @return newly allocated string structure
 */
slax_string_t *
slaxStringLiteral (const char *str, int ttype)
{
    slax_string_t *ssp;
    int len = strlen(str);

    ssp = xmlMalloc(sizeof(*ssp) + len);
    if (ssp) {
	memcpy(ssp->ss_token, str, len);
	ssp->ss_token[len] = 0;
	ssp->ss_ttype = ttype;
	ssp->ss_next = ssp->ss_concat = NULL;
	ssp->ss_flags = slaxStringQFlags(ssp);
    }

    return ssp;
}

/**
 * Link all strings above "start" (and below "top") into a single
 * string.
 *
 * @param sdp main slax data structure (UNUSED)
 * @param start stack location to start linking
 * @param top stack location to stop linking
 * @return linked list of strings (via ss_next)
 */
slax_string_t *
slaxStringLink (slax_data_t *sdp UNUSED, slax_string_t **start,
		slax_string_t **top)
{
    slax_string_t **sspp, *ssp, **next, *results = NULL;

    next = &results;

    for (sspp = start; sspp <= top; sspp++) {
	ssp = *sspp;
	if (ssp == NULL)
	    continue;

	*sspp = NULL;		/* Clear stack */

	*next = ssp;
	while (ssp->ss_next)	/* Skip to end of the list */
	    ssp = ssp->ss_next;
	next = &ssp->ss_next;
    }

    *next = NULL;
    return results;
}

/**
 * Calculate the length of the string consisting of the concatenation
 * of all the string segments hung off "start".
 *
 * @param start string (linked via ss_next) to determine length
 * @param flags indicate how string data is marshalled
 * @return number of bytes required to hold this string
 */
int
slaxStringLength (slax_string_t *start, unsigned flags)
{
    slax_string_t *ssp;
    int len = 0;
    char *cp;

    for (ssp = start; ssp != NULL; ssp = ssp->ss_next) {
	len += strlen(ssp->ss_token);

	if (ssp->ss_ttype != T_AXIS_NAME && ssp->ss_ttype != L_DCOLON)
	    len += 1;		/* One for space or NUL */

	if (ssp->ss_ttype == T_QUOTED) {
	    if (flags & SSF_QUOTES) {
		len += 2;
		for (cp = strchr(ssp->ss_token, '"');
			     cp; cp = strchr(cp + 1, '"'))
		    len += 1;	/* Needs a backslash */
	    }
	    if (flags & SSF_BRACES) {
		for (cp = ssp->ss_token; *cp; cp++)
		    if (*cp == '{' || *cp == '}')
			len += 1; /* Needs a double */
	    }
	    if (flags & SSF_ESCAPE) {
		for (cp = ssp->ss_token; *cp; cp++)
		    if (*cp == '\\')
			len += 1; /* Must be escaped */
	    }
	}
    }

    return len + 1;
}

/*
 * Should the character be stripped of surrounding whitespace?
 */
static int
slaxStringNoSpace (int ttype)
{
    if (ttype == L_AT || ttype == L_SLASH || ttype == L_DSLASH
	|| ttype == L_OPAREN  || ttype == L_CPAREN
	|| ttype == L_OBRACK  || ttype == L_CBRACK)
	return TRUE;
    return FALSE;
}


/**
 * Build a single string out of the string segments hung off "start".
 *
 * @param buf memory buffer to hold built string
 * @param bufsiz number of bytes available in buffer
 * @param start first link in linked list
 * @param flags indicate how string data is marshalled
 * @return number of bytes used to hold this string
 */
int
slaxStringCopy (char *buf, int bufsiz, slax_string_t *start, unsigned flags)
{
    slax_string_t *ssp;
    int len = 0, slen;
    const char *str, *cp;
    char *bp = buf;
    int squote = 0; /* Single quote string flag */
    int last_ttype = 0;

    for (ssp = start; ssp != NULL; ssp = ssp->ss_next) {
	str = ssp->ss_token;
	slen = strlen(str);
	len += slen + 1;	/* One for space or NUL */

	if ((flags & SSF_QUOTES) && ssp->ss_ttype == T_QUOTED)
	    len += 2;		/* Two quotes */

	if (len > bufsiz)
	    break;		/* Tragedy: not enough room */

	if (bp > &buf[1] && bp[-1] == ' ') {
	    /*
	     * Decide if we can trim off the last trailing space.  You
	     * would think this was an easy one, but it's not.
	     */
	    int trim = FALSE, comma = FALSE;

	    if (last_ttype == L_UNDERSCORE || ssp->ss_ttype == L_UNDERSCORE)
		trim = FALSE;

	    else if (ssp->ss_ttype == L_COMMA) {
		trim = TRUE;	/* foo(1, 2, 3) */
		comma = TRUE;

	    } else if ((last_ttype == L_PLUS || last_ttype == L_MINUS
			|| last_ttype == L_STAR || last_ttype == L_UNDERSCORE
			|| last_ttype == L_QUESTION || last_ttype == L_COLON)
		     && ssp->ss_ttype == L_OPAREN)
		trim = FALSE;	/*  */

	    else if (last_ttype == L_QUESTION && ssp->ss_ttype == L_COLON)
		trim = TRUE;
	    
	    else if (slaxStringNoSpace(last_ttype)
		       || slaxStringNoSpace(ssp->ss_ttype)) {
		if (bp[-2] != ',')
		    trim = TRUE;	/* foo/goo[@zoo] */

	    } else if (last_ttype == T_NUMBER && ssp->ss_ttype == L_DOT)
		trim = TRUE;	/* 1.0 (looking at the '.') */

	    else if (last_ttype == L_DOT && ssp->ss_ttype == T_NUMBER)
		trim = TRUE;	/* 1.0 (looking at the '0') */

	    else if (bp == &buf[2] && buf[0] == '-')
		trim = TRUE;	/* Negative number */

	    /*
	     * We only want to trim closers if the next thing is a closer
	     * or a slash, so that we handle "foo[goo]/zoo" correctly.
	     */
	    if ((last_ttype == L_CPAREN || last_ttype == L_CBRACK)
		    && !(ssp->ss_ttype == L_CPAREN
			 || ssp->ss_ttype == L_CBRACK
			 || ssp->ss_ttype == L_SLASH
			 || ssp->ss_ttype == L_DSLASH))
		trim = comma;

	    if (trim) {
		bp -= 1;
		len -= 1;
	    }
	}

	if ((flags & SSF_QUOTES) && ssp->ss_ttype == T_QUOTED) {
	    /*
	     * If it's a quoted string, we surround it by quotes, but
	     * we also have to handle embedded quotes.
	     */
	    const char *dqp = strchr(str, '"');
	    const char *sqp = strchr(str, '\'');

	    if (dqp && sqp) {
		/* Bad news */
		slaxLog("warning: both quotes used in string: %s", str);
		*bp++ = '"';
	    } else if (dqp) {
		/* double quoted string to be surrounded by single quotes */
		*bp++ = '\'';
		squote = 1;
	    } else
		*bp++ = '"';

	    slen = strlen(str);
	    if (flags & SSF_ESCAPE) {
		int i;
		for (i = 0; i < slen; i++) {
		    *bp++ = str[i];
		    if (str[i] == '\\') {
			if (++len > bufsiz)
			    break;
			*bp++ = '\\';
		    }
		}
	    } else {
		memcpy(bp, str, slen);
		bp += slen;
	    }

	    if (squote) {
		/* double quoted string to be surrounded by single quotes */
		*bp++ = '\'';
		squote = 0;
	    } else
		*bp++ = '"';

	} else if ((flags & SSF_BRACES) && ssp->ss_ttype == T_QUOTED) {
	    for (cp = str; *cp; cp++) {
		*bp++ = *cp;
		if (*cp == '{' || *cp == '}') {
		    /*
		     * XSLT uses double open and close braces to escape
		     * them inside attributes to allow for attribute
		     * value templates (AVTs).  SLAX does AVTs with
		     * normal expression syntax, so if we see a brace,
		     * we want it to stay a brace.  We make this happen
		     * by doubling the character.
		     */
		    *bp++ = *cp;
		    len += 1;
		}
	    }

	} else {
	    memcpy(bp, str, slen);
	    bp += slen;
	}

	/* We want axis::elt with no spaces */
	if (ssp->ss_ttype == T_AXIS_NAME || ssp->ss_ttype == L_DCOLON)
	    len -= 1;
	else *bp++ = ' ';

	last_ttype = ssp->ss_ttype;
    }

    if (len <= 0)
	return 0;

    if (len < bufsiz) {
	buf[--len] = '\0';
	return len;
    } else {
	buf[--bufsiz] = '\0';
	return bufsiz;
    }
}

/**
 * Build a string from the string segment hung off "value" and return it
 *
 * @param value start of linked list of string segments
 * @param flags indicate how string data is marshalled
 * @return newly allocated character string
 */
char *
slaxStringAsChar (slax_string_t *value, unsigned flags)
{
    int len;
    char *buf;

    len = slaxStringLength(value, flags);
    if (len == 0)
	return NULL;

    buf = xmlMalloc(len);
    if (buf == NULL)
	return NULL;

    slaxStringCopy(buf, len, value, flags);

    if (slaxLogIsEnabled)
	slaxLog("slaxStringAsChar: '%s'", buf);
    return buf;
}

static slax_string_t *
slaxStringSkipToMarker (slax_string_t *start, slax_string_t *marker)
{
    slax_string_t *ssp = NULL;

    for (ssp = start; ssp && ssp->ss_next != marker; ssp = ssp->ss_next)
	    continue;

    return ssp;
}

static int
slaxStringValueTemplateLength (slax_string_t *value, unsigned flags)
{
    int len = 0;
    unsigned xflags;

    if (value->ss_ttype == M_CONCAT) {
	slax_string_t *ssp, *marker, *last;

	ssp = value->ss_next->ss_next; /* Skip 'concat' and '(' */
	marker = value->ss_concat;       /* ',' */
	last = slaxStringSkipToMarker(ssp, marker);

	while (ssp && last) {
	    last->ss_next = NULL; /* Fake terminate the string */

	    xflags = flags;
	    if (ssp->ss_ttype != T_QUOTED) {
		len += 2; /* Room for braces */
		xflags |= SSF_QUOTES;
	    }

	    len += slaxStringLength(ssp, xflags);

	    last->ss_next = marker; /* Re-terminate the string */

	    if (marker->ss_ttype == L_CPAREN)
		break;

	    ssp = marker->ss_next;
	    marker = marker->ss_concat;
	    last = slaxStringSkipToMarker(ssp, marker);
	}

    } else {
	xflags = flags;
	if (value->ss_ttype != T_QUOTED) {
	    len += 2; /* Room for braces */
	    xflags |= SSF_QUOTES;
	}

	len += slaxStringLength(value, xflags);

    }
    return len;
}

static void
slaxStringValueTemplateCopy (char *buf, int len,
			     slax_string_t *value, unsigned flags)
{
    unsigned xflags;
    int blen = len;
    char *bp = buf;

    if (value->ss_ttype == M_CONCAT) {
	slax_string_t *ssp, *marker, *last;

	ssp = value->ss_next->ss_next; /* Skip 'concat' and '(' */
	marker = value->ss_concat;       /* ',' */
	last = slaxStringSkipToMarker(ssp, marker);

	while (ssp && last) {
	    last->ss_next = NULL; /* Fake terminate the string */

	    xflags = flags;
	    if (ssp->ss_ttype != T_QUOTED) {
		*bp++ = '{';
		blen -= 1;
		xflags |= SSF_QUOTES;
	    }

	    len = slaxStringCopy(bp, blen, ssp, xflags);
	    blen -= len;
	    bp += len;


	    if (ssp->ss_ttype != T_QUOTED) {
		*bp++ = '}';
		blen -= 1;
	    }

	    last->ss_next = marker; /* Re-terminate the string */

	    if (marker->ss_ttype == L_CPAREN)
		break;

	    ssp = marker->ss_next;
	    marker = marker->ss_concat;
	    last = slaxStringSkipToMarker(ssp, marker);
	}

    } else {
	xflags = flags;
	if (value->ss_ttype != T_QUOTED) {
	    *bp++ = '{';
	    blen -= 1;
	    xflags |= SSF_QUOTES;
	}

	len = slaxStringCopy(bp, blen, value, xflags);
	blen -= len;
	bp += len;

	if (value->ss_ttype != T_QUOTED) {
	    *bp++ = '}';
	    blen -= 1;
	}
    }

    *bp = '\0';
}

/**
 * Return a set of xpath values as an attribute value template.  The SLAX
 * concatenation operations used the "{a1}{b2}" braces templating scheme
 * for concatenation.
 * For example: <a b = one _ "-" _ two>;  would be <a b="{one}-{two}"/>.
 * Attribute value templates are used for non-xsl elements.
 *
 * @param value start of linked list of string segments
 * @param flags indicate how string data is marshalled
 * @return newly allocated character string
 */
char *
slaxStringAsValueTemplate (slax_string_t *value, unsigned flags)
{
    char *buf;
    int len = 0;

    flags |= SSF_BRACES;	/* Just in case the caller forgot */

    /*
     * If there's no concatenation, just return the XPath expression
     */
    if (slaxStringIsSimple2(value, T_QUOTED, T_NUMBER))
	return slaxStringAsChar(value, flags);

    /*
     * First we work out the size of the buffer
     */
    len = slaxStringValueTemplateLength(value, flags);

    /* Allocate the buffer */
    buf = xmlMalloc(len + 1);
    if (buf == NULL) {
	slaxLog("slaxStringAsValueTemplate:: out of memory");
	return NULL;
    }

    /* Fill it in: build the attribute value template */
    slaxStringValueTemplateCopy(buf, len, value, flags);

    return buf;
}

/*
 * Fuse a variable number of quoted strings together, returning the results.
 */
slax_string_t *
slaxStringFuse (slax_string_t *start)
{
    slax_string_t *ssp, *results;
    int len = 0;
    char *cp;

    for (ssp = start; ssp != NULL; ssp = ssp->ss_next)
	len += strlen(ssp->ss_token);

    if (len == 0)
	return NULL;

    results = xmlMalloc(sizeof(*results) + len + 1);
    if (results == NULL)
	return NULL;

    cp = results->ss_token;
    for (ssp = start; ssp != NULL; ssp = ssp->ss_next) {
	len = strlen(ssp->ss_token);
	memcpy(cp, ssp->ss_token, len);
	cp += len;
    }

    *cp = '\0';			/* NUL terminate the string */

    results->ss_ttype = start->ss_ttype;
    results->ss_next = NULL;
    results->ss_flags = slaxStringQFlags(results);

    if (slaxLogIsEnabled)
	slaxLog("slaxStringFuse: '%s'", results->ss_token);
    return results;
}

/*
 * Rewrite an invocation of the underscore operator to
 * be a call to concat().
 * Two cases:
 * (a) first time called (for this expression)
 *     - build new M_CONCAT node
 *     - build new L_OPAREN node
 *     - convert op to comma
 *     - add close paren
 * (b) not first time (for this expression)
 *     - discard op to close paren
 *     - convert last token of "left" to be a comma
 *     - add close paren
 */
slax_string_t *
slaxConcatRewrite (slax_data_t *sdp UNUSED, slax_string_t *left,
		   slax_string_t *op, slax_string_t *right)
{
    slax_string_t *ssp = NULL, *commap;

    slaxLog("slaxConcatRewrite: %p[%s], %p[%s], %p[%s]",
	    left, left ? left->ss_token : "",
	    op, op ? op->ss_token : "",
	    right, right ? right->ss_token : "");

    if (left == NULL || op == NULL || right == NULL)
	return left;

    if (left->ss_ttype == T_QUOTED && right->ss_ttype == T_QUOTED
	&& left->ss_next == NULL && right->ss_next == NULL
	&& ((left->ss_flags & SSF_QUOTE_MASK)
	    == (right->ss_flags & SSF_QUOTE_MASK))) {
	/*
	 * Simple parse-time optimization: Both left and right are
	 * simple quoted strings so we can combine them into a single
	 * string.
	 */
	left->ss_next = right;
	ssp = slaxStringFuse(left);
	if (ssp) {
	    slaxStringFree(left);
	    slaxStringFree(op);
	    /* Don't free right, because it was linked into left */
	    return ssp;
	}
    }

    /* Turn the op (L_UNDERSCORE) into a close paren (L_CPAREN) */
    op->ss_token[0] = ')';
    op->ss_ttype = L_CPAREN;

    if (left->ss_ttype != M_CONCAT) {
	slax_string_t *conp = slaxStringLiteral("concat", M_CONCAT);
	slax_string_t *openp = slaxStringLiteral("(", L_OPAREN);
	commap = slaxStringLiteral(",", L_COMMA);

	/*
	 * Create the concatenation sequence:
	 *     "concat" -> "(" -> left... -> "," -> right... -> ")"
	 */
	if (conp && openp && commap) {
	    conp->ss_next = openp;
	    openp->ss_next = left;

	    /* Find end of left list */
	    for (ssp = left; ssp->ss_next; ssp = ssp->ss_next)
		continue;
	    ssp->ss_next = commap;
	    conp->ss_concat = commap; /* Point to next marker */

	    commap->ss_next = right;

	    /* Find end of right list */
	    for (ssp = right; ssp->ss_next; ssp = ssp->ss_next)
		continue;
	    ssp->ss_next = op;
	    commap->ss_concat = op; /* Point to next marker */

	    return conp;
	}

	slaxLog("slaxConcatRewrite: failed (cond 1): %p/%p/%p",
		conp, openp, commap);

	if (conp)
	    slaxStringFree(conp);
	if (openp)
	    slaxStringFree(openp);
	if (commap)
	    slaxStringFree(commap);

    } else {
	/*
	 * Modify an existing concatenation sequence
	 *     left... -> "," -> right... -> ")"
	 * We know that the existing sequence (left) has a close
	 * paren at the end, so we turn that into a comma for
	 * the middle and add "op" as the new cparen.
	 */
	for (commap = left; commap->ss_next; commap = commap->ss_next)
	    continue;
	if (commap->ss_ttype == L_CPAREN) {

	    commap->ss_ttype = L_COMMA;
	    commap->ss_token[0] = ',';
	    commap->ss_next = right;

	    /* Find end of right string */
	    for (ssp = right; ssp->ss_next; ssp = ssp->ss_next)
		continue;
	    ssp->ss_next = op;

	    commap->ss_concat = op; /* Point to next marker */

	    return left;
		
	}

	slaxLog("slaxConcatRewrite: failed (cond 2): %p/%d/%s",
		ssp, ssp ? ssp->ss_ttype : 0, ssp ? ssp->ss_token : 0);
    }

    slaxStringFree(op);
    slaxStringFree(right);
    return left;
}

/*
 * We need to rewrite the ternary operator ("?:") into a form suitable
 * for the current usage.  This isn't easy, but puts the complexity
 * into the compiler instead of exposing it to the user.  A couple of
 * rewrite patterns are required.  The primary pattern uses
 * <xsl:choose>.
 */
slax_string_t *
slaxTernaryRewrite (slax_data_t *sdp UNUSED, slax_string_t *scond,
		    slax_string_t *qsp, slax_string_t *strue,
		    slax_string_t *csp, slax_string_t *sfalse)
{
    static unsigned varnumber;
    static char varfmt[] = SLAX_TERNARY_VAR_FORMAT;
    char varname[sizeof(varfmt) + SLAX_TERNARY_VAR_FORMAT_WIDTH];

    snprintf(varname, sizeof(varname), varfmt, ++varnumber);

    slaxLog("slaxTernaryRewrite: %s/%s/%s", scond ? scond->ss_token : "",
	    strue ? strue->ss_token : "", sfalse ? sfalse->ss_token : "");

    slax_string_t *tsp = slaxStringLiteral(varname, M_TERNARY);
    slax_string_t *esp = slaxStringLiteral(varname, M_TERNARY_END);
    slax_string_t *nsp;

    if (tsp == NULL || qsp == NULL || csp == NULL || esp == NULL)
	return NULL;

    /* Our use of "slax:value()" requires the SLAX namespace */
    tsp->ss_flags |= SSF_SLAXNS;

    /*
     * Make "concat" links:
     *    M_TERNARY -> L_QUESTION -> L_COLON -> M_TERNARY_END
     */
    tsp->ss_next = scond;
    tsp->ss_concat = qsp;
    qsp->ss_next = strue ?: csp;
    qsp->ss_concat = csp;
    csp->ss_next = sfalse;
    csp->ss_concat = esp;

    /* Link the 'question mark' string after the 'condition' string */
    for (nsp = scond; nsp->ss_next; nsp = nsp->ss_next)
	continue;
    nsp->ss_next = qsp;

    if (strue) {
	/* Link the 'colon' string after the 'question mark' string */
	for (nsp = strue; nsp->ss_next; nsp = nsp->ss_next)
	    continue;
	nsp->ss_next = csp;
    }

    for (nsp = sfalse; nsp->ss_next; nsp = nsp->ss_next)
	continue;
    nsp->ss_next = esp;

    return tsp;
}

/**
 * Free a set of string segments.  The ss_next link is
 * followed and all contents are freed.
 * @param ssp string links to be freed.
 */
void
slaxStringFree (slax_string_t *ssp)
{
    slax_string_t *next;

    for ( ; ssp; ssp = next) {
	next = ssp->ss_next;
	ssp->ss_next = NULL;

	ssp->ss_ttype = 0;
	ssp->ss_flags = 0;
	xmlFree(ssp);
    }
}

static int
slaxStringAddTailHelper (slax_string_t ***tailp,
			 const char *buf, size_t bufsiz, int ttype)
{
    slax_string_t *ssp;

    /*
     * Allocate and fill in a slax_string_t; we don't need a +1
     * for bufsiz since the ss_token already has it.
     */
    ssp = xmlMalloc(sizeof(*ssp) + bufsiz);
    if (ssp == NULL)
	return TRUE;

    memcpy(ssp->ss_token, buf, bufsiz);
    ssp->ss_token[bufsiz] = '\0';
    ssp->ss_next = NULL;
    ssp->ss_ttype = ttype;
    ssp->ss_flags = slaxStringQFlags(ssp);

    **tailp = ssp;
    *tailp = &ssp->ss_next;

    return FALSE;
}

/**
 * Add a new slax string segment to the linked list
 * @param tailp pointer to a variable that points to the end of the linked list
 * @param first pointer to first string in linked list
 * @param buf the string to add
 * @param bufsiz length of the string to add
 * @param ttype token type
 * @return TRUE if xmlMalloc fails, FALSE otherwise
 */
int
slaxStringAddTail (slax_string_t ***tailp, slax_string_t *first,
		   const char *buf, size_t bufsiz, int ttype)
{
    if (tailp == NULL || buf == NULL)
	return FALSE;		/* Benign no-op */

    /*
     * SLAX uses "_" as the concatenation operator, so if we're not
     * the first chunk of string, we need to insert an underscore.
     */
    if (first != NULL) {
	if (slaxStringAddTailHelper(tailp, "_", 1, L_UNDERSCORE))
	    return TRUE;
    }

    return slaxStringAddTailHelper(tailp, buf, bufsiz, ttype);
}

/**
 * Detect if the string needs the "slax" namespace
 * @value: string to test
 * @returns TRUE if the strings needs the slax namespace
 */
int
slaxNeedsSlaxNs (slax_string_t *value)
{
    for ( ; value; value = value->ss_next)
	if (value->ss_flags & SSF_SLAXNS)
	    return TRUE;

    return FALSE;
}
