/*
 * Copyright (c) 2006-2013, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * slaxlexer.c -- lex slax input into lexical tokens
 *
 * This file contains the lexer for slax.
 */

#include "slaxinternals.h"
#include <libslax/slax.h>
#include "slaxparser.h"
#include <ctype.h>
#include <stdint.h>
#include <errno.h>
#include <assert.h>

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
    L_COLON, ':',
    L_CPAREN, ')',
    L_DOT, '.',
    L_EOS, ';',
    L_EQUALS, '=',
    L_GRTR, '>',
    L_LESS, '<',
    L_MINUS, '-',
    L_NOT, '!',
    L_OBRACE, '{',
    L_OBRACK, '[',
    L_OPAREN, '(',
    L_PLUS, '+',
    L_QUESTION, '?',
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
#define KMF_STMT_KW	(1<<3)	/* Fancy statement (slax keyword in xpath) */
#define KMF_JSON_KW	(1<<4)	/* JSON-only keywords */

static keyword_mapping_t keywordMap[] = {
    { K_AND, "and", KMF_XPATH_KW },
    { K_APPEND, "append", KMF_SLAX_KW },
    { K_APPLY_IMPORTS, "apply-imports", KMF_SLAX_KW },
    { K_APPLY_TEMPLATES, "apply-templates", KMF_SLAX_KW },
    { K_ATTRIBUTE, "attribute", KMF_SLAX_KW },
    { K_ATTRIBUTE_SET, "attribute-set", KMF_SLAX_KW },
    { K_CALL, "call", KMF_SLAX_KW | KMF_STMT_KW },
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
    { K_FALSE, "false", KMF_JSON_KW },
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
    { K_MAIN, "main", KMF_SLAX_KW },
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
    { K_NULL, "null", KMF_JSON_KW },
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
    { K_TRUE, "true", KMF_JSON_KW },
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

typedef struct slaxTtnameMap_s {
    int st_ttype;		/* Token number */
    const char *st_name;	/* Fancy, human-readable value */
} slaxTtnameMap_t;

slaxTtnameMap_t slaxTtnameMap[] = {
    { L_ASSIGN,			"assignment operator (':=')" },
    { L_AT,			"attribute axis ('@')" },
    { L_CBRACE,			"close brace ('}')" },
    { L_OBRACK,			"close bracket (']')" },
    { L_COLON,			"colon (':')" },
    { L_COMMA,			"comma (',')" },
    { L_DAMPER,			"logical AND operator ('&&')" },
    { L_DCOLON,			"axis operator ('::')" },
    { L_DEQUALS,		"equality operator ('==')" },
    { L_DOTDOT,			"parent axis ('..')" },
    { L_DOTDOTDOT,		"sequence operator ('...')" },
    { L_DSLASH,			"descendant operator ('//')" },
    { L_DVBAR,			"logical OR operator ('||')" },
    { L_EOS,			"semi-colon (';')" },
    { L_EQUALS,			"equal sign ('=')" },
    { L_GRTR,			"greater-than operator ('>')" },
    { L_GRTREQ,			"greater-or-equals operator ('>=')" },
    { L_LESS,			"less-than operator ('<')" },
    { L_LESSEQ,			"less-or-equals operator ('<=')" },
    { L_MINUS,			"minus sign ('-')" },
    { L_NOT,			"not sign ('!')" },
    { L_NOTEQUALS,		"not-equals sign ('!=')" },
    { L_OBRACE,			"open brace ('{')" },
    { L_OBRACK,			"open bracket ('[')" },
    { L_OPAREN,			"open parentheses ('(')" },
    { L_PLUS,			"plus sign ('+')" },
    { L_PLUSEQUALS,		"increment assign operator ('+=')" },
    { L_SLASH,			"slash ('/')" },
    { L_STAR,			"star ('*')" },
    { L_UNDERSCORE,		"concatenation operator ('_')" },
    { L_VBAR,			"union operator ('|')" },
    { K_APPEND,			"'append'" },
    { K_APPLY_IMPORTS,		"'apply-imports'" },
    { K_APPLY_TEMPLATES,	"'apply-templates'" },
    { K_ATTRIBUTE,		"'attribute'" },
    { K_ATTRIBUTE_SET,		"'attribute-set'" },
    { K_CALL,			"'call'" },
    { K_CASE_ORDER,		"'case-order'" },
    { K_CDATA_SECTION_ELEMENTS,	"'cdata-section-elements'" },
    { K_COMMENT,		"'comment'" },
    { K_COPY_NODE,		"'copy-node'" },
    { K_COPY_OF,		"'copy-of'" },
    { K_COUNT,			"'count'" },
    { K_DATA_TYPE,		"'data-type'" },
    { K_DECIMAL_FORMAT,		"'decimal-format'" },
    { K_DECIMAL_SEPARATOR,	"'decimal-separator'" },
    { K_DIE,			"'die'" },
    { K_DIGIT,			"'digit'" },
    { K_DOCTYPE_PUBLIC,		"'doctype-public'" },
    { K_DOCTYPE_SYSTEM,		"'doctype-system'" },
    { K_ELEMENT,		"'element'" },
    { K_ELSE,			"'else'" },
    { K_ENCODING,		"'encoding'" },
    { K_EXCLUDE,		"'exclude'" },
    { K_EXPR,			"'expr'" },
    { K_EXTENSION,		"'extension'" },
    { K_FALLBACK,		"'fallback'" },
    { K_FALSE,			"'false'" },
    { K_FORMAT,			"'format'" },
    { K_FOR,			"'for'" },
    { K_FOR_EACH,		"'for-each'" },
    { K_FROM,			"'from'" },
    { K_FUNCTION,		"'function'" },
    { K_GROUPING_SEPARATOR,	"'grouping-separator'" },
    { K_GROUPING_SIZE,		"'grouping-size'" },
    { K_ID,			"'id'" },
    { K_IF,			"'if'" },
    { K_IMPORT,			"'import'" },
    { K_INCLUDE,		"'include'" },
    { K_INDENT,			"'indent'" },
    { K_INFINITY,		"'infinity'" },
    { K_KEY,			"'key'" },
    { K_LANGUAGE,		"'language'" },
    { K_LETTER_VALUE,		"'letter-value'" },
    { K_LEVEL,			"'level'" },
    { K_MATCH,			"'match'" },
    { K_MAIN,			"'main'" },
    { K_MEDIA_TYPE,		"'media-type'" },
    { K_MESSAGE,		"'message'" },
    { K_MINUS_SIGN,		"'minus-sign'" },
    { K_MODE,			"'mode'" },
    { K_MVAR,			"'mvar'" },
    { K_NAN,			"'nan'" },
    { K_NODE,			"'node'" },
    { K_NS,			"'ns'" },
    { K_NS_ALIAS,		"'ns-alias'" },
    { K_NS_TEMPLATE,		"'ns-template'" },
    { K_NULL,			"'null'" },
    { K_NUMBER,			"'number'" },
    { K_OMIT_XML_DECLARATION,	"'omit-xml-declaration'" },
    { K_ORDER,			"'order'" },
    { K_OUTPUT_METHOD,		"'output-method'" },
    { K_PARAM,			"'param'" },
    { K_PATTERN_SEPARATOR,	"'pattern-separator'" },
    { K_PERCENT,		"'percent'" },
    { K_PER_MILLE,		"'per-mille'" },
    { K_PRESERVE_SPACE,		"'preserve-space'" },
    { K_PRIORITY,		"'priority'" },
    { K_PROCESSING_INSTRUCTION,	"'processing-instruction'" },
    { K_RESULT,			"'result'" },
    { K_SET,			"'set'" },
    { K_SORT,			"'sort'" },
    { K_STANDALONE,		"'standalone'" },
    { K_STRIP_SPACE,		"'strip-space'" },
    { K_TEMPLATE,		"'template'" },
    { K_TERMINATE,		"'terminate'" },
    { K_TEXT,			"'text'" },
    { K_TRACE,			"'trace'" },
    { K_TRUE,			"'true'" },
    { K_UEXPR,			"'uexpr'" },
    { K_USE_ATTRIBUTE_SETS,	"'use-attribute-set'" },
    { K_VALUE,			"'value'" },
    { K_VAR,			"'var'" },
    { K_VERSION,		"'version'" },
    { K_WHILE,			"'while'" },
    { K_WITH,			"'with'" },
    { K_ZERO_DIGIT,		"'zero-digit'" },
    { K_AND,			"'and'" },
    { K_DIV,			"'div'" },
    { K_MOD,			"'mod'" },
    { K_OR,			"'or'" },
    { L_ASTERISK,		"asterisk ('*')" },
    { L_CBRACK,			"close bracket (']')" },
    { L_CPAREN,			"close parentheses (')')" },
    { L_DOT,			"dot ('.')" },
    { T_AXIS_NAME,		"built-in axis name" },
    { T_BARE,			"bare word string" },
    { T_FUNCTION_NAME,		"function name" },
    { T_NUMBER,			"number" },
    { T_QUOTED,			"quoted string" },
    { T_VAR,			"variable name" },
    { 0, NULL }
};

/*
 * Set up the lexer's lookup tables
 */
void
slaxSetupLexer (void)
{
    int i, ttype;

    slaxSetup = 1;

    for (i = 0; singleWideData[i]; i += 2)
	singleWide[singleWideData[i + 1]] = singleWideData[i];

    for (i = 0; doubleWideData[i]; i += 3)
	doubleWide[doubleWideData[i + 1]] = i + 2;

    /* There's only one triple wide, so optimize (for now) */
    tripleWide['.'] = 1;

    for (i = 0; keywordMap[i].km_ttype; i++)
	slaxKeywordString[slaxTokenTranslate(keywordMap[i].km_ttype)]
	    = keywordMap[i].km_string;

    for (i = 0; slaxTtnameMap[i].st_ttype; i++) {
	ttype = slaxTokenTranslate(slaxTtnameMap[i].st_ttype);
	slaxTokenNameFancy[ttype] =  slaxTtnameMap[i].st_name;
    }
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
slaxDoubleWide (slax_data_t *sdp UNUSED, uint8_t ch1, uint8_t ch2)
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
slaxTripleWide (slax_data_t *sdp UNUSED, uint8_t ch1, uint8_t ch2, uint8_t ch3)
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
    int json_kwa = JSON_KEYWORDS_ALLOWED(sdp);
    keyword_mapping_t *kmp;
    int ch;

    for (kmp = keywordMap; kmp->km_string; kmp++) {
	if (slaxKeywordMatch(sdp, kmp->km_string)) {
	    if (json_kwa && (kmp->km_flags & KMF_JSON_KW))
		return kmp->km_ttype;

	    if (slax_kwa && (kmp->km_flags & KMF_SLAX_KW))
		return kmp->km_ttype;

	    if (xpath_kwa && (kmp->km_flags & KMF_XPATH_KW))
		return kmp->km_ttype;

	    if ((sdp->sd_last == L_ASSIGN || sdp->sd_last == L_EQUALS)
			&& (kmp->km_flags & KMF_STMT_KW)) {
		int look = sdp->sd_cur + strlen(kmp->km_string);

		for ( ; look < sdp->sd_len; look++) {
		    ch = sdp->sd_buf[look];

		    /*
		     * An underscore here could be either the
		     * concatenation operator or a bare word ("_foo")
		     * or even just "_" as a bare word. Compare
		     * 'var $a = call _;' and 'var $a = call _ call;'.
		     */
		    if (ch == '_') {
			ch = sdp->sd_buf[++look];
			if (slaxIsBareChar(ch))
			    return kmp->km_ttype;

			if (ch == 0 || ch == '(' || ch == ';')
			    return kmp->km_ttype;

			if (!isspace(ch))
			    break;

			for (;;) {
			    if (look > sdp->sd_len)
				return kmp->km_ttype;

			    ch = sdp->sd_buf[++look];
			    if (ch == 0 || ch == '(' || ch == ';')
				return kmp->km_ttype;

			    if (isspace(ch))
				continue;

			    if (slaxIsBareChar(ch))
				break;
			}
			break;

		    } else if (slaxIsBareChar(ch))
			return kmp->km_ttype;

		    if (!isspace(ch))
			break;
		}
	    }

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

int
slaxParseIsSlax (slax_data_t *sdp)
{
    return (sdp->sd_parse == M_PARSE_FULL
	    || sdp->sd_parse == M_PARSE_SLAX
	    || sdp->sd_parse == M_PARSE_PARTIAL);
}

int
slaxParseIsXpath (slax_data_t *sdp)
{
    return (sdp->sd_parse == M_PARSE_XPATH);
}

/*
 * Fill the input buffer, shifting data forward if we need the room
 * and dynamically reallocating the buffer if we still need room.
 */
int
slaxGetInput (slax_data_t *sdp, int final)
{
    int first_read = (sdp->sd_buf == NULL);

    /*
     * If we're parsing XPath expressions, we alreay have the
     * complete string, so further reads should fail
     */
    if (sdp->sd_parse == M_PARSE_XPATH || sdp->sd_parse == M_PARSE_SLAX
		|| sdp->sd_file == NULL)
	return TRUE;

    for (;;) {
	if (sdp->sd_len + SD_BUF_FUDGE > sdp->sd_size) {
	    if (sdp->sd_start > SD_BUF_FUDGE) {
		/*
		 * Shift the data forward to give us some room at the end
		 */
		memmove(sdp->sd_buf, sdp->sd_buf + sdp->sd_start,
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
	    if (slaxLogIsEnabled)
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

#if 0
	if (sdp->sd_buf[sdp->sd_len - 1] == '\n') {
	    sdp->sd_line += 1;
	    if (final && sdp->sd_ctxt->input)
		sdp->sd_ctxt->input->line = sdp->sd_line;
	}
#endif

	if (first_read) {
	    /*
	     * UTF-8 allows (but doesn't recommend) UTF-8 files
	     * to start with a BOM, which is 0xefbbbf.  Skip over it.
	     */
	    if ((unsigned char) sdp->sd_buf[0] == 0xef
		    && (unsigned char) sdp->sd_buf[1] == 0xbb
		    && (unsigned char) sdp->sd_buf[2] == 0xbf)
		sdp->sd_cur += 3;

	    if (sdp->sd_buf[sdp->sd_cur] == '#'
		    && sdp->sd_buf[sdp->sd_cur + 1] == '!') {
		/*
		 * If we hit a line on our first read that starts with "#!",
		 * then we skip the line.
		 */
		sdp->sd_len = 0;
		sdp->sd_line += 1;
	    }

	    first_read = FALSE;
	    continue;
	}

	/* We don't want to get half a keyword */
	if (!final || slaxIsFinalChar(sdp->sd_buf[sdp->sd_len - 1]))
	    return FALSE;
    }
}

/*
 * Move the current point by one character, getting more data if needed.
 */
static int
slaxMoveCur (slax_data_t *sdp)
{
    slaxLog("slax: move:- %u/%u/%u", sdp->sd_start, sdp->sd_cur, sdp->sd_len);

    int moved;
    if (sdp->sd_cur < sdp->sd_len) {
	sdp->sd_cur += 1;
	moved = TRUE;
    } else moved = FALSE;

    if (sdp->sd_cur == sdp->sd_len) {
       if (slaxGetInput(sdp, 0)) {
           slaxLog("slax: move:! %u/%u/%u",
                   sdp->sd_start, sdp->sd_cur, sdp->sd_len);
	   if (moved)
	       sdp->sd_cur -= 1;
           return -1;
       }
    }

    slaxLog("slax: move:+ %u/%u/%u", sdp->sd_start, sdp->sd_cur, sdp->sd_len);
    return 0;
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

    slaxLog("addchild: '%s' %d", (const char *) cur->name, cur->line);

    return xmlAddChild(parent, cur);
}

void
slaxAddInsert (slax_data_t *sdp, xmlNodePtr nodep)
{
    xmlNodePtr *nextp = &sdp->sd_insert;

    while (*nextp != NULL) {
	nextp = &(*nextp)->next;
    }

    nodep->next = nodep->prev = nodep->parent = NULL;
    *nextp = nodep;
}

xmlNodePtr
slaxAddChild (slax_data_t *sdp, xmlNodePtr parent, xmlNodePtr nodep)
{
    xmlNodePtr res;

    if (parent == NULL)
	parent = sdp->sd_ctxt->node;

    res = slaxAddChildLineNo(sdp->sd_ctxt, parent, nodep);

    if (sdp->sd_insert) {
	xmlNodePtr cur, next;

	cur = sdp->sd_insert;
	sdp->sd_insert = NULL;

	for ( ; cur; cur = next) {
	    next = cur->next;
	    cur->next = cur->prev = cur->parent = NULL;
	    xmlAddPrevSibling(nodep, cur);
	}
    }

    return res;
}

static int
slaxIsCommentStart (slax_data_t *sdp, const char *buf)
{
    if (sdp->sd_flags & SDF_SLSH_COMMENTS) {
	if (buf[0] == '/' && buf[1] == '/') {
	    sdp->sd_flags |= SDF_SLSH_OPEN;
	    return TRUE;
	}
    }

    return (buf[0] == '/' && buf[1] == '*');
}

static int
slaxIsCommentEnd (slax_data_t *sdp, const char *buf)
{
    if (sdp->sd_flags & SDF_SLSH_OPEN) {
	if (buf[0] == '\n') {
	    sdp->sd_flags &= ~SDF_SLSH_OPEN;
	    return TRUE;
	}
    }

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
    unsigned flags = sdp->sd_flags;

    for ( ; ; sdp->sd_cur++) {
	if (sdp->sd_cur >= sdp->sd_len) {
	    if (slaxGetInput(sdp, 1))
		return TRUE;
	}

	if (slaxIsCommentEnd(sdp, sdp->sd_buf + sdp->sd_cur)) {
	    if (flags & SDF_SLSH_OPEN)
		sdp->sd_cur += 1;
	    else
		sdp->sd_cur += COMMENT_MARKER_SIZE;	/* Move past "\*\/" */
	    return FALSE;
	}

	/* Keep the line number accurate inside comments */
	if (sdp->sd_buf[sdp->sd_cur] == '\n')
	    sdp->sd_line += 1;
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
 * An XML comment cannot contain two adjacent dashes, which is
 * sad.  And since libxml2 doesn't perform this escaping, we are
 * left to do it ourselves.  We turn "--" into "-&#2d;"
 */
static xmlChar *
slaxCommentMakeValue (xmlChar *input)
{
    static const char dash[] = "&#2d;"; /* XML character entity for dash */
    xmlChar *cp, *out, *res;
    int len = xmlStrlen(input);
    int hit = FALSE;

    for (cp = input; *cp; cp++)
	if (*cp == '-') {
	    len += sizeof(dash) - 1; /* Worst case */
	    hit = TRUE;
	}

    if (!hit)
	return NULL;

    res = out = xmlMalloc(len + 1);
    if (out == NULL)
	return NULL;

    for (cp = input; *cp; cp++) {

	if (*cp == '-') {
	    if (out > res && out[-1] == '-') {
		memcpy(out, dash, sizeof(dash) - 1);
		out += sizeof(dash) - 1;
	    } else
		*out++ = *cp;
		
	} else
	    *out++ = *cp;
    }
    *out = '\0';

    return (xmlChar *) res;
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
    uint8_t ch1, ch2, ch3;
    int look, rc;

    for (;;) {
	sdp->sd_start = sdp->sd_cur;

	if (sdp->sd_cur == sdp->sd_len)
	    if (slaxGetInput(sdp, 1))
		return -1;

	while (sdp->sd_cur < sdp->sd_len
	       && isspace((int) sdp->sd_buf[sdp->sd_cur])) {
	    if (sdp->sd_buf[sdp->sd_cur] == '\n') {
		sdp->sd_line += 1;
		if (sdp->sd_ctxt->input)
		    sdp->sd_ctxt->input->line = sdp->sd_line;
	    }

	    sdp->sd_cur += 1;
	}

	if (sdp->sd_cur != sdp->sd_len) {
	    /*
	     * See if we're at the start of a comment, but only
	     * if we're not in the middle of an XPath expression.
	     * We don't want "$foo / * [. = 2]" (without the spaces)
	     * to trigger a comment start.
	     */
	    if (SLAX_KEYWORDS_ALLOWED(sdp)
		&& slaxIsCommentStart(sdp, sdp->sd_buf + sdp->sd_cur)) {

		sdp->sd_start = sdp->sd_cur;
		if (slaxDrainComment(sdp)) {
		    sdp->sd_flags |= SDF_OPEN_COMMENT;
		    return -1;
		}

		/*
		 * So now we've got the comment.  We need to turn it
		 * into an XML comment and add it into the xsl tree.
		 */
		{
		    int start = sdp->sd_start + COMMENT_MARKER_SIZE;
		    int end = sdp->sd_cur - COMMENT_MARKER_SIZE;
		    int len = end - start;
		    xmlChar *buf = alloca(len + 1), *contents;
		    xmlNodePtr nodep;

		    while (isspace((int) sdp->sd_buf[start])) {
			if (sdp->sd_buf[start] == '\n')
			    break;
			start += 1;
			len -= 1;
		    }
		    while (end > start
			   && isspace((int) sdp->sd_buf[end - 1])) {
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

			contents = slaxCommentMakeValue(buf);
			nodep = xmlNewComment(contents ?: buf);
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
			xmlFreeAndEasy(contents);
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

    /*
     * The rules for YANG are fairly simple.  But if we're
     * looking for a string argument, it's pretty easy.
     */
    if ((sdp->sd_flags & SDF_STRING)
		&& ch1 != '{' && ch1 != '}' && ch1 != ';' && ch1 != '+') {
	if (ch1 == '\'' || ch1 == '"') {
	    /*
	     * Found a quoted string.  Scan for the end.  We may
	     * need to read some more, if the string is long.
	     */
	    if (slaxMoveCur(sdp))
		return -1;

	    for (;;) {
		if (sdp->sd_cur == sdp->sd_len)
		    if (slaxGetInput(sdp, 0))
			return -1;

		if ((uint8_t) sdp->sd_buf[sdp->sd_cur] == ch1)
		    break;

		int bump = (sdp->sd_buf[sdp->sd_cur] == '\\') ? 1 : 0;

		if (slaxMoveCur(sdp))
		    return -1;

		if (bump && !slaxParseIsXpath(sdp)
			&& sdp->sd_cur < sdp->sd_len)
		    sdp->sd_cur += bump;
	    }

	    if (sdp->sd_cur < sdp->sd_len)
		sdp->sd_cur += 1;	/* Move past the end quote */

	    return T_QUOTED;
	}
	
	for (sdp->sd_cur++; sdp->sd_cur < sdp->sd_len; sdp->sd_cur++) {
	    int ch = sdp->sd_buf[sdp->sd_cur];
	    if (isspace((int) ch) || ch == ';' || ch == '{' || ch == '}')
		break;
	}
	return (sdp->sd_buf[sdp->sd_start] == '$') ? T_VAR : T_BARE;
    }

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
	    int lit1 = singleWide[ch1];
	    sdp->sd_cur += 1;

	    if (lit1 == L_STAR) {
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

            if (ch1 == '.' && isdigit((int) ch2)) {
                /* continue */
            } else if (lit1 == L_UNDERSCORE) {
                /*
                 * Underscore is a valid first character for an element
                 * name, which is troubling, since it's also the concatenation
                 * operator in SLAX.  We look ahead to see if the next
                 * character is a valid character before making our
                 * decision.
                 */
		if (!slaxIsBareChar(ch2))
		    return lit1;
	    } else if (sdp->sd_parse == M_JSON
		       && (lit1 == L_PLUS || lit1 == L_MINUS)) {
		static const char digits[] = "0123456789.+-eE";
		unsigned char ch;

		rc = lit1;
		for ( ; sdp->sd_cur < sdp->sd_len; sdp->sd_cur++) {
		    ch = sdp->sd_buf[sdp->sd_cur];
		    if (strchr(digits, (int) ch) == NULL)
			return rc;
		    rc = T_NUMBER;
		}

		return rc;

	    } else {
		return lit1;
	    }
	}

	if (ch1 == '\'' || ch1 == '"') {
	    /*
	     * Found a quoted string.  Scan for the end.  We may
	     * need to read some more, if the string is long.
	     */
	    if (slaxMoveCur(sdp)) /* Move past the first quote */
		return -1;

	    for (;;) {
		if (sdp->sd_cur == sdp->sd_len)
		    if (slaxGetInput(sdp, 0))
			return -1;

		if ((uint8_t) sdp->sd_buf[sdp->sd_cur] == ch1)
		    break;

		int bump = (sdp->sd_buf[sdp->sd_cur] == '\\') ? 1 : 0;

		if (slaxMoveCur(sdp))
		    return -1;

		if (bump && !slaxParseIsXpath(sdp)
			&& sdp->sd_cur < sdp->sd_len)
		    sdp->sd_cur += bump;
	    }

	    if (sdp->sd_cur < sdp->sd_len)
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
	    sdp->sd_cur += strlen(slaxKeywordString[slaxTokenTranslate(rc)]);
	    return rc;
	}

	if (isdigit(ch1) || (ch1 == '.' && isdigit(ch2))) {
	    int seen_e = FALSE;

	    for ( ; sdp->sd_cur < sdp->sd_len; sdp->sd_cur++) {
		int ch4 =  sdp->sd_buf[sdp->sd_cur];
		if (isdigit(ch4))
		    continue;
		if (ch4 == '.')
		    continue;
		if (ch4 == 'e' || ch4 == 'E') {
		    seen_e = TRUE;
		    continue;
		}
		if (seen_e && (ch4 == '+' || ch4 == '-'))
		    continue;
		break;		/* Otherwise, we're done */
	    }
	    return T_NUMBER;
	}
    }

    /*
     * The rules for JSON are a bit simpler.  T_BARE can't contain
     * colons and we don't need the fancy exceptions below.
     */
    if (sdp->sd_parse == M_JSON) {
	for ( ; sdp->sd_cur < sdp->sd_len; sdp->sd_cur++) {
	    int ch = sdp->sd_buf[sdp->sd_cur];
	    if (!isalnum((int) ch) && ch != '_')
		break;
	}
	return T_BARE;
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

    /* Add the record flags to the current set */
    sdp->sd_flags |= sdp->sd_flags_next;
    sdp->sd_flags_next = 0;

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
     * It's a hack, but it's a standard-specified hack: We need to see
     * if this is a function name (T_FUNCTION_NAME) or an NCName
     * (q_name) (T_BARE).  So we look ahead for a '('.  If we find
     * one, it's a function; if not it's an T_BARE;
     */
    if (rc == T_BARE && sdp->sd_parse != M_JSON) {
	for (look = sdp->sd_cur; look < sdp->sd_len; look++) {
	    unsigned char ch = sdp->sd_buf[look];
	    if (ch == '(') {
		rc = T_FUNCTION_NAME;
		break;
	    }

	    if (!isspace((int) ch))
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
	if (slaxLogIsEnabled)
	    slaxError("%s:%d: slax: lex: zero length token: %d/%s",
		      sdp->sd_filename, sdp->sd_line,
		      rc, slaxTokenName(rc));
	rc = M_ERROR;
	/*
	 * We're attempting to return a reasonable token type, but
	 * with a zero length token.  Something it very busted with
	 * the input.  We can't just sit here, but, well, there are
	 * no good options.  We're going to move the current point
	 * forward in the hope that we'll see good input eventually.
	 */
	if (sdp->sd_cur < sdp->sd_len)
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

    if (slaxLogIsEnabled && ssp)
	slaxLog("slax: lex: (%s) %p '%.*s' -> %d/%s %p",
		  SLAX_KEYWORDS_ALLOWED(sdp) ? "keywords" : "nokeywords", ssp,
		  sdp->sd_cur - sdp->sd_start, sdp->sd_buf + sdp->sd_start,
		  rc, (rc > 0) ? slaxTokenName(rc) : "",
		  ssp ? ssp->ss_token : NULL);

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
    case K_MAIN:
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
	sdp->sd_flags_next |= SDF_NO_SLAX_KEYWORDS;
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
	sdp->sd_flags_next |= SDF_NO_KEYWORDS;
	break;
    }

    return rc;
}

static xmlNodePtr
slaxFindOpenNode (slax_data_t *sdp)
{
    return sdp->sd_ctxt->node;
}

/*
 * Return a better class of error message
 * @returns freshly allocated string containing error message
 */
static char *
slaxSyntaxError (slax_data_t *sdp, const char *token, int yystate, int yychar,
		 slax_string_t **vstack UNUSED, slax_string_t **vtop UNUSED)
{
    char buf[BUFSIZ], *cp = buf, *ep = buf + sizeof(buf);
    xmlNodePtr nodep;

    /*
     * If yystate is 1, then we're in our initial state and have
     * not seen any valid input.  We handle this state specifically
     * to give better error messages.
     */
    if (yystate == 1) {
	if (yychar == -1)
	    return xmlStrdup2("unexpected end-of-file found (empty input)");

	if (yychar == L_LESS)
	    return xmlStrdup2("unexpected '<'; file may be XML/XSLT");

	SNPRINTF(cp, ep, "missing 'version' statement");
	if (token)
	    SNPRINTF(cp, ep, "; %s is not legal", token);

    } else if (yychar == -1) {
	if (sdp->sd_flags & SDF_OPEN_COMMENT)
	    return xmlStrdup2("unexpected end-of-file due to open comment");

	SNPRINTF(cp, ep, "unexpected end-of-file");

	nodep = slaxFindOpenNode(sdp);
	if (nodep) {
	    int lineno = xmlGetLineNo(nodep);

	    if (lineno > 0) {
		if (nodep->ns && nodep->ns->href
			&& streq((const char *)nodep->ns->href, XSL_URI)) {
		    SNPRINTF(cp, ep,
			     "; unterminated statement (<xsl:%s>) on line %d",
			     nodep->name, lineno);
		} else {
		    SNPRINTF(cp, ep, "; open element (<%s>) on line %d",
			     nodep->name, lineno);
		}
	    }
	}

    } else {
	char *msg = slaxExpectingError(token, yystate, yychar);
	if (msg)
	    return msg;

	SNPRINTF(cp, ep, "unexpected input");
	if (token)
	    SNPRINTF(cp, ep, ": %s", token);
    }

    return xmlStrdup2(buf);
}

/**
 * Callback from bison when an error is detected.
 *
 * @param sdp main slax data structure
 * @param str error message
 * @param value stack entry from bison's lexical stack
 * @return zero
 */
int
slaxYyerror (slax_data_t *sdp, const char *str, slax_string_t *value,
	     int yystate, slax_string_t **vstack, slax_string_t **vtop)
{
    static const char leader[] = "syntax error, unexpected";
    static const char leader2[] = "error recovery ignores input";
    const char *token = value ? value->ss_token : NULL;
    char buf[BUFSIZ];

    if (strncmp(str, leader2, sizeof(leader2) - 1) != 0)
	sdp->sd_errors += 1;

    if (token == NULL)
	token = slaxTokenNameFancy[slaxTokenTranslate(sdp->sd_last)];
    else {
	int len = strlen(token);
	char *cp = alloca(len + 3); /* Two quotes plus NUL */

	cp[0] = '\'';
	memcpy(cp + 1, token, len);
	cp[len + 1] = '\'';
	cp[len + 2] = '\0';

	token = cp;
    }

    /*
     * See if there's a multi-line string that could be the source
     * of the problem.
     */
    buf[0] = '\0';
    if (vtop && *vtop) {
	slax_string_t *ssp = *vtop;

	if (ssp->ss_ttype == T_QUOTED) {
	    char *np = strchr(ssp->ss_token, '\n');

	    if (np != NULL) {
		int count = 1;
		char *xp;

		for (xp = np + 1; *xp; xp++)
		    if (*xp == '\n')
			count += 1;

		snprintf(buf, sizeof(buf),
			 "\n%s:%d:   could be related to unterminated string "
			 "on line %d (\"%.*s\\n...\")",
			 sdp->sd_filename, sdp->sd_line, sdp->sd_line - count,
			 (int) (np - ssp->ss_token), ssp->ss_token);
	    }
	}
    }

    /*
     * Two possibilities: generic "syntax error" or some
     * specific error.  If the message has a generic
     * prefix, use our logic instead.  This avoids tossing
     * bison token names (K_VERSION) at the user.
     */
    if (strncmp(str, leader, sizeof(leader) - 1) == 0) {
	char *msg = slaxSyntaxError(sdp, token, yystate, sdp->sd_last,
				    vstack, vtop);

	if (msg) {
	    slaxError("%s:%d: %s%s\n", sdp->sd_filename, sdp->sd_line,
		      msg, buf);
	    xmlFree(msg);
	    return 0;
	}
    }

    slaxError("%s:%d: %s%s%s%s%s\n",
	      sdp->sd_filename, sdp->sd_line, str,
	      token ? " before " : "", token, token ? ": " : "", buf);

    return 0;
}
