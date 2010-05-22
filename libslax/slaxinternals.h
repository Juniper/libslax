/*
 * $Id: slaxinternals.h,v 1.3 2008/05/21 02:06:12 phil Exp $
 *
 * Copyright (c) 2006-2010, Juniper Networks, Inc.
 * All rights reserved.
 * See ./Copyright for the status of this software
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/param.h>

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/xmlsave.h>

#include <libxslt/documents.h>

#define XSL_VERSION "1.0"

#define XSL_PREFIX  "xsl"
#define XSL_NS "http://www.w3.org/1999/XSL/Transform"

#define XML_PREFIX  "xml"
#define XML_NS "http://www.w3.org/XML/1998/namespace"

/*
 * This is a should-not-occur error string if you ever see it, we've
 * done something wrong (but it beats a core dump).
 */
#define UNKNOWN_EXPR "<<<<slax error>>>>"

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

extern int slaxDebug;			/* Global debug switch */
extern const char *keywordString[];

/* Attribute names */
#define ATT_ELEMENTS	"elements"
#define ATT_DISABLE_OUTPUT_ESCAPING "disable-output-escaping"
#define ATT_EXCLUDE_RESULT_PREFIXES "exclude-result-prefixes"
#define ATT_EXTENSION_ELEMENT_PREFIXES "extension-element-prefixes"
#define ATT_HREF	"href"
#define ATT_NAME	"name"
#define ATT_MATCH	"match"
#define ATT_MODE	"mode"
#define ATT_PRIORITY	"priority"
#define ATT_SELECT	"select"
#define ATT_TEST	"test"
#define ATT_VERSION	"version" 

/* Element names */
#define ELT_APPLY_TEMPLATES "apply-templates"
#define ELT_ATTRIBUTE	"attribute"
#define ELT_CALL_TEMPLATE "call-template"
#define ELT_CHOOSE	"choose"
#define ELT_COMMENT	"comment"
#define ELT_COPY_OF	"copy-of"
#define ELT_FOR_EACH	"for-each"
#define ELT_IF		"if"
#define ELT_OTHERWISE	"otherwise"
#define ELT_PARAM	"param"
#define ELT_STYLESHEET	"stylesheet"
#define ELT_TEMPLATE	"template"
#define ELT_TEXT	"text"
#define ELT_TRANSFORM	"transform"
#define ELT_VALUE_OF	"value-of"
#define ELT_VARIABLE	"variable"
#define ELT_WHEN	"when"
#define ELT_WITH_PARAM	"with-param"

/*
 * We build strings as we go using this structure.
 */
typedef struct slax_string_s {
    struct slax_string_s *ss_next; /* Linked list of strings */
    int ss_ttype;		/* Token type */
    char ss_token[1];		/* Value of this token */
} slax_string_t;

#define YYSTYPE slax_string_t *	/* This is our bison stack frame */

/*
 * Our main data structure
 */
typedef struct slax_data_s {
    int sd_errors;		/* Number of errors seen */
    FILE *sd_file;		/* File to read from */
    unsigned sd_flags;		/* Flags */
    int sd_parse;		/* Parsing mode (M_PARSE_*) */
    int sd_ttype;		/* Token type returned on next lexer call */
    int sd_last;		/* Last token type returned */
    char sd_filename[MAXPATHLEN]; /* Path of current file */
    int sd_line;		/* Line number */
    int sd_col;			/* Column number */
    int sd_start;		/* Next valid byte in sd_buf */
    int sd_cur;			/* Current byte in sd_buf */
    int sd_len;			/* Last valid byte in sd_buf (+1) */
    int sd_size;		/* Size of sd_buf */
    char *sd_buf;		/* Input buffer */
    xmlParserCtxtPtr sd_ctxt;	/* XML Parser context */
    xmlDocPtr sd_docp;		/* The XML document we are building */
    xmlNsPtr sd_xsl_ns;		/* Pointer to the XSL namespace */
    slax_string_t *sd_xpath;	/* Parsed XPath expression */
} slax_data_t;

/* Flags for sd_flags */
#define SDF_EOF			(1<<0)	/* EOF seen */
#define SDF_NO_SLAX_KEYWORDS	(1<<1)	/* Do not allow slax keywords */
#define SDF_NO_XPATH_KEYWORDS	(1<<2)	/* Do not allow xpath keywords */

#define SDF_NO_KEYWORDS (SDF_NO_SLAX_KEYWORDS | SDF_NO_XPATH_KEYWORDS)

/*
 * Two kinds of keywords: slax and xpath.  Slax keywords
 * are not valid inside xpath expressions, but xpath ones
 * are.  This leaves some obvious problems, but...
 */
#define KEYWORDS_OFF() slax_data->sd_flags |= SDF_NO_SLAX_KEYWORDS
#define KEYWORDS_ON()  slax_data->sd_flags &= ~SDF_NO_KEYWORDS
#define ALL_KEYWORDS_OFF() slax_data->sd_flags |= SDF_NO_KEYWORDS
#define ALL_KEYWORDS_ON()  slax_data->sd_flags &= ~SDF_NO_KEYWORDS

#define XPATH_KEYWORDS_ON() \
    slax_data->sd_flags = (SDF_NO_SLAX_KEYWORDS \
		| (slax_data->sd_flags & ~SDF_NO_XPATH_KEYWORDS))

#define SLAX_KEYWORDS_ALLOWED(_x) (!((_x)->sd_flags & SDF_NO_SLAX_KEYWORDS))
#define XPATH_KEYWORDS_ALLOWED(_x) (!((_x)->sd_flags & SDF_NO_XPATH_KEYWORDS))

/*
 * We redefine yylex to give our parser a specific name, avoiding
 * conflicts with other yacc/bison based parsers.
 */
#define yylex(sp, v) slaxYylex(slax_data, sp)
int
slaxYylex (slax_data_t *sdp, YYSTYPE *yylvalp);

#define yyerror(str) slaxYyerror(slax_data, str, yylval)
int
slaxYyerror (slax_data_t *sdp, const char *str, YYSTYPE);

/*
 * The bison-based parser's main function
 */
int
slaxParse (slax_data_t *);

/*
 * Return a human-readable name for a given token type
 */
const char *
slaxTokenName (int ttype);

/*
 * Expose YYTRANSLATE outside the bison file
 */
int
slaxTokenTranslate (int ttype);

/*
 * Check the version string.  The only supported version is "1.0".
 */
void
slaxVersionMatch (const char *major, const char *minor);

/*
 * Should the character be stripped of surrounding whitespace?
 */
int
slaxNoSpace (int ch);

/*
 * Add a namespace to the node on the top of the stack
 */
void
slaxNsAdd (slax_data_t *, const char *prefix, const char *uri);

/*
 * Add an XSL element to the node at the top of the context stack
 */
xmlNodePtr
slaxElementAdd (slax_data_t *, const char *tag,
		    const char *attrib, const char *value);

/*
 * Add an XSL element to the top of the context stack
 */
xmlNodePtr
slaxElementPush (slax_data_t *sdp, const char *tag,
			   const char *attrib, const char *value);

/*
 * Pop an XML element off the context stack
 */
void
slaxElementPop (slax_data_t *sdp);

/*
 * Simple trace function that tosses messages to stderr if slaxDebug
 * has been set to non-zero.
 */
void
slaxTrace (const char *fmt, ...);

/*
 * Backup the stack up 'til the given node is seen
 */
slax_string_t *
slaxStackClear (slax_data_t *sdp, slax_string_t **sspp,
				    slax_string_t **top);

/*
 * Add a simple value attribute.
 */
void slaxAttribAdd (slax_data_t *sdp, const char *name, slax_string_t *value);

/*
 * Add a value to an attribute on an XML element.  The value consists of
 * one or more items, each of which can be a string, a variable reference,
 * or an xpath expression.  If the value (any items) does not contain an
 * xpath expression, we use the normal attribute substitution to achieve
 * this ({$foo}).  Otherwise, we use the xsl:attribute element to construct
 * the attribute.
 */
void slaxAttribAddValue (slax_data_t *sdp, const char *name,
			 slax_string_t *value);

/*
 * Add a simple value attribute as straight string
 */
void slaxAttribAddString (slax_data_t *sdp, const char *name, slax_string_t *);

/*
 * Add an XPath expression as a statement.  If this is a string,
 * we place it inside an <xsl:text> element.  Otherwise we
 * put the value inside an <xsl:value-of>'s select attribute.
 */
void slaxElementXPath (slax_data_t *sdp, slax_string_t *ssp, int text_as_elt);

/*
 * Add an element to the top of the context stack
 */
void slaxElementOpen (slax_data_t *sdp, const char *tag);

/*
 * Pop an element off the context stack
 */
void slaxElementClose (slax_data_t *sdp);

/*
 * Extend the existing value for an attribute, appending the given value.
 */
void slaxAttribExtend (slax_data_t *sdp, const char *attrib, const char *val);

/*
 * Check to see if an "if" statement had no "else".  If so,
 * we can rewrite it from an "xsl:choose" into an "xsl:if".
 */
void slaxCheckIf (slax_data_t *sdp, xmlNodePtr choosep);

/*
 * If we know we're about to assign a result tree fragment (RTF)
 * to a variable, punt and do The Right Thing.
 */
void slaxAvoidRtf (slax_data_t *sdp);

/* -------------------------------------------------------------------
 * slax_string_t string handling functions
 */

/* Flags for slaxString functions */
#define SSF_QUOTES	(1<<0)	/* Wrap the string in double quotes */
#define SSF_BRACES	(1<<1)	/* Escape braces (in attributes) */

/*
 * Concatenate a variable number of strings, returning the results.
 */
slax_string_t *
slaxStringConcat (slax_data_t *, int, slax_string_t **);

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
slaxStringAsValue (slax_string_t *value, unsigned flags);

/*
 * Calculate the length of the string consisting of the concatenation
 * of all the string segments hung off "start".
 */
int
slaxStringLength (slax_string_t *start, unsigned flags);

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
		   const char *buf, size_t bufsiz, int ttype);

/*
 * Simple, inline convenience functions
 */

/*
 * Free a chunk of memory
 */
static inline void
xmlFreeAndEasy (void *ptr)
{
    if (ptr)
	xmlFree(ptr);
}
