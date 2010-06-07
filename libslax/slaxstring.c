/*
 * $Id: slaxstring.c,v 1.1 2006/11/01 21:27:20 phil Exp $
 *
 * Copyright (c) 2006-2010, Juniper Networks, Inc.
 * All rights reserved.
 * See ./Copyright for the status of this software
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
 * Create a string.  Slax strings allow sections of strings (typically
 * tokens returned by the lexer) to be chained together to built
 * longer strings (typically an XPath expression).
 */
slax_string_t *
slaxStringCreate (slax_data_t *sdp, int ttype)
{
    int len, i;
    char *cp;
    const char *start;
    slax_string_t *ssp;

    if (ttype == L_EOS)
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
	ssp->ss_next = NULL;

	if (ttype != T_QUOTED)
	    memcpy(ssp->ss_token, start, len);
	else {
	    cp = ssp->ss_token;

	    for (i = 0; i < len; i++) {
		unsigned char ch = start[i];

		if (ch == '\\') {
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
		    }
		}

		*cp++ = ch;
	    }
	    *cp = '\0';
	}
    }

    return ssp;
}

/*
 * Return a string for a literal string constant.
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
	ssp->ss_next = NULL;
    }

    return ssp;
}

/*
 * Link all strings above "start" (and below "top") into a single
 * string.
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

/*
 * Calculate the length of the string consisting of the concatenation
 * of all the string segments hung off "start".
 */
int
slaxStringLength (slax_string_t *start, unsigned flags)
{
    slax_string_t *ssp;
    int len = 0;
    char *cp;

    for (ssp = start; ssp != NULL; ssp = ssp->ss_next) {
	len += strlen(ssp->ss_token) + 1; /* One for space or NUL */
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
	}
    }

    return len;
}

/*
 * Build a single string out of the string segments hung off "start".
 */
int
slaxStringCopy (char *buf, int bufsiz, slax_string_t *start, unsigned flags)
{
    slax_string_t *ssp;
    int len = 0, slen;
    const char *str, *cp;
    char *bp = buf;
    int squote = 0; /* Single quote string flag */

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
	    int trim = FALSE;

	    if (bp[-2] == '_' || *str == '_')
		trim = FALSE;
	    else if (slaxNoSpace(bp[-2]) || slaxNoSpace(*str))
		trim = TRUE;	/* foo/goo[@zoo] */

	    else if (*str == ',')
		trim = TRUE;	/* foo(1, 2, 3) */

	    else if (isdigit(bp[-2]) && *str == '.')
		trim = TRUE;	/* 1.0 (looking at the '.') */

	    else if (bp[-2] == '.' && isdigit(*str))
		trim = TRUE;	/* 1.0 (looking at the '0') */

	    /*
	     * We only want to trim closers if the next thing is a closer
	     * or a slash, so that we handle "foo[goo]/zoo" correctly
	     */
	    if ((bp[-2] == ')' || bp[-2] == ']')
		    && !(*str == ')' || *str == ']' || *str == '/'))
		trim = FALSE;

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

	    if (strchr(str, '"')) {
		/* double quoted string to be surrounded by single quotes */
		*bp++ = '\'';
		squote = 1;
	    } else
		*bp++ = '"';

	    slen = strlen(str);
	    memcpy(bp, str, slen);
	    bp += slen;
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
		    *bp = *cp;
		}
	    }

	} else {
	    memcpy(bp, str, slen);
	    bp += slen;
	}

	*bp++ = ' ';
    }

    if (len > 0) {
	if (len < bufsiz)
	    buf[len - 1] = '\0';
	else buf[bufsiz - 1] = '\0';
    }

    return len;
}

/*
 * Concatenate a variable number of strings, returning the results.
 */
slax_string_t *
slaxStringConcat (slax_data_t *sdp UNUSED, int ttype, slax_string_t **sspp)
{
    slax_string_t *results, *start = *sspp;
    int len;

    if (start && start->ss_next == NULL && start->ss_ttype != T_QUOTED) {
	/*
	 * If we're only talking about one string and it doesn't
	 * need quoting, return it directly.  We need to clear
	 * out sspp to that the item isn't freed when the stack
	 * is cleared.
	 */
	*sspp = NULL;
	return start;
    }

    if (start && start->ss_next == NULL && start->ss_ttype == T_QUOTED)
	ttype = T_QUOTED;

    len = slaxStringLength(start, SSF_QUOTES);
    if (len == 0)
	return NULL;

    results = xmlMalloc(sizeof(*results) + len);
    if (results == NULL)
	return NULL;

    results->ss_ttype = ttype;
    results->ss_next = NULL;

    slaxStringCopy(results->ss_token, len, start, SSF_QUOTES);

    if (slaxDebug)
	slaxTrace("slaxConcat: '%s'", results->ss_token);
    return results;
}

/*
 * Build a string from the string segment hung off "value" and return it
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

    if (slaxDebug)
	slaxTrace("slaxStringAsChar: '%s'", buf);
    return buf;
}

/*
 * Return a set of xpath values as a concat() invocation
 */
char *
slaxStringAsValue (slax_string_t *value, unsigned flags UNUSED)
{
    static char s_prepend[] = "concat(";
    static char s_middle[] = ", ";
    static char s_append[] = ")";
    char *buf, *bp;
    int len = 0;
    slax_string_t *ssp;

    /*
     * First we work out the size of the buffer
     */
    for (ssp = value; ssp; ssp = ssp->ss_next) {
	if (len != 0)
	    len += sizeof(s_middle) - 1;
	len += strlen(ssp->ss_token);
    }

    len += sizeof(s_prepend) + sizeof(s_append) - 1;

    /* Allocate the buffer */
    buf = bp = xmlMalloc(len);
    if (buf == NULL) {
	slaxTrace("slaxStringAsValue:: out of memory");
	return NULL;
    }

    /* Fill it in: build the xpath concat invocation */
    memcpy(bp, s_prepend, sizeof(s_prepend) - 1);
    bp += sizeof(s_prepend) - 1;

    for (ssp = value; ssp; ssp = ssp->ss_next) {
	if (ssp != value) {
	    memcpy(bp, s_middle, sizeof(s_middle) - 1);
	    bp += sizeof(s_middle) - 1;
	}

	len = strlen(ssp->ss_token);
	memcpy(bp, ssp->ss_token, len);
	bp += len;
    }

    memcpy(bp, s_append, sizeof(s_append) - 1);
    bp += sizeof(s_append) - 1;

    *bp = 0;
    return buf;
}

/*
 * Free a set of string segments
 */
void
slaxStringFree (slax_string_t *ssp)
{
    slax_string_t *next;

    for ( ; ssp; ssp = next) {
	next = ssp->ss_next;
	ssp->ss_ttype = 0;
	ssp->ss_next = NULL;
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

    ssp->ss_next = NULL;
    ssp->ss_ttype = ttype;
    memcpy(ssp->ss_token, buf, bufsiz);
    ssp->ss_token[bufsiz] = '\0';

    **tailp = ssp;
    *tailp = &ssp->ss_next;

    return FALSE;
}

/*
 * Add a new slax string segment to the linked list
 * @tailp: pointer to a variable that points to the end of the linked list
 * @first: pointer to first string in linked list
 * @buf: the string to add
 * @bufsiz: length of the string to add
 * @ttype: token type
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
