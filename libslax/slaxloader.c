/*
 * $Id: slaxloader.c,v 1.6 2008/05/21 02:06:12 phil Exp $
 *
 * Copyright (c) 2006-2010, Juniper Networks, Inc.
 * All rights reserved.
 * See ../Copyright for the status of this software
 *
 * slaxloader.c -- load slax files into XSL trees
 *
 * This file contains the lexer for slax, as well as the utility
 * functions called by the productions in slaxparser.y.  It also
 * contains the hooks to attach the slax parser into the libxslt
 * parsing mechanism.
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
extern xsltDocLoaderFunc xsltDocDefaultLoader;
xsltDocLoaderFunc originalXsltDocDefaultLoader;

static int slaxEnabled;		/* Global enable (SLAX_*) */

/* Stub to handle xmlChar strings in "?:" expressions */
const xmlChar null[] = "";

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
	    if (sdp->sd_ctxt->input)
		sdp->sd_ctxt->input->line += 1;
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
	if (final && slaxIsFinalChar(sdp->sd_buf[sdp->sd_len - 1]))
	    return FALSE;
    }
}

#define COMMENT_MARKER_SIZE 2	/* "\/\*" or "\*\/" */

/*
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
static xmlNodePtr
slaxAddChildLineNo (xmlParserCtxtPtr ctxt, xmlNodePtr parent, 
		   xmlNodePtr cur)
{
    if (ctxt->linenumbers) { 
	if (ctxt->input != NULL) { 
	    if (ctxt->input->line < 65535) 
		cur->line = (short) ctxt->input->line - 1; 
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
    
/*
 * Detect a particular xsl:* node
 */
static int
slaxNodeIsXsl (xmlNodePtr node, const char *name)
{
    return (node && node->ns && node->ns->href
	    && streq((const char *) node->name, name)
	    && streq((const char *) node->ns->href, XSL_NS));
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

/**
 * Callback from bison when an error is detected.
 *
 * @param sdp main slax data structure
 * @param str error message
 * @param yylvalp stack entry from bison's lexical stack
 * @return zero
 */
int
slaxYyerror (slax_data_t *sdp, const char *str, YYSTYPE yylvalp)
{
    char *token = yylvalp ? yylvalp->ss_token : NULL;

    sdp->sd_errors += 1;

    xmlParserError(sdp->sd_ctxt, "%s:%d: %s%s%s%s\n",
		   sdp->sd_filename, sdp->sd_line, str,
		   token ? " before '" : "", token, token ? "': " : "");

    return 0;
}

/**
 * Check the version string.  The only supported version is "1.0".
 *
 * @param major major version number
 * @param minor minor version number
 */
void
slaxVersionMatch (slax_data_t *sdp, const char *major, const char *minor)
{
    if (major == NULL || !streq(major, "1")
	|| minor == NULL || !(streq(minor, "0") || streq(minor, "1"))) {
	fprintf(stderr, "invalid version number: %s.%s\n",
		major ?: "", minor ?: "");
	sdp->sd_errors += 1;
    }
}

/**
 * Add a namespace to the node on the top of the stack
 *
 * @param sdp main slax data structure
 * @param prefix namespace prefix
 * @param uri namespace URI
 */
void
slaxNsAdd (slax_data_t *sdp, const char *prefix, const char *uri)
{
    xmlNsPtr ns;

    ns = xmlNewNs(sdp->sd_ctxt->node, (const xmlChar *) uri,
		  (const xmlChar *) prefix);

    /*
     * set the namespace node, making sure that if the default namspace
     * is unbound on a parent we simply keep it NULL
     */
    if (ns) {
	xmlNsPtr cur = sdp->sd_ctxt->node->ns;
	if (cur) {
	    if ((cur->prefix == NULL && ns->prefix == NULL)
		|| (cur->prefix && ns->prefix
		    && streq((const char *) cur->prefix,
			     (const char *) ns->prefix)))
		xmlSetNs(sdp->sd_ctxt->node, ns);
	} else {
	    if (ns->prefix == NULL)
		xmlSetNs(sdp->sd_ctxt->node, ns);
	}
    }
}

/**
 * Find a namespace definition in the node's parent chain that
 * matches the given prefix.  Since the prefix string lies in situ
 * in the original tag, we give the length of the prefix.
 *
 * @param nodep the node where the search begins
 * @param prefix the prefix string
 * @param len the length of the prefix string
 * @return pointer to the existing name space 
 */
static xmlNsPtr
slaxFindNs (xmlNodePtr nodep, const char *prefix, int len)
{
    xmlChar *name;

    name = alloca(len + 1);
    if (name == NULL)
	return NULL;

    memcpy(name, prefix, len);
    name[len] = '\0';
    return xmlSearchNs(nodep->doc, nodep, name);
}

/*
 * Add a simple value attribute.  Our style parameter is limited to
 * SAS_NONE, SAS_XPATH, or SAS_SELECT.  See the other slaxAttribAdd*()
 * variants for other styles.
 */
void
slaxAttribAdd (slax_data_t *sdp, int style,
	       const char *name, slax_string_t *value)
{
    slax_string_t *ssp;
    char *buf;
    xmlNsPtr ns = NULL;
    xmlAttrPtr attr;
    const char *cp;
    int ss_flags = (style == SAS_SELECT) ? SSF_CONCAT  : 0;

    ss_flags |= SSF_QUOTES;	/* Always want the quotes */

    if (value == NULL)
	return;

    if (slaxDebug)
	for (ssp = value; ssp; ssp = ssp->ss_next)
	    slaxLog("initial: xpath_value: %s", ssp->ss_token);

    if (value->ss_next == NULL) {
	if (style == SAS_SELECT && value->ss_ttype == T_QUOTED
		&& (value->ss_flags & SSF_BOTHQS)) {
	    /*
	     * The string contains both types of quotes, which is bad news,
	     * but we are 'select' style, so we just throw the string into
	     * a text node.
	     */
	    xmlNodePtr tp;

	    tp = xmlNewText((const xmlChar *) value->ss_token);
	    slaxAddChildLineNo(sdp->sd_ctxt, sdp->sd_ctxt->node, tp);

	    return;
	}

	/*
	 * There is only one value, so we stuff it into the
	 * attribute give in the 'name' parameter.
	 */
	buf = slaxStringAsConcat(value, SSF_BRACES | ss_flags);

    } else {
	/*
	 * There are multiple values, like: var $x=test/one _ test/two
	 * We stuff these values inside a call to the XPath "concat()"
	 * function, which will do all the hard work for us, without
	 * giving us a result fragment tree.
	 */
	buf = slaxStringAsConcat(value, SSF_BRACES | ss_flags);
    }

    /*
     * Deal with namespaces.  If there's a prefix, we need to find
     * the definition of this namespace and pass it along.
     */
    cp = index(name, ':');
    if (cp) {
	const char *prefix = name;
	int len = cp - prefix;
	name = cp + 1;
	ns = slaxFindNs(sdp->sd_ctxt->node, prefix, len);
    }

    attr = xmlNewNsProp(sdp->sd_ctxt->node, ns, (const xmlChar *) name,
		      (const xmlChar *) buf);
    if (attr == NULL)
	fprintf(stderr, "could not make attribute: @%s=%s\n", name, buf);

    free(buf);
}

/*
 * Add a value to an attribute on an XML element.  The value consists of
 * one or more items, each of which can be a string, a variable reference,
 * or an xpath expression.  If the value (any items) does not contain an
 * xpath expression, we use the normal attribute substitution to achieve
 * this ({$foo}).  Otherwise, we use the xsl:attribute element to construct
 * the attribute.  Note that this function uses only the SAS_ATTRIB style
 * of quote handling.
 */
void
slaxAttribAddValue (slax_data_t *sdp, const char *name, slax_string_t *value)
{
    int len;
    xmlAttrPtr attr;
    xmlNsPtr ns = NULL;
    char *buf;
    const char *cp;

    buf = slaxStringAsValueTemplate(value, SSF_BRACES);
    if (buf == NULL)
	return;

    /*
     * Deal with namespaces.  If there's a prefix, we need to find
     * the definition of this namespace and pass it along.
     */
    cp = index(name, ':');
    if (cp) {
	const char *prefix = name;
	len = cp - prefix;
	name = cp + 1;
	ns = slaxFindNs(sdp->sd_ctxt->node, prefix, len);
    }

    attr = xmlNewNsProp(sdp->sd_ctxt->node, ns, (const xmlChar *) name,
		      (const xmlChar *) buf);
    if (attr == NULL)
	fprintf(stderr, "could not make attribute: @%s=%s\n", name, buf);

    xmlFreeAndEasy(buf);
}

/*
 * Add a simple value attribute as straight string.  Note that this function
 * always uses the SAS_XPATH style of quote handling.
 */
void
slaxAttribAddString (slax_data_t *sdp, const char *name,
		     slax_string_t *value, unsigned flags)
{
    slax_string_t *ssp;
    char *buf;
    xmlAttrPtr attr;

    if (value == NULL)
	return;

    if (slaxDebug)
	for (ssp = value; ssp; ssp = ssp->ss_next)
	    slaxLog("initial: xpath_value: %s", ssp->ss_token);
	
    buf = slaxStringAsChar(value, flags);
    if (buf) {
	attr = xmlNewProp(sdp->sd_ctxt->node, (const xmlChar *) name,
			  (const xmlChar *) buf);
	if (attr == NULL)
	    fprintf(stderr, "could not make attribute: @%s=%s\n", name, buf);

	xmlFree(buf);
    }
}

/*
 * Add a literal value as an attribute to the current node
 */
void
slaxAttribAddLiteral (slax_data_t *sdp, const char *name, const char *val)
{
    xmlAttrPtr attr;

    attr = xmlNewProp(sdp->sd_ctxt->node, (const xmlChar *) name,
		      (const xmlChar *) val);
    if (attr == NULL)
	fprintf(stderr, "could not make attribute: @%s=%s\n", name, val);
}

/*
 * Extend the existing value for an attribute, appending the given value.
 */
static void
slaxNodeAttribExtend (slax_data_t *sdp, xmlNodePtr nodep,
		  const char *attrib, const char *value)
{
    const xmlChar *uattrib = (const xmlChar *) attrib;
    xmlChar *current = xmlGetProp(nodep, uattrib);
    int clen = current ? xmlStrlen(current) + 1 : 0;
    int vlen = strlen(value) + 1;
    xmlAttrPtr attr;

    unsigned char *newp = alloca(clen + vlen);
    if (newp == NULL) {
	xmlParserError(sdp->sd_ctxt, "%s:%d: out of memory",
		       sdp->sd_filename, sdp->sd_line);
	return;
    }

    if (clen) {
	memcpy(newp, current, clen - 1);
	newp[clen - 1] = ' ';
	xmlFree(current);
    }

    memcpy(newp + clen, value, vlen);

    attr = xmlSetProp(nodep, uattrib, newp);
}

/*
 * Extend the existing value for an attribute, appending the given value.
 */
void
slaxAttribExtend (slax_data_t *sdp, const char *attrib, const char *value)
{
    slaxNodeAttribExtend(sdp, sdp->sd_ctxt->node, attrib, value);
}

/*
 * Backup the stack up 'til the given node is seen
 */
slax_string_t *
slaxStackClear (slax_data_t *sdp UNUSED, slax_string_t **sspp,
		slax_string_t **top)
{
    slax_string_t *ssp;

    while (*sspp == NULL && sspp < top)
	sspp += 1;

    for ( ; sspp <= top; sspp++) {
	ssp = *sspp;
	if (ssp) {
	    slaxStringFree(ssp);
	    *sspp = NULL;
	}
    }

    return NULL;
}

/*
 * Backup the stack up 'til the given node is seen; return the given node.
 */
slax_string_t *
slaxStackClear2 (slax_data_t *sdp, slax_string_t **sspp,
		 slax_string_t **top, slax_string_t **retp)
{
    slax_string_t *ssp;

    ssp = *retp;
    *retp = NULL;
    (void) slaxStackClear(sdp, sspp, top);
    return ssp;
}

/*
 * Add an XSL element to the node at the top of the context stack
 */
xmlNodePtr
slaxElementAdd (slax_data_t *sdp, const char *tag,
	       const char *attrib, const char *value)
{
    xmlNodePtr nodep;

    nodep = xmlNewNode(sdp->sd_xsl_ns, (const xmlChar *) tag);
    if (nodep == NULL) {
	fprintf(stderr, "could not make node: %s\n", tag);
	return NULL;
    }

    slaxAddChildLineNo(sdp->sd_ctxt, sdp->sd_ctxt->node, nodep);

    if (attrib) {
	xmlAttrPtr attr = xmlNewProp(nodep, (const xmlChar *) attrib,
				     (const xmlChar *) value);
	if (attr == NULL)
	    fprintf(stderr, "could not make attribute: %s/@%s\n", tag, attrib);
    }

    return nodep;
}

/*
 * Add an XSL element to the node at the top of the context stack
 */
xmlNodePtr
slaxElementAddString (slax_data_t *sdp, const char *tag,
	       const char *attrib, slax_string_t *value)
{
    xmlNodePtr nodep;

    nodep = xmlNewNode(sdp->sd_xsl_ns, (const xmlChar *) tag);
    if (nodep == NULL) {
	fprintf(stderr, "could not make node: %s\n", tag);
	return NULL;
    }

    slaxAddChildLineNo(sdp->sd_ctxt, sdp->sd_ctxt->node, nodep);

    if (attrib) {
	char *full = slaxStringAsChar(value, 0);
	xmlAttrPtr attr = xmlNewProp(nodep, (const xmlChar *) attrib,
				     (const xmlChar *) full);
	if (attr == NULL)
	    fprintf(stderr, "could not make attribute: %s/@%s\n", tag, attrib);

	xmlFreeAndEasy(full);
    }

    return nodep;
}

/*
 * Add an XSL element to the top of the context stack
 */
xmlNodePtr
slaxElementPush (slax_data_t *sdp, const char *tag,
		 const char *attrib, const char *value)
{
    xmlNodePtr nodep;

    nodep = slaxElementAdd(sdp, tag, attrib, value);
    if (nodep == NULL) {
	xmlParserError(sdp->sd_ctxt, "%s:%d: could not add element '%s'",
		       sdp->sd_filename, sdp->sd_line, tag);
	return NULL;
    }

    nodePush(sdp->sd_ctxt, nodep);
    return nodep;
}

/*
 * Pop an XML element off the context stack
 */
void
slaxElementPop (slax_data_t *sdp)
{
    nodePop(sdp->sd_ctxt);
}

/*
 * Relocate the most-recent "sort" node, if the parent was a "for"
 * loop.  The "sort" should be at the top of the stack.  If the parent
 * is a "for", then the parser will have built two nested "for-each"
 * loops, and we really want the sort to apply to the parent (outer)
 * loop.  So if we've hitting these conditions, move the sort node.
 */
void
slaxRelocateSort (slax_data_t *sdp)
{
    xmlNodePtr nodep = sdp->sd_ctxt->node;

    if (nodep && nodep->parent) {
	xmlNodePtr parent = nodep->parent;
        xmlChar *sel = xmlGetProp(parent, (const xmlChar *) ATT_SELECT);

	if (sel && strncmp((char *) sel, "$slax-dot-", 10) == 0) {
	    slaxLog("slaxRelocateSort: %s:%d: must relocate",
		      nodep->name, xmlGetLineNo(nodep));
	    /*
	     * "parent" is the inner for-each loop, and "parent->parent"
	     * is the outer for-each loop.  Add the sort as the first
	     * child of the outer loop.  xmlAddPrevSibling will unlink
	     * nodep from its current location.
	     */
	    xmlAddPrevSibling(parent->parent->children, nodep);
	}

	xmlFreeAndEasy(sel);
    }
}

/*
 * Look upward thru the stack to find a namespace that's the
 * default (one with no prefix).
 */
static xmlNsPtr
slaxFindDefaultNs (slax_data_t *sdp UNUSED)
{
    xmlNsPtr ns = NULL;
    xmlNodePtr nodep;

    for (nodep = sdp->sd_ctxt->node; nodep; nodep = nodep->parent)
	for (ns = nodep->nsDef; ns; ns = ns->next)
	    if (ns->prefix == NULL)
		return ns;

    return NULL;
}

/*
 * Add an element to the top of the context stack
 */
void
slaxElementOpen (slax_data_t *sdp, const char *tag)
{
    xmlNodePtr nodep = sdp->sd_ctxt->node;
    xmlNsPtr ns = NULL;
    const char *cp;

    /*
     * Deal with namespaces.  If there's a prefix, we need to find
     * the definition of this namespace and pass it along.
     */
    cp = index(tag, ':');
    if (cp) {
	const char *prefix = tag;
	int len = cp - prefix;
	tag = cp + 1;
	ns = slaxFindNs(nodep, prefix, len);
    }

    /* If we don't have a namespace, use the parent's namespace */
    if (ns == NULL)
	ns = slaxFindDefaultNs(sdp);

    nodep = xmlNewNode(ns, (const xmlChar *) tag);
    if (nodep == NULL) {
	fprintf(stderr, "could not make node: %s\n", tag);
	return;
    }

    slaxAddChildLineNo(sdp->sd_ctxt, sdp->sd_ctxt->node, nodep);
    nodePush(sdp->sd_ctxt, nodep);
}

/*
 * Pop an element off the context stack
 */
void
slaxElementClose (slax_data_t *sdp)
{
    nodePop(sdp->sd_ctxt);
}

/*
 * Add an xsl:comment node
 */
xmlNodePtr
slaxCommentAdd (slax_data_t *sdp, slax_string_t *value)
{
    xmlNodePtr nodep;
    nodep = slaxElementAdd(sdp, ELT_COMMENT, NULL, NULL);
    if (nodep) {
	if (slaxStringIsSimple(value, T_QUOTED)) {
	    xmlNodePtr tp = xmlNewText((const xmlChar *) value->ss_token);

	    if (tp)
		slaxAddChildLineNo(sdp->sd_ctxt, nodep, tp);

	} else {
	    xmlNodePtr attrp;
	    nodePush(sdp->sd_ctxt, nodep);

	    attrp = slaxElementAdd(sdp, ELT_VALUE_OF, NULL, NULL);
	    if (attrp) {
		nodePush(sdp->sd_ctxt, attrp);
		slaxAttribAdd(sdp, SAS_SELECT, ATT_SELECT, value);
		nodePop(sdp->sd_ctxt);
	    }
	    nodePop(sdp->sd_ctxt);
	}
    }

    return nodep;
}

/*
 * Find or construct a (possibly temporary) namespace node
 * for the "func" exslt library and put the given node into
 * that namespace.  We also have to add this as an "extension"
 * namespace.
 */
static void 
slaxSetNs (slax_data_t *sdp, xmlNodePtr nodep,
	   const char *prefix, const xmlChar *uri, int local)
{
    xmlNsPtr nsp;

    nsp = xmlSearchNs(sdp->sd_docp, nodep->parent, (const xmlChar *) prefix);
    if (nsp == NULL) {
	xmlNodePtr root = xmlDocGetRootElement(sdp->sd_docp);
	nsp = xmlNewNs(root, uri, (const xmlChar *) prefix);
	if (nsp == NULL) {
	    xmlParserError(sdp->sd_ctxt, "%s:%d: out of memory",
			   sdp->sd_filename, sdp->sd_line);
	    return;
	}
	/*
	 * Since we added this namespace, we need to add it to the
	 * list of extension prefixes.
	 */
	slaxNodeAttribExtend(sdp, root,
			     ATT_EXTENSION_ELEMENT_PREFIXES, prefix);
    }

    /* Add a distinct namespace to the current node */
    nsp = xmlNewNs(nodep, uri, (const xmlChar *) prefix);
    if (local)
	nodep->ns = nsp;
}


/*
 * Find or construct a (possibly temporary) namespace node
 * for the "func" exslt library and put the given node into
 * that namespace.  We also have to add this as an "extension"
 * namespace.
 */
void 
slaxSetFuncNs (slax_data_t *sdp, xmlNodePtr nodep)
{
    const char *prefix = FUNC_PREFIX;
    const xmlChar *uri = FUNC_URI;

    slaxSetNs(sdp, nodep, prefix, uri, TRUE);
}

void 
slaxSetTraceNs (slax_data_t *sdp, xmlNodePtr nodep)
{
    const char *prefix = TRACE_PREFIX;
    const xmlChar *uri = (const xmlChar *) TRACE_URI;

    slaxSetNs(sdp, nodep, prefix, uri, TRUE);
}

void 
slaxSetSlaxNs (slax_data_t *sdp, xmlNodePtr nodep, int local)
{
    const char *prefix = SLAX_PREFIX;
    const xmlChar *uri = (const xmlChar *) SLAX_URI;

    slaxSetNs(sdp, nodep, prefix, uri, local);
}

/*
 * If we know we're about to assign a result tree fragment (RTF)
 * to a variable, punt and do The Right Thing.
 *
 * When we're called, the variable statement has ended, so the
 * <xsl:variable> should be the node on the top of the stack.
 * If there's something inside the element (as opposed to a
 * "select" attribute or no content), then this must be building
 * an RTF, which we dislike.  Instead we convert the current variable
 * into a temporary variable, and build the real variable using a
 * call to "ext:node-set()".  We'll need define the "ext" namespace
 * on the same (new) element, so we can ensure that it's been created.
 */
void
slaxAvoidRtf (slax_data_t *sdp)
{

    static const char new_value_format[] = EXT_PREFIX ":node-set($%s)";
    static const char node_value_format[] = EXT_PREFIX ":node-set(%s)";
    static const char temp_name_format[] = "%s-temp-%u";

    static unsigned temp_count;

    xmlNodePtr nodep = sdp->sd_ctxt->node;
    xmlNodePtr newp;
    xmlChar *name;
    char *temp_name, *value, *old_value;
    const char *format;
    int clen, vlen;
    xmlChar *sel;

    /*
     * Parameters and variables can be assigned values in two ways,
     * via the 'select' attribute and via contents of the element
     * itself.  In most cases, the select attribute is not an RTF
     * and doesn't need the node-set() functionality.  The main exception
     * is <func:function>s, which are cursed to return RTFs.  Since
     * we can't "know" if the expression is a func:function and since
     * the cost of node-set() is minimal, we just wrap the select
     * expression in a node-set call.
     */
    sel = xmlGetProp(nodep, (const xmlChar *) ATT_SELECT);
    if (sel) {
	if (nodep->children != NULL) {
	    xmlParserError(sdp->sd_ctxt,
			   "%s:%d: %s cannot have both select and children\n",
			   sdp->sd_filename, sdp->sd_line, nodep->name);
	    return;
	}

	format = node_value_format;
	old_value = (char *) sel;
	vlen = strlen(old_value);

    } else {

	name = xmlGetProp(nodep, (const xmlChar *) ATT_NAME);
	clen = name ? xmlStrlen(name) + 1 : 0;

	/*
	 * Generate the new temporary variable name using the original
	 * name, the string "-temp-", and a monotonically increasing
	 * number.
	 */
	vlen = clen + strlen(temp_name_format) + 10 + 1; /* 10 is max %u */
	temp_name = alloca(vlen);
	snprintf(temp_name, vlen, temp_name_format, name, ++temp_count);

	(void) xmlSetProp(sdp->sd_ctxt->node, (const xmlChar *) ATT_NAME,
			  (xmlChar *) temp_name);

	/*
	 * Now generate the new element, using the original, user-provided
	 * name and a call to "slax:node-set" function.
	 */
	newp = xmlNewNode(sdp->sd_xsl_ns, nodep->name);
	if (newp == NULL) {
	    fprintf(stderr, "could not make node: %s\n", nodep->name);
	    xmlFreeAndEasy(name);
	    return;
	}

	xmlAddChild(nodep->parent, newp);

	/* Use the line number from the original node */
	if (sdp->sd_ctxt->linenumbers)
	    newp->line = nodep->line;

	xmlNewProp(newp, (const xmlChar *) ATT_NAME, (const xmlChar *) name);

	format = new_value_format;
	old_value = temp_name;
	nodep = newp;
	xmlFreeAndEasy(name);
    }

    /* Build the value for the new variable */
    vlen += strlen(format) + 1;
    value = alloca(vlen);
    snprintf(value, vlen, format, old_value);

    slaxLog("AvoidRTF: '%s'", value);

    xmlSetNsProp(nodep, NULL, (const xmlChar *) ATT_SELECT, (xmlChar *) value);

    /* Add the namespace for 'ext' */
    xmlNewNs(nodep, (const xmlChar *) EXT_URI, (const xmlChar *) EXT_PREFIX);

    xmlFreeAndEasy(sel);
}

/**
 * Create namespace alias between the prefix given and the
 * containing (current) prefix.
 *
 * @param sdp main slax data structure
 * @param style stylesheet prefix value (as declared in the "ns" statement)
 * @param results result prefix value
 */
void
slaxNamespaceAlias (slax_data_t *sdp, slax_string_t *style,
		    slax_string_t *results)
{
    const char *alias = style->ss_token;

    xmlNodePtr nodep;
    nodep = slaxElementPush(sdp, ELT_NAMESPACE_ALIAS,
			    ATT_STYLESHEET_PREFIX, alias);

    if (nodep) {
	slaxAttribAddString(sdp, ATT_RESULT_PREFIX, results, 0);
	slaxElementPop(sdp);
    }
}

/**
 * Add an XPath expression as a statement.  If this is a string,
 * we place it inside an <xsl:text> element.  Otherwise we
 * put the value inside an <xsl:value-of>'s select attribute.
 *
 * @param sdp main slax data structure
 * @param value XPath expression to add
 * @param text_as_elt always use <xsl:text> for text data
 * @param disable_escaping add the "disable-output-escaping" attribute
 */
void
slaxElementXPath (slax_data_t *sdp, slax_string_t *value,
		  int text_as_elt, int disable_escaping)
{
    char *buf;
    xmlNodePtr nodep;
    xmlAttrPtr attr;

    if (slaxStringIsSimple(value, T_QUOTED)) {
	char *cp = value->ss_token;
	int len = strlen(cp);

	/*
	 * if the text string has leading or trailing whitespace.
	 * then we must use a text element.
	 */
	if (disable_escaping)
	    text_as_elt = TRUE;
	else if (*cp == '\t' || *cp == ' ' || *cp == '\0')
	    text_as_elt = TRUE;
	else {
	    char *ep = cp + len - 1;
	    if (*ep == '\t' || *ep == ' ')
		text_as_elt = TRUE;
	}

	nodep = xmlNewText((const xmlChar *) cp);
	if (nodep == NULL)
	    fprintf(stderr, "could not make node: text\n");
	else if (text_as_elt) {
	    xmlNodePtr textp = xmlNewNode(sdp->sd_xsl_ns,
					 (const xmlChar *) ELT_TEXT);
	    if (textp == NULL) {
		fprintf(stderr, "could not make node: %s\n", ELT_TEXT);
		return;
	    }

	    if (disable_escaping) {
		xmlNewNsProp(textp, NULL,
			     (const xmlChar *) ATT_DISABLE_OUTPUT_ESCAPING,
			     (const xmlChar *) "yes");

	    }

	    slaxAddChildLineNo(sdp->sd_ctxt, textp, nodep);
	    xmlAddChild(sdp->sd_ctxt->node, textp);

	} else {
	    slaxAddChildLineNo(sdp->sd_ctxt, sdp->sd_ctxt->node, nodep);
	}

	return;
    }

    nodep = xmlNewNode(sdp->sd_xsl_ns, (const xmlChar *) ELT_VALUE_OF);
    if (nodep == NULL) {
	fprintf(stderr, "could not make node: %s\n", ELT_VALUE_OF);
	return;
    }

    buf = slaxStringAsConcat(value, SSF_CONCAT | SSF_QUOTES);
    if (buf == NULL) {
	fprintf(stderr, "could not make attribute string: @%s=%s\n",
		ATT_SELECT, buf);
	return;
    }

    attr = xmlNewProp(nodep, (const xmlChar *) ATT_SELECT,
		      (const xmlChar *) buf);
    if (attr == NULL) {
	fprintf(stderr, "could not make attribute: @%s=%s\n",
		ATT_SELECT, buf);
	free(buf);
	return;
    }

    free(buf);

    if (disable_escaping) {
	xmlNewNsProp(nodep, NULL,
		     (const xmlChar *) ATT_DISABLE_OUTPUT_ESCAPING,
		     (const xmlChar *) "yes");
    }

    slaxAddChildLineNo(sdp->sd_ctxt, sdp->sd_ctxt->node, nodep);

}

/*
 * Check to see if an "if" statement had no "else".  If so,
 * we can rewrite it from an "xsl:choose" into an "xsl:if".
 */
void
slaxCheckIf (slax_data_t *sdp, xmlNodePtr choosep)
{
    xmlNodePtr nodep;
    int count = 0;

    if (choosep == NULL)
	return;

    for (nodep = choosep->children; nodep; nodep = nodep->next) {
	if (count++ > 0)
	    return;

	if (nodep->type != XML_ELEMENT_NODE)
	    return;

	if (!streq((const char *) nodep->name, ELT_WHEN))
	    return;
    }

    /*
     * So we know that choosep has exactly one child that is
     * an "xsl:when".  We need to turn the "when" into an "if"
     * and re-parent it under the current context node.
     */
    nodep = choosep->children;

    xmlUnlinkNode(nodep);
    xmlFree(const_drop(nodep->name));

    nodep->name = xmlStrdup((const xmlChar *) ELT_IF);
    if (nodep->name == NULL)
	return;

    xmlUnlinkNode(choosep);
    xmlFreeNode(choosep);
    xmlAddChild(sdp->sd_ctxt->node, nodep);
}

/*
 * Build the initial document, with the heading and top-level element.
 */
static xmlDocPtr
slaxBuildDoc (slax_data_t *sdp, xmlParserCtxtPtr ctxt)
{
    xmlDocPtr docp;
    xmlNodePtr nodep;

    docp = xmlNewDoc((const xmlChar *) XML_DEFAULT_VERSION);
    if (docp == NULL)
	return NULL;

    docp->standalone = 1;

    nodep = xmlNewNode(NULL, (const xmlChar *) ELT_STYLESHEET);
    if (nodep) {

	sdp->sd_xsl_ns = xmlNewNs(nodep, (const xmlChar *) XSL_NS,
				  (const xmlChar *) XSL_PREFIX);
	xmlSetNs(nodep, sdp->sd_xsl_ns); /* Noop if NULL */

	xmlDocSetRootElement(docp, nodep);
	nodePush(ctxt, nodep);

	(void) xmlNewProp(nodep, (const xmlChar *) ATT_VERSION,
			  (const xmlChar *) XSL_VERSION);
    }

    if (ctxt->dict) {
	docp->dict = ctxt->dict;
	xmlDictReference(docp->dict);
    }

    return docp;
}

static void
slaxDataCleanup (slax_data_t *sdp)
{
    if (sdp->sd_buf) {
	free(sdp->sd_buf);
	sdp->sd_buf = NULL;
	sdp->sd_cur = sdp->sd_size = 0;
    }

    sdp->sd_ns = NULL;		/* We didn't allocate this */

    if (sdp->sd_ctxt) {
	xmlFreeParserCtxt(sdp->sd_ctxt);
	sdp->sd_ctxt = NULL;
    }

    if (sdp->sd_docp) {
	xmlFreeDoc(sdp->sd_docp);
	sdp->sd_docp = NULL;
    }
}

/**
 * Read a SLAX file from an open file pointer
 *
 * @param filename Name of the file (or "-")
 * @param file File pointer for input
 * @param dict libxml2 dictionary
 * @param partial TRUE if parsing partial SLAX contents
 * @return xml document pointer
 */
xmlDocPtr
slaxLoadFile (const char *filename, FILE *file, xmlDictPtr dict, int partial)
{
    slax_data_t sd;
    xmlDocPtr res;
    int rc;
    xmlParserCtxtPtr ctxt = xmlNewParserCtxt();

    /*
     * Turn on line number recording in each node
     */
    ctxt->linenumbers = 1;

    if (ctxt == NULL)
	return NULL;

    if ((dict != NULL) && (ctxt->dict != NULL)) {
	xmlDictFree(ctxt->dict);
	ctxt->dict = NULL;
    }

    if (dict) {
    	ctxt->dict = dict;
 	xmlDictReference(ctxt->dict);
    }

    bzero(&sd, sizeof(sd));

    /* We want to parse SLAX, either full or partial */
    sd.sd_parse = sd.sd_ttype = partial ? M_PARSE_PARTIAL : M_PARSE_SLAX;

    strncpy(sd.sd_filename, filename, sizeof(sd.sd_filename));
    sd.sd_file = file;

    sd.sd_ctxt = ctxt;

    ctxt->version = xmlCharStrdup(XML_DEFAULT_VERSION);
    ctxt->userData = &sd;

    /*
     * Fake up an inputStream so the error mechanisms will work
     */
    if (filename)
	xmlSetupParserForBuffer(ctxt, (const xmlChar *) "", filename);

    sd.sd_docp = slaxBuildDoc(&sd, ctxt);
    if (sd.sd_docp == NULL) {
	slaxDataCleanup(&sd);
	return NULL;
    }

    if (filename != NULL)
        sd.sd_docp->URL = (xmlChar *) xmlStrdup((const xmlChar *) filename);

    rc = slaxParse(&sd);

    if (sd.sd_errors) {
	xmlParserError(ctxt, "%s: %d error%s detected during parsing\n",
		sd.sd_filename, sd.sd_errors, (sd.sd_errors == 1) ? "" : "s");

	slaxDataCleanup(&sd);
	return NULL;
    }

    /* Save docp before slaxDataCleanup nukes it */
    res = sd.sd_docp;
    sd.sd_docp = NULL;
    slaxDataCleanup(&sd);

    return res;
}

/*
 * Determine if a file is a SLAX file.  This is currently based on
 * the filename extension, but should one day be more.
 */
static int
slaxIsSlaxFile (const char *filename)
{
    static char slax_ext[] = ".slax";
    size_t len = strlen(filename);

    if (slaxEnabled == SLAX_FORCE)
	return TRUE;

    if (len < sizeof(slax_ext) - 1
	|| memcmp(&filename[len - sizeof(slax_ext) + 1],
		  slax_ext, sizeof(slax_ext)) != 0)
	return FALSE;

    return TRUE;
}

/*
 * A hook into the XSLT parsing mechanism, that gives us first
 * crack at parsing a stylesheet.
 */
static xmlDocPtr
slaxLoader(const xmlChar * URI, xmlDictPtr dict, int options,
	   void *callerCtxt, xsltLoadType type)
{
    FILE *file;
    xmlDocPtr docp;

    if (!slaxIsSlaxFile((const char *) URI))
	return originalXsltDocDefaultLoader(URI, dict, options,
					    callerCtxt, type);

    if (URI[0] == '-' && URI[1] == 0)
	file = stdin;
    else {
	file = fopen((const char *) URI, "r");
	if (file == NULL) {
	    slaxLog("slax: file open failed for '%s': %s",
		      URI, strerror(errno));
	    return NULL;
	}
    }

    docp = slaxLoadFile((const char *) URI, file, dict, 0);

    if (file != stdin)
	fclose(file);

    return docp;
}

/*
 * Read a SLAX stylesheet from an open file descriptor.
 * Written as a clone of libxml2's xmlCtxtReadFd().
 */
xmlDocPtr
slaxCtxtReadFd(xmlParserCtxtPtr ctxt, int fd, const char *URL,
	       const char *encoding, int options)
{
    FILE *file;
    xmlDocPtr docp;

    if (!slaxIsSlaxFile((const char *) URL))
	return xmlCtxtReadFd(ctxt, fd, URL, encoding, options);

    if (fd == 0)
	file = stdin;
    else {
	file = fdopen(fd, "r");
	if (file == NULL) {
	    slaxLog("slax: file open failed for %s: %s",
		      URL, strerror(errno));
	    return NULL;
	}
    }

    docp = slaxLoadFile(URL, file, ctxt->dict, 0);

    if (file != stdin)
	fclose(file);

    return docp;
}

/*
 * Turn on the SLAX XSLT document parsing hook.  This must be
 * called before SLAX files can be parsed.
 */
void
slaxEnable (int enable)
{
    if (enable == SLAX_CLEANUP) {
	xsltSetLoaderFunc(NULL);
	
	slaxEnabled = 0;
	return;
    }

    if (slaxEnabled == 0) {
	/* Register EXSLT function functions so our function keywords work */
	exsltFuncRegister();

	/* Seed the random number generator */
	sranddev();
    }

    /*
     * Save the original doc loader to pass non-slax file into
     */
    if (originalXsltDocDefaultLoader == NULL)
	originalXsltDocDefaultLoader = xsltDocDefaultLoader;
    xsltSetLoaderFunc(enable ? slaxLoader : NULL);
    slaxEnabled = enable;

    slaxExtRegister();
}

/*
 * Prefer text expressions be stored in <xsl:text> elements.
 * THIS FUNCTION IS DEPRECATED.
 */
void
slaxSetTextAsElement (int enable UNUSED)
{
    return; /* deprecated */
}

/**
 * Dump a formatted version of the XSL tree to a file
 *
 * @param fd file descriptor open for output
 * @param docp document pointer
 * @param partial Should we emit partial (snippet) output?
 */
void
slaxDumpToFd (int fd, xmlDocPtr docp, int partial)
{
    xmlSaveCtxtPtr handle;

    handle = xmlSaveToFd(fd, "UTF-8", XML_SAVE_FORMAT);

    if (!partial)
	xmlSaveDoc(handle, docp);
    else {
	xmlNodePtr nodep = xmlDocGetRootElement(docp);
	if (nodep)
	    nodep = nodep->children;

	for ( ; nodep; nodep = nodep->next) {
	    if (nodep->type == XML_ELEMENT_NODE) {
		xmlSaveTree(handle, nodep);
		xmlSaveFlush(handle);
		write(fd, "\n", 1);
	    }
	}
    }

    xmlSaveClose(handle);
}

/*
 * Dump a formatted version of the XSL tree to stdout
 */
void
slaxDump (xmlDocPtr docp)
{
    slaxDumpToFd(1, docp, 0);
}

#ifdef UNIT_TEST

int
main (int argc, char **argv)
{
    xmlDocPtr docp;
    int ch;
    const char *filename = "-";

    while ((ch = getopt(argc, argv, "df:ty")) != EOF) {
        switch (ch) {
        case 'd':
	    slaxDebug = TRUE;
	    break;

        case 'f':
	    filename = optarg;
	    break;

	case 'y':
	    slaxDebug = TRUE;
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
    slaxEnable(SLAX_FORCE);

    docp = slaxLoader(filename, NULL, XSLT_PARSE_OPTIONS,
                               NULL, XSLT_LOAD_START);

    if (docp == NULL) {
	xsltTransformError(NULL, NULL, NULL,
                "xsltParseStylesheetFile : cannot parse %s\n", filename);
        return -1;
    }

    slaxDump(docp);

    return 0;
}

#endif /* UNIT_TEST */
