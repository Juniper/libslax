/*
 * $Id: slaxloader.c,v 1.6 2008/05/21 02:06:12 phil Exp $
 *
 * Copyright (c) 2006-2010, Juniper Networks, Inc.
 * All rights reserved.
 * See ../Copyright for the status of this software
 *
 * slaxlexer.c -- lex slax input into lexical tokens
 *
 * This file contains the lexer for slax.
 */

#include "slaxinternals.h"
#include <libslax/slax.h>
#include "slaxparser.h"
#include <ctype.h>
#include <errno.h>

#include <libxslt/extensions.h>
#include <libexslt/exslt.h>

#define SD_BUF_FUDGE (BUFSIZ/8)
#define SD_BUF_INCR BUFSIZ

#define SLAX_MAX_CHAR	128	/* Size of our character tables */

static int slaxSetup;		/* Have we initialized? */

/*
 * These are lookup tables for one and two character literal tokens.
 */
static short singleWide[SLAX_MAX_CHAR];
static char doubleWide[SLAX_MAX_CHAR];
static char tripleWide[SLAX_MAX_CHAR];

/*
 * Define all one character literal tokens, mapping the single
 * character to the slaxparser.y token
 */
static int singleWideData[] = {
    L_AT, '@',
    L_CBRACE, '}',
    L_CBRACK, ']',
    L_COMMA, ',',
    L_CPAREN, ')',
    L_DOT, '.',
    L_EOS, ';',
    L_EQUALS, '=',
    L_GRTR, '>',
    L_LESS, '<',
    L_MINUS, '-',
    L_OBRACE, '{',
    L_OBRACK, '[',
    L_OPAREN, '(',
    L_PLUS, '+',
    L_SLASH, '/',
    L_STAR, '*',
    L_UNDERSCORE, '_',
    L_VBAR, '|',
    0
};

/*
 * Define all two character literal tokens, mapping two contiguous
 * characters into a single slaxparser.y token.
 * N.B.:  These are not "double-wide" as in multi-byte characters,
 * but are two distinct characters.
 * See also: slaxDoubleWide(), where this list is unfortunately duplicated.
 */
static int doubleWideData[] = {
    L_ASSIGN, ':', '=',
    L_DAMPER, '&', '&',
    L_DCOLON, ':', ':',
    L_DEQUALS, '=', '=',
    L_DOTDOT, '.', '.',
    L_DSLASH, '/', '/',
    L_DVBAR, '|', '|',
    L_GRTREQ, '>', '=',
    L_LESSEQ, '<', '=',
    L_NOTEQUALS, '!', '=',
    L_PLUSEQUALS, '+', '=',
    0
};

#if 0
/*
 * Define all three character literal tokens, mapping three contiguous
 * characters into a single slaxparser.y token.
 */
static int tripleWideData[] = {
    L_DOTDOTDOT, '.', '.', '.',
    0
};
#endif

/*
 * Define all keyword tokens, mapping the keywords into the slaxparser.y
 * token.  We also provide KMF_* flags to indicate what context the keyword
 * is reserved.  SLAX keywords are not reserved inside XPath expressions
 * and XPath keywords are not reserved in non-XPath contexts.
 */
typedef struct keyword_mapping_s {
    int km_ttype;		/* Token type (returned from yylex) */
    const char *km_string;	/* Token string */
    int km_flags;		/* Flags for this entry */
} keyword_mapping_t;

/* Flags for km_flags: */
#define KMF_NODE_TEST	(1<<0)	/* Node test */
#define KMF_SLAX_KW	(1<<1)	/* Keyword for slax */
#define KMF_XPATH_KW	(1<<2)	/* Keyword for xpath */

static keyword_mapping_t keywordMap[] = {
    { K_AND, "and", KMF_XPATH_KW },
    { K_APPEND, "append", KMF_SLAX_KW },
    { K_APPLY_IMPORTS, "apply-imports", KMF_SLAX_KW },
    { K_APPLY_TEMPLATES, "apply-templates", KMF_SLAX_KW },
    { K_ATTRIBUTE, "attribute", KMF_SLAX_KW },
    { K_ATTRIBUTE_SET, "attribute-set", KMF_SLAX_KW },
    { K_CALL, "call", KMF_SLAX_KW },
    { K_CASE_ORDER, "case-order", KMF_SLAX_KW },
    { K_CDATA_SECTION_ELEMENTS, "cdata-section-elements", KMF_SLAX_KW },
    { K_COMMENT, "comment", KMF_SLAX_KW | KMF_NODE_TEST },
    { K_COPY_NODE, "copy-node", KMF_SLAX_KW },
    { K_COPY_OF, "copy-of", KMF_SLAX_KW },
    { K_COUNT, "count", KMF_SLAX_KW },
    { K_DATA_TYPE, "data-type", KMF_SLAX_KW },
    { K_DECIMAL_FORMAT, "decimal-format", KMF_SLAX_KW },
    { K_DECIMAL_SEPARATOR, "decimal-separator", KMF_SLAX_KW },
    { K_DIE, "die", KMF_SLAX_KW },
    { K_DIGIT, "digit", KMF_SLAX_KW },
    { K_DIV, "div", KMF_XPATH_KW },
    { K_DOCTYPE_PUBLIC, "doctype-public", KMF_SLAX_KW },
    { K_DOCTYPE_SYSTEM, "doctype-system", KMF_SLAX_KW },
    { K_ELEMENT, "element", KMF_SLAX_KW },
    { K_ELSE, "else", KMF_SLAX_KW },
    { K_ELSE, "else", KMF_SLAX_KW },
    { K_ENCODING, "encoding", KMF_SLAX_KW },
    { K_EXCLUDE, "exclude", KMF_SLAX_KW },
    { K_EXPR, "expr", KMF_SLAX_KW },
    { K_EXTENSION, "extension", KMF_SLAX_KW },
    { K_FALLBACK, "fallback", KMF_SLAX_KW },
    { K_FORMAT, "format", KMF_SLAX_KW },
    { K_FOR, "for", KMF_SLAX_KW },
    { K_FOR_EACH, "for-each", KMF_SLAX_KW },
    { K_FROM, "from", KMF_SLAX_KW },
    { K_FUNCTION, "function", KMF_SLAX_KW },
    { K_GROUPING_SEPARATOR, "grouping-separator", KMF_SLAX_KW },
    { K_GROUPING_SIZE, "grouping-size", KMF_SLAX_KW },
    { K_ID, "id", KMF_NODE_TEST }, /* Not really, but... */
    { K_IF, "if", KMF_SLAX_KW },
    { K_IMPORT, "import", KMF_SLAX_KW },
    { K_INCLUDE, "include", KMF_SLAX_KW },
    { K_INDENT, "indent", KMF_SLAX_KW },
    { K_INFINITY, "infinity", KMF_SLAX_KW },
    { K_KEY, "key", KMF_SLAX_KW | KMF_NODE_TEST }, /* Not really, but... */
    { K_LANGUAGE, "language", KMF_SLAX_KW },
    { K_LETTER_VALUE, "letter-value", KMF_SLAX_KW },
    { K_LEVEL, "level", KMF_SLAX_KW },
    { K_MATCH, "match", KMF_SLAX_KW },
    { K_MEDIA_TYPE, "media-type", KMF_SLAX_KW },
    { K_MESSAGE, "message", KMF_SLAX_KW },
    { K_MINUS_SIGN, "minus-sign", KMF_SLAX_KW },
    { K_MOD, "mod", KMF_XPATH_KW },
    { K_MODE, "mode", KMF_SLAX_KW },
    { K_MVAR, "mvar", KMF_SLAX_KW },
    { K_NAN, "nan", KMF_SLAX_KW },
    { K_NODE, "node", KMF_NODE_TEST },
    { K_NS, "ns", KMF_SLAX_KW },
    { K_NS_ALIAS, "ns-alias", KMF_SLAX_KW },
    { K_NS_TEMPLATE, "ns-template", KMF_SLAX_KW },
    { K_NUMBER, "number", KMF_SLAX_KW },
    { K_OMIT_XML_DECLARATION, "omit-xml-declaration", KMF_SLAX_KW },
    { K_OR, "or", KMF_XPATH_KW },
    { K_ORDER, "order", KMF_SLAX_KW },
    { K_OUTPUT_METHOD, "output-method", KMF_SLAX_KW },
    { K_PARAM, "param", KMF_SLAX_KW },
    { K_PATTERN_SEPARATOR, "pattern-separator", KMF_SLAX_KW },
    { K_PERCENT, "percent", KMF_SLAX_KW },
    { K_PER_MILLE, "per-mille", KMF_SLAX_KW },
    { K_PRESERVE_SPACE, "preserve-space", KMF_SLAX_KW },
    { K_PRIORITY, "priority", KMF_SLAX_KW },
    { K_PROCESSING_INSTRUCTION, "processing-instruction",
      KMF_SLAX_KW | KMF_NODE_TEST }, /* Not a node test, but close enough */
    { K_RESULT, "result", KMF_SLAX_KW },
    { K_SET, "set", KMF_SLAX_KW },
    { K_SORT, "sort", KMF_SLAX_KW },
    { K_STANDALONE, "standalone", KMF_SLAX_KW },
    { K_STRIP_SPACE, "strip-space", KMF_SLAX_KW },
    { K_TEMPLATE, "template", KMF_SLAX_KW },
    { K_TERMINATE, "terminate", KMF_SLAX_KW },
    { K_TEXT, "text", KMF_NODE_TEST },
    { K_TRACE, "trace", KMF_SLAX_KW },
    { K_UEXPR, "uexpr", KMF_SLAX_KW },
    { K_USE_ATTRIBUTE_SETS, "use-attribute-sets", KMF_SLAX_KW },
    { K_VALUE, "value", KMF_SLAX_KW },
    { K_VAR, "var", KMF_SLAX_KW },
    { K_VERSION, "version", KMF_SLAX_KW },
    { K_WHILE, "while", KMF_SLAX_KW },
    { K_WITH, "with", KMF_SLAX_KW },
    { K_ZERO_DIGIT, "zero-digit", KMF_SLAX_KW },
    { 0, NULL, 0 }
};

/*
 * Set up the lexer's lookup tables
 */
static void
slaxSetupLexer (void)
{
    int i;

    slaxSetup = 1;

    for (i = 0; singleWideData[i]; i += 2)
	singleWide[singleWideData[i + 1]] = singleWideData[i];

    for (i = 0; doubleWideData[i]; i += 3)
	doubleWide[doubleWideData[i + 1]] = i + 2;

    /* There's only one triple wide, so optimize (for now) */
    tripleWide['.'] = 1;

    for (i = 0; keywordMap[i].km_ttype; i++)
	keywordString[slaxTokenTranslate(keywordMap[i].km_ttype)]
	    = keywordMap[i].km_string;
}

/*
 * Is the given character valid inside bare word tokens (T_BARE)?
 */
static inline int
slaxIsBareChar (int ch)
{
    return (isalnum(ch) || (ch == ':') || (ch == '_') || (ch == '.')
	    || (ch == '-') || (ch & 0x80));
}

/*
 * Is the given character valid inside variable names (T_VAR)?
 */
static inline int
slaxIsVarChar (int ch)
{
    return (isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == ':');
}

/*
 * Does the given character end a token?
 */
static inline int
slaxIsFinalChar (int ch)
{
    return (ch == ';' || isspace(ch));
}

/*
 * Does the input buffer start with the given keyword?
 */
static int
slaxKeywordMatch (slax_data_t *sdp, const char *str)
{
    int len = strlen(str);
    int ch;

    if (sdp->sd_len - sdp->sd_start < len)
	return FALSE;

    if (memcmp(sdp->sd_buf + sdp->sd_start, str, len) != 0)
	return FALSE;

    ch = sdp->sd_buf[sdp->sd_start + len];

    if (slaxIsBareChar(ch))
	return FALSE;

    return TRUE;
}

/*
 * Return the token type for the two character token given by
 * ch1 and ch2.  Returns zero if there is none.
 */
static int
slaxDoubleWide (slax_data_t *sdp UNUSED, int ch1, int ch2)
{
#define DOUBLE_WIDE(ch1, ch2) (((ch1) << 8) | (ch2))
    switch (DOUBLE_WIDE(ch1, ch2)) {
    case DOUBLE_WIDE(':', '='): return L_ASSIGN;
    case DOUBLE_WIDE('&', '&'): return L_DAMPER;
    case DOUBLE_WIDE(':', ':'): return L_DCOLON;
    case DOUBLE_WIDE('=', '='): return L_DEQUALS;
    case DOUBLE_WIDE('.', '.'): return L_DOTDOT;
    case DOUBLE_WIDE('/', '/'): return L_DSLASH;
    case DOUBLE_WIDE('|', '|'): return L_DVBAR;
    case DOUBLE_WIDE('>', '='): return L_GRTREQ;
    case DOUBLE_WIDE('<', '='): return L_LESSEQ;
    case DOUBLE_WIDE('!', '='): return L_NOTEQUALS;
    case DOUBLE_WIDE('+', '='): return L_PLUSEQUALS;
    }

    return 0;
}

/*
 * Return the token type for the triple character token given by
 * ch1, ch2 and ch3.  Returns zero if there is none.
 */
static int
slaxTripleWide (slax_data_t *sdp UNUSED, int ch1, int ch2, int ch3)
{
    if (ch1 == '.' && ch2 == '.' && ch3 == '.')
	return L_DOTDOTDOT;	/* Only one (for now) */

    return 0;
}

/*
 * Returns the token type for the keyword at the start of
 * the input buffer, or zero if there isn't one.
 *
 * Ignore XPath keywords if they are not allowed.  Same for SLAX keywords.
 * For node test tokens, we look ahead for the open paren before
 * returning the token type.
 *
 * (Should this use a hash or something?)
 */
static int
slaxKeyword (slax_data_t *sdp)
{
    int slax_kwa = SLAX_KEYWORDS_ALLOWED(sdp);
    int xpath_kwa = XPATH_KEYWORDS_ALLOWED(sdp);
    keyword_mapping_t *kmp;
    int ch;

    for (kmp = keywordMap; kmp->km_string; kmp++) {
	if (slaxKeywordMatch(sdp, kmp->km_string)) {
	    if (slax_kwa && (kmp->km_flags & KMF_SLAX_KW))
		return kmp->km_ttype;

	    if (xpath_kwa && (kmp->km_flags & KMF_XPATH_KW))
		return kmp->km_ttype;

	    if (kmp->km_flags & KMF_NODE_TEST) {
		int look = sdp->sd_cur + strlen(kmp->km_string);

		for ( ; look < sdp->sd_len; look++) {
		    ch = sdp->sd_buf[look];
		    if (ch == '(')
			return kmp->km_ttype;
		    if (ch != ' ' && ch != '\t')
			break;
		}

		/* Didn't see the open paren, so it's not a node test */
	    }

	    return 0;
	}
    }

    return 0;
}

/*
 * Fill the input buffer, shifting data forward if we need the room
 * and dynamically reallocating the buffer if we still need room.
 */
static int
slaxGetInput (slax_data_t *sdp, int final)
{
    int first_read = (sdp->sd_buf == NULL);

    /*
     * If we're parsing XPath expressions, we alreay have the
     * complete string, so further reads should fail
     */
    if (sdp->sd_parse == M_PARSE_XPATH)
	return TRUE;

    for (;;) {
	if (sdp->sd_len + SD_BUF_FUDGE > sdp->sd_size) {
	    if (sdp->sd_start > SD_BUF_FUDGE) {
		/*
		 * Shift the data forward to give us some room at the end
		 */
		memcpy(sdp->sd_buf, sdp->sd_buf + sdp->sd_start,
		       sdp->sd_len - sdp->sd_start);
		sdp->sd_len -= sdp->sd_start;
		sdp->sd_cur -= sdp->sd_start;
		sdp->sd_start = 0;

	    } else {
		/*
		 * We need more room, so realloc() some
		 */
		int new_size = sdp->sd_size + SD_BUF_INCR;
		char *cp = realloc(sdp->sd_buf, new_size);
		if (cp == NULL) {
		    fprintf(stderr, "slax: lex: out of memory");
		    return TRUE;
		}

		sdp->sd_size = new_size;
		sdp->sd_buf = cp;
	    }
	}

	if ((sdp->sd_flags & SDF_EOF)
	    || fgets(sdp->sd_buf + sdp->sd_len, sdp->sd_size - sdp->sd_len,
		     sdp->sd_file) == NULL) {
	    if (slaxDebug)
		slaxLog("slax: lex: %s",
			feof(sdp->sd_file) ? "eof seen" : "read failed");
	    sdp->sd_flags |= SDF_EOF;

	    /*
	     * If we've got some data, we're cool.
	     */
	    if (sdp->sd_cur < sdp->sd_len)
		return FALSE;
	    return TRUE;
	}

	sdp->sd_len += strlen(sdp->sd_buf + sdp->sd_len);

	if (sdp->sd_len == 0)
	    continue;

	if (sdp->sd_buf[sdp->sd_len - 1] == '\n') {
	    sdp->sd_line += 1;
	    if (final && sdp->sd_ctxt->input)
		sdp->sd_ctxt->input->line = sdp->sd_line;
	}

	if (first_read && sdp->sd_buf[0] == '#' && sdp->sd_buf[1] == '!') {
	    /*
	     * If we hit a line on our first read that starts with "#!",
	     * then we skip the line.
	     */
	    sdp->sd_len = 0;
	    first_read = FALSE;
	    continue;
	}

	/* We don't want to get half a keyword */
	if (!final || slaxIsFinalChar(sdp->sd_buf[sdp->sd_len - 1]))
	    return FALSE;
    }
}

#define COMMENT_MARKER_SIZE 2	/* "\/\*" or "\*\/" */

/**
 * Make a child node and assign it proper file/line number info.
 *
 * The logic is much similar to how LIBXML2 (in SAX2.c) is assiging the line 
 * number in each nodes. It assigns the line number only when the context
 * has the 'linenumbers' set.
 *
 * The only difference is, while parsing the slax ctxt->input->line will be 
 * always one greater than the actual line number in the file. The reason
 * is, in xmlNewInputStream() the input->line is initialized to 1. While
 * we read the first line in slax input->line is already one and we increment 
 * that by 1. Because of this here we assign (input->line - 1) as line number 
 * in node.
 */
xmlNodePtr
slaxAddChildLineNo (xmlParserCtxtPtr ctxt, xmlNodePtr parent, xmlNodePtr cur)
{
    if (ctxt->linenumbers) { 
	if (ctxt->input != NULL) { 
	    if (ctxt->input->line < 65535) 
		cur->line = (short) ctxt->input->line;
	    else 
		cur->line = 65535;
	}
    }

    return xmlAddChild(parent, cur);
}

static inline int
slaxIsCommentStart (const char *buf)
{
    return (buf[0] == '/' && buf[1] == '*');
}

static inline int
slaxIsCommentEnd (const char *buf)
{
    return (buf[0] == '*' && buf[1] == '/');
}

/*
 * Drain (but do not discard) the comment.  Since we aren't moving
 * sd_start, the comment will remain in the input buffer until
 * we can insert it into the XSL tree.
 */
static int
slaxDrainComment (slax_data_t *sdp)
{
    for ( ; ; sdp->sd_cur++) {
	if (sdp->sd_cur >= sdp->sd_len) {
	    if (slaxGetInput(sdp, 1))
		return TRUE;
	}

	if (slaxIsCommentEnd(sdp->sd_buf + sdp->sd_cur)) {
	    sdp->sd_cur += COMMENT_MARKER_SIZE;	/* Move past "\*\/" */
	    return FALSE;
	}
    }
}

/**
 * Issue an error if the axis name is not valid
 *
 * @param sdp main slax data structure
 * @param axis name of the axis to check
 */
void
slaxCheckAxisName (slax_data_t *sdp, slax_string_t *axis)
{
    static const char *axis_names[] = {
	"ancestor",
	"ancestor-or-self",
	"attribute",
	"child",
	"descendant",
	"descendant-or-self",
	"following",
	"following-sibling",
	"namespace",
	"parent",
	"preceding",
	"preceding-sibling",
	"self",
	NULL
    };
    const char **namep;

    /*
     * Fix the token type correctly, since sometimes these are parsed
     * as T_BARE.
     */
    axis->ss_ttype = T_AXIS_NAME;

    for (namep = axis_names; *namep; namep++) {
	if (streq(*namep, axis->ss_token))
	    return;
    }

    xmlParserError(sdp->sd_ctxt,  "%s:%d: unknown axis name: %s\n",
		   sdp->sd_filename, sdp->sd_line, axis->ss_token);
}

/**
 * This function is the core of the lexer.
 *
 * We inspect the input buffer, finding the first token and returning
 * it's token type.
 *
 * @param sdp main slax data structure
 * @return token type
 */
static int
slaxLexer (slax_data_t *sdp)
{
    unsigned ch1, ch2, ch3;
    int look, rc;

    for (;;) {
	sdp->sd_start = sdp->sd_cur;

	if (sdp->sd_cur == sdp->sd_len)
	    if (slaxGetInput(sdp, 1))
		return -1;

	while (sdp->sd_cur < sdp->sd_len && isspace(sdp->sd_buf[sdp->sd_cur]))
	    sdp->sd_cur += 1;

	if (sdp->sd_cur != sdp->sd_len) {
	    /*
	     * See if we're at the start of a comment, but only
	     * if we're not in the middle of an XPath expression.
	     * We don't want "$foo / * [. = 2]" (without the spaces)
	     * to trigger a comment start.
	     */
	    if (SLAX_KEYWORDS_ALLOWED(sdp)
		    && slaxIsCommentStart(sdp->sd_buf + sdp->sd_cur)) {

		sdp->sd_start = sdp->sd_cur;
		if (slaxDrainComment(sdp))
		    return -1;

		/*
		 * So now we've got the comment.  We need to turn it
		 * into an XML comment and add it into the xsl tree.
		 */
		{
		    int start = sdp->sd_start + COMMENT_MARKER_SIZE;
		    int end = sdp->sd_cur - COMMENT_MARKER_SIZE;
		    int len = end - start;
		    unsigned char *buf = alloca(len + 1);
		    xmlNodePtr nodep;

		    while (isspace(sdp->sd_buf[start])) {
			if (sdp->sd_buf[start] == '\n')
			    break;
			start += 1;
			len -= 1;
		    }
		    while (end > start && isspace(sdp->sd_buf[end - 1])) {
			if (sdp->sd_buf[end - 1] == '\n')
			    break;
			end -= 1;
			len -= 1;
		    }
		    len = end - start;

		    if (len > 0) {
			len += 2; /* Two surrounding spaces */
			memcpy(buf + 1, sdp->sd_buf + start, len);
			buf[0] = ' ';
			buf[len - 1] = ' ';
			buf[len] = 0;

			nodep = xmlNewComment(buf);
			if (nodep) {
			    /*
			     * xsl:sort elements cannot contain comments
			     * so we may need to move the comment up one
			     * level in the output document
			     */
			    xmlNodePtr par = sdp->sd_ctxt->node;
			    if (slaxNodeIsXsl(par, ELT_SORT))
				par = par->parent;
			    xmlAddChild(par, nodep);
			}
		    }
		}

		continue;
	    }

	    break;
	}
    }

    sdp->sd_start = sdp->sd_cur;
	
    ch1 = sdp->sd_buf[sdp->sd_cur];
    ch2 = (sdp->sd_cur + 1 < sdp->sd_len) ? sdp->sd_buf[sdp->sd_cur + 1] : 0;
    ch3 = (sdp->sd_cur + 2 < sdp->sd_len) ? sdp->sd_buf[sdp->sd_cur + 2] : 0;

    if (ch1 < SLAX_MAX_CHAR) {
	if (tripleWide[ch1]) {
	    rc = slaxTripleWide(sdp, ch1, ch2, ch3);
	    if (rc) {
		sdp->sd_cur += 3;
		return rc;
	    }
	}

	if (doubleWide[ch1]) {
	    rc = slaxDoubleWide(sdp, ch1, ch2);
	    if (rc) {
		sdp->sd_cur += 2;
		return rc;
	    }
	}

	if (singleWide[ch1]) {
	    ch1 = singleWide[ch1];
	    sdp->sd_cur += 1;

	    if (ch1 == L_STAR) {
		/*
		 * If we see a "*", we have to think about if it's a
		 * L_STAR or an L_ASTERISK.  If we put them both into
		 * the same literal, we get a shift-reduce error since
		 * there's an ambiguity between "/ * /" and "foo * foo".
		 * Is it a node test or the multiplier operator?
		 * To discriminate, we look at the last token returned.
		 */
		if (sdp->sd_last > M_MULTIPLICATION_TEST_LAST)
		    return L_STAR; /* It's the multiplcation operator */
		else
		    return L_ASTERISK; /* It's a q_name (NCName) */
	    }

	    /*
	     * Underscore is a valid first character for an element
	     * name, which is troubling, since it's also the concatenation
	     * operator in SLAX.  We look ahead to see if the next
	     * character is a valid character before making our
	     * decision.
	     */
	    if (ch1 == L_UNDERSCORE) {
		if (!slaxIsBareChar(sdp->sd_buf[sdp->sd_cur]))
		    return ch1;
	    } else {
		return ch1;
	    }
	}

	if (ch1 == '\'' || ch1 == '"') {
	    /*
	     * Found a quoted string.  Scan for the end.  We may
	     * need to read some more, if the string is long.
	     */
	    sdp->sd_cur += 1;	/* Move past the first quote */
	    while (((unsigned char *) sdp->sd_buf)[sdp->sd_cur] != ch1) {
		int bump = (sdp->sd_buf[sdp->sd_cur] == '\\') ? 1 : 0;

		sdp->sd_cur += 1;

		if (sdp->sd_cur == sdp->sd_len) {
		    if (slaxGetInput(sdp, 0))
			return -1;
		}

		if (bump && sdp->sd_cur < sdp->sd_len)
		    sdp->sd_cur += bump;
	    }

	    sdp->sd_cur += 1;	/* Move past the end quote */
	    return T_QUOTED;
	}

	if (ch1 == '$') {
	    /*
	     * Found a variable; scan for the end of it.
	     */
	    sdp->sd_cur += 1;
	    while (sdp->sd_cur < sdp->sd_len
		   && slaxIsVarChar(sdp->sd_buf[sdp->sd_cur]))
		sdp->sd_cur += 1;
	    return T_VAR;
	}

	rc = slaxKeyword(sdp);
	if (rc) {
	    sdp->sd_cur += strlen(keywordString[slaxTokenTranslate(rc)]);
	    return rc;
	}

	if (isdigit(ch1)) {
	    while (sdp->sd_cur < sdp->sd_len
		   && isdigit(sdp->sd_buf[sdp->sd_cur]))
		sdp->sd_cur += 1;
	    return T_NUMBER;
	}
    }

    /*
     * Must have found a bare word or a function name, since they
     * are the only things left.  We scan forward for the next
     * character that doesn't fit in a T_BARE, but allow "foo:*"
     * as a special case.
     */
    for ( ; sdp->sd_cur < sdp->sd_len; sdp->sd_cur++) {
	if (sdp->sd_cur + 1 < sdp->sd_len && sdp->sd_buf[sdp->sd_cur] == ':'
		&& sdp->sd_buf[sdp->sd_cur + 1] == ':')
	    return T_AXIS_NAME;
	if (slaxIsBareChar(sdp->sd_buf[sdp->sd_cur]))
	    continue;
	if (sdp->sd_cur > sdp->sd_start && sdp->sd_buf[sdp->sd_cur] == '*'
		&& sdp->sd_buf[sdp->sd_cur - 1] == ':')
	    continue;
	break;
    }

    /*
     * It's a hack, but it's a standard-specified hack:
     * We need to see if this is a function name (T_FUNCTION_NAME)
     * or an NCName (q_name) (T_BARE).
     * So we look ahead for a '('.  If we find one, it's a function;
     * if not it's a q_name.
     */
    for (look = sdp->sd_cur; look < sdp->sd_len; look++) {
	ch1 = sdp->sd_buf[look];
	if (ch1 == '(')
	    return T_FUNCTION_NAME;
	if (!isspace(ch1))
	    break;
    }

    if (sdp->sd_cur == sdp->sd_start && sdp->sd_buf[sdp->sd_cur] == '#') {
	/*
	 * There's a special token "#default" that's used for
	 * namespace-alias.  It's an absurd hack, but we
	 * have to dummy it up as a T_BARE.
	 */
	static const char pdef[] = "#default";
	static const int plen = sizeof(pdef) - 1;
	if (sdp->sd_len - sdp->sd_cur > plen
	    && memcmp(sdp->sd_buf + sdp->sd_cur,
		      pdef, plen) == 0
	    && !slaxIsBareChar(sdp->sd_buf[sdp->sd_cur + plen])) {
	    sdp->sd_cur += sizeof(pdef) - 1;
	}
    }

    return T_BARE;
}

/**
 * Lexer function called from bison's yyparse()
 *
 * We lexically analyze the input and return the token type to
 * bison's yyparse function, which we've renamed to slaxParse.
 *
 * @param sdp main slax data structure
 * @param yylvalp pointer to bison's lval (stack value)
 * @return token type
 */
int
slaxYylex (slax_data_t *sdp, YYSTYPE *yylvalp)
{
    int rc, look;
    slax_string_t *ssp = NULL;

    if (!slaxSetup)
	slaxSetupLexer();

    /*
     * If we've saved a token type into sd_ttype, then we return
     * it as if we'd just lexed it.
     */
    if (sdp->sd_ttype) {
	rc = sdp->sd_ttype;
	sdp->sd_ttype = 0;

	if (yylvalp)		/* We don't have a real token */
	    *yylvalp = NULL;

	return rc;
    }

    /*
     * Discard the previous token by moving the start pointer
     * past it.
     */
    sdp->sd_start = sdp->sd_cur;

    rc = slaxLexer(sdp);
    if (M_OPERATOR_FIRST < rc && rc < M_OPERATOR_LAST
	&& sdp->sd_last < M_MULTIPLICATION_TEST_LAST)
	rc = T_BARE;

    /*
     * It's a hack, but it's a standard-specified hack:
     * We need to see if this is a function name (T_FUNCTION_NAME)
     * or an NCName (q_name) (T_BARE).
     * So we look ahead for a '('.  If we find one, it's a function;
     * if not it's an T_BARE;
     */
    if (rc == T_BARE) {
	for (look = sdp->sd_cur; look < sdp->sd_len; look++) {
	    unsigned char ch = sdp->sd_buf[look];
	    if (ch == '(') {
		rc = T_FUNCTION_NAME;
		break;
	    }

	    if (!isspace(ch))
		break;
	}
    }

    /*
     * And now our own ambiguity:  Template names look like function
     * names.  Since we have the nice "template" keyword, we can use
     * it to drive our decision.
     * Note that the grammar still has the "template_name" production
     * to handle backward compatibility with SLAX documents that don't
     * have the "template" keyword (which previously wasn't mandatory).
     * Hopefully one day, we can nix that (but it's no big deal).
     */
    if (rc == T_FUNCTION_NAME && sdp->sd_last == K_TEMPLATE)
	rc = T_BARE;

    /*
     * Save the token type in sd_last so we can do these same
     * hacks next time thru
     */
    sdp->sd_last = rc;

    if (rc > 0 && sdp->sd_start == sdp->sd_cur) {
	if (slaxDebug)
	    slaxLog("slax: lex: zero length token: %d/%s",
		      rc, slaxTokenName(rc));
	rc = M_ERROR;
	/*
	 * We're attempting to return a reasonable token type, but
	 * with a zero length token.  Something it very busted with
	 * the input.  We can't just sit here, but, well, there are
	 * no good options.  We're going to move the current point
	 * forward in the hope that we'll see good input eventually.
	 */
	sdp->sd_cur += 1;
    }

    if (yylvalp)
	*yylvalp = ssp = (rc > 0) ? slaxStringCreate(sdp, rc) : NULL;

    /*
     * If we're still looking at a function, determine if it's one
     * using SLAX_PREFIX, and record that fact so we can later (in
     * slaxElementPop()) ensure we've mapped the prefix.
     */
    if (rc == T_FUNCTION_NAME) {
	static const char slax_prefix[] = SLAX_PREFIX ":";
	const char *start = ssp->ss_token;
	unsigned len = strlen(ssp->ss_token);

	if (len > sizeof(slax_prefix)
		&& strncmp(slax_prefix, start, sizeof(slax_prefix) - 1) == 0)
	    ssp->ss_flags |= SSF_SLAXNS;
    }

    if (slaxDebug && ssp)
	slaxLog("slax: lex: (%s) %p '%.*s' -> %d/%s %x",
		  SLAX_KEYWORDS_ALLOWED(sdp) ? "keywords" : "nokeywords", ssp,
		  sdp->sd_cur - sdp->sd_start, sdp->sd_buf + sdp->sd_start,
		  rc, (rc > 0) ? slaxTokenName(rc) : "",
		  (ssp && ssp->ss_token) ? ssp->ss_token : 0);

    /*
     * Disable keywords processing based on the token returned
     */
    switch (rc) {
    case K_APPLY_TEMPLATES:
    case K_ATTRIBUTE:
    case K_COMMENT:
    case K_COPY_OF:
    case K_COUNT:
    case K_DECIMAL_FORMAT:
    case K_DIE:
    case K_ELEMENT:
    case K_EXPR:
    case K_FOR:
    case K_FOR_EACH:
    case K_FORMAT:
    case K_FROM:
    case K_GROUPING_SEPARATOR:
    case K_GROUPING_SIZE:
    case K_IF:
    case K_KEY:
    case K_LANGUAGE:
    case K_LETTER_VALUE:
    case K_MATCH:
    case K_MODE:
    case K_MVAR:
    case K_NS_TEMPLATE:
    case K_MESSAGE:
    case K_NUMBER:
    case K_PARAM:
    case K_PROCESSING_INSTRUCTION:
    case K_SORT:
    case K_TEMPLATE:
    case K_TERMINATE:
    case K_TRACE:
    case K_UEXPR:
    case K_VAR:
    case K_VALUE:
    case K_WHILE:
    case K_WITH:
	sdp->sd_flags |= SDF_NO_SLAX_KEYWORDS;
	break;

    case K_ATTRIBUTE_SET:
    case K_CALL:
    case K_CDATA_SECTION_ELEMENTS:
    case K_DOCTYPE_PUBLIC:
    case K_DOCTYPE_SYSTEM:
    case K_ENCODING:
    case K_FUNCTION:
    case K_INDENT:
    case K_MEDIA_TYPE:
    case K_NS:
    case K_NS_ALIAS:
    case K_OMIT_XML_DECLARATION:
    case K_PRESERVE_SPACE:
    case K_STANDALONE:
    case K_STRIP_SPACE:
    case K_USE_ATTRIBUTE_SETS:
    case K_VERSION:
	sdp->sd_flags |= SDF_NO_KEYWORDS;
	break;
    }

    return rc;
}
