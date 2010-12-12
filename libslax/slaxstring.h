/*
 * $Id$
 *
 * Copyright (c) 2010, Juniper Networks, Inc.
 * All rights reserved.
 *
 * slax_string_t string handling functions
 */

/*
 * We build strings as we go using this structure.
 */
struct slax_string_s {
    struct slax_string_s *ss_next; /* Linked list of strings in XPath expr */
    struct slax_string_s *ss_concat; /* Linked list of strings with "_" op */
    int ss_ttype;		   /* Token type */
    int ss_flags;		   /* Flags */
    char ss_token[1];		   /* Value of this token */
};

/* Flags for slaxString functions */
#define SSF_QUOTES	(1<<0)	/* Wrap the string in double quotes */
#define SSF_BRACES	(1<<1)	/* Escape braces (in attributes) */
#define SSF_SINGLEQ	(1<<2)	/* String has single quotes */
#define SSF_DOUBLEQ	(1<<3)	/* String has double quotes */

#define SSF_BOTHQS	(1<<4)	/* String has both single and double quotes */
#define SSF_CONCAT	(1<<5)	/* Turn BOTHQS string into xpath w/ concat */
#define SSF_SLAXNS	(1<<6)	/* Need the slax namespace */

/* SLAX UTF-8 character conversions */
#define SLAX_UTF_WIDTH4	4	/* '\u+xxxx' */
#define SLAX_UTF_WIDTH6	6	/* '\u-xxxxxx' */

/*
 * If the string is simple, we can optimize how we treat it
 */
static inline int
slaxStringIsSimple (slax_string_t *value, int ttype)
{
    return (value && value->ss_ttype == ttype
	    && value->ss_next == NULL && value->ss_concat == NULL);
}

/*
 * If the string is simple, we can optimize how we treat it
 */
static inline int
slaxStringIsSimple2 (slax_string_t *value, int ttype, int ttype2)
{
    return (value && (value->ss_ttype == ttype || value->ss_ttype == ttype2)
	    && value->ss_next == NULL && value->ss_concat == NULL);
}

/*
 * Fuse a variable number of strings together, returning the results.
 */
slax_string_t *
slaxStringFuse (slax_data_t *, int, slax_string_t **);

/*
 * Create a string.  Slax strings allow sections of strings (typically
 * tokens returned by the lexer) to be chained together to built
 * longer strings (typically an XPath expression).
 */
slax_string_t *
slaxStringCreate (slax_data_t *sdp, int token);

/*
 * Build a single string out of the string segments hung off "start".
 */
int
slaxStringCopy (char *buf, int bufsiz, slax_string_t *start, unsigned flags);

/*
 * Return a string for a literal string constant.
 */
slax_string_t *
slaxStringLiteral (const char *str, int);

/*
 * Link all strings above "start" (and below "top") into a single
 * string.
 */
slax_string_t *
slaxStringLink (slax_data_t *sdp, slax_string_t **, slax_string_t **);

/*
 * Free a set of string segments
 */
void
slaxStringFree (slax_string_t *ssp);

/*
 * Build a string from the string segment hung off "value" and return it
 */
char *
slaxStringAsChar (slax_string_t *value, unsigned flags);

/*
 * Return a set of xpath values as a concat() invocation
 */
char *
slaxStringAsConcat (slax_string_t *value, unsigned flags);

/*
 * Return a set of xpath values as an attribute value template
 */
char *
slaxStringAsValueTemplate (slax_string_t *value, unsigned flags);

/*
 * Calculate the length of the string consisting of the concatenation
 * of all the string segments hung off "start".
 */
int
slaxStringLength (slax_string_t *start, unsigned flags);

/**
 * Add a new slax string segment to the linked list
 * @tailp: pointer to a variable that points to the end of the linked list
 * @first: pointer to first string in linked list
 * @buf: the string to add
 * @bufsiz: length of the string to add
 * @ttype: token type
 */
int
slaxStringAddTail (slax_string_t ***tailp, slax_string_t *first,
		   const char *buf, size_t bufsiz, int ttype);
