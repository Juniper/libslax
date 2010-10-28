/*
 * $Id: slaxinternals.h,v 1.3 2008/05/21 02:06:12 phil Exp $
 *
 * Copyright (c) 2006-2010, Juniper Networks, Inc.
 * All rights reserved.
 * See ../Copyright for the status of this software
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

#include "util.h"
#include "xmlsoft.h"

#define XSL_VERSION "1.0"

#define XSL_PREFIX  "xsl"
#define XSL_NS "http://www.w3.org/1999/XSL/Transform"

#define XML_PREFIX  "xml"
#define XML_NS "http://www.w3.org/XML/1998/namespace"

#define EXT_URI "http://xmlsoft.org/XSLT/namespace"
#define EXT_PREFIX "slax-ext"

#define FUNC_URI EXSLT_FUNCTIONS_NAMESPACE
#define FUNC_PREFIX "slax-func"

#define TRACE_URI "http://code.google.com/p/libslax/trace"
#define TRACE_PREFIX "slax-trace"

#define SLAX_URI "http://code.google.com/p/libslax/slax"
#define SLAX_PREFIX "slax"

/*
 * This is a should-not-occur error string if you ever see it, we've
 * done something wrong (but it beats a core dump).
 */
#define UNKNOWN_EXPR "<<<<slax error>>>>"

extern int slaxDebug;			/* Global debug switch */
extern const char *keywordString[];

/* Stub to handle xmlChar strings in "?:" expressions */
extern const xmlChar null[];

/* Attribute names */
#define ATT_CDATA_SECTION_ELEMENTS "cdata-section-elements"
#define ATT_COUNT	"count"
#define ATT_DISABLE_OUTPUT_ESCAPING "disable-output-escaping"
#define ATT_ELEMENTS	"elements"
#define ATT_DISABLE_OUTPUT_ESCAPING "disable-output-escaping"
#define ATT_EXCLUDE_RESULT_PREFIXES "exclude-result-prefixes"
#define ATT_EXTENSION_ELEMENT_PREFIXES "extension-element-prefixes"
#define ATT_FROM	"from"
#define ATT_GROUPING_SEPARATOR "grouping-separator"
#define ATT_GROUPING_SIZE "grouping-size"
#define ATT_HREF	"href"
#define ATT_LANG	"lang"
#define ATT_NAME	"name"
#define ATT_NAMESPACE	"namespace"
#define ATT_MATCH	"match"
#define ATT_METHOD	"method"
#define ATT_MODE	"mode"
#define ATT_MUTABLE	"mutable"
#define ATT_NAN		"NaN"	/* Yes, it's mixed case */
#define ATT_PRIORITY	"priority"
#define ATT_RESULT_PREFIX "result-prefix"
#define ATT_SELECT	"select"
#define ATT_STYLESHEET_PREFIX "stylesheet-prefix"
#define ATT_TERMINATE	"terminate"
#define ATT_TEST	"test"
#define ATT_USE		"use"
#define ATT_USE_ATTRIBUTE_SETS	"use-attribute-sets"
#define ATT_VALUE	"value"
#define ATT_VERSION	"version" 

/* Element names */
#define ELT_APPEND_TO_VARIABLE "append-to-variable"
#define ELT_APPLY_IMPORTS "apply-imports"
#define ELT_APPLY_TEMPLATES "apply-templates"
#define ELT_ATTRIBUTE	"attribute"
#define ELT_ATTRIBUTE_SET "attribute-set"
#define ELT_CALL_TEMPLATE "call-template"
#define ELT_CHOOSE	"choose"
#define ELT_COMMENT	"comment"
#define ELT_COPY_OF	"copy-of"
#define ELT_COPY	"copy"
#define ELT_ELEMENT	"element"
#define ELT_FOR_EACH	"for-each"
#define ELT_FUNCTION	"function"
#define ELT_IF		"if"
#define ELT_MESSAGE	"message"
#define ELT_NAME	"name"
#define ELT_NAMESPACE_ALIAS "namespace-alias"
#define ELT_OTHERWISE	"otherwise"
#define ELT_OUTPUT	"output"
#define ELT_PARAM	"param"
#define ELT_RESULT	"result"
#define ELT_SET_VARIABLE "set-variable"
#define ELT_SORT	"sort"
#define ELT_STYLESHEET	"stylesheet"
#define ELT_TEMPLATE	"template"
#define ELT_TEXT	"text"
#define ELT_TRACE	"trace"
#define ELT_TRANSFORM	"transform"
#define ELT_USE_ATTRIBUTE_SETS	"use-attribute-sets"
#define ELT_VALUE_OF	"value-of"
#define ELT_VARIABLE	"variable"
#define ELT_WHEN	"when"
#define ELT_WITH_PARAM	"with-param"

#define FOR_VARIABLE_PREFIX "$slax-dot-"
#define FUNC_BUILD_SEQUENCE "build-sequence"

/*
 * We build strings as we go using this structure.
 */
typedef struct slax_string_s {
    struct slax_string_s *ss_next; /* Linked list of strings in XPath expr */
    struct slax_string_s *ss_concat; /* Linked list of strings with "_" op */
    int ss_ttype;		   /* Token type */
    int ss_flags;		   /* Flags */
    char ss_token[1];		   /* Value of this token */
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
    slax_string_t *sd_ns;	/* Namespace stash */
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
#define SLAX_KEYWORDS_OFF() slax_data->sd_flags |= SDF_NO_SLAX_KEYWORDS
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
slaxVersionMatch (slax_data_t *sdp, const char *major, const char *minor);

/*
 * Add a namespace to the node on the top of the stack
 */
void
slaxNsAdd (slax_data_t *, const char *prefix, const char *uri);

/*
 * Create namespace alias between the two prefixes given.
 */
void
slaxNamespaceAlias (slax_data_t *sdp, slax_string_t *, slax_string_t *);

/*
 * Add an XSL element to the node at the top of the context stack
 */
xmlNodePtr
slaxElementAdd (slax_data_t *, const char *tag,
		    const char *attrib, const char *value);
xmlNodePtr
slaxElementAddString (slax_data_t *sdp, const char *tag,
		      const char *attrib, slax_string_t *value);

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
 * Relocate the most-recent "sort" node, if the parent was a "for" loop.
 */
void
slaxRelocateSort (slax_data_t *sdp);

/*
 * Add an xsl:comment node
 */
xmlNodePtr
slaxCommentAdd (slax_data_t *sdp, slax_string_t *value);

/*
 * Simple trace function that tosses messages to stderr if slaxDebug
 * has been set to non-zero.
 */
void
slaxLog (const char *fmt, ...);

/*
 * Backup the stack up 'til the given node is seen
 */
slax_string_t *
slaxStackClear (slax_data_t *sdp, slax_string_t **sspp,
				    slax_string_t **top);

/*
 * Backup the stack up 'til the given node is seen; return the given node.
 */
slax_string_t *
slaxStackClear2 (slax_data_t *sdp, slax_string_t **sspp,
		 slax_string_t **top, slax_string_t **retp);

/*
 * Give an error if the axis name is not valid
 */
void
slaxCheckAxisName (slax_data_t *sdp, slax_string_t *axis);

/*
 * 'style' values for slaxAttribAdd*()
 *
 * Style is mainly concerned with how to represent data that can't
 * be directly represented in XSLT.  The principle case is quotes,
 * which are a problem because string in an attribute cannot have both
 * single and double quotes.  SLAX can parse:
 *     expr "quotes are \" or \'";
 * but putting this into the "select" attribute of an <xsl:value-of>
 * is not legal XSLT.  So we need to know how the caller wants us to
 * handle this.  For select attributes on <xsl:*> nodes, values that
 * are strickly strings can be tossed into a text node as the contents
 * of the element:
 *     <xsl:value-of>quotes are " or '</xsl:value-of>
 * If the value is not a static string, then things get uglier:
 *     expr fred("quotes are \" or \'");
 * would need to become:
 *     <xsl:value-of select="fred(concat('quotes are " or ', &quot;'&quot;))"/>
 * If the attribute cannot accept an XPATH value, then this concat scheme
 * will not work, but we can use <xsl:attribute> to construct the
 * proper value:
 *     <node fred="quotes are \" or \'">;
 * would need to become:
 *     <node>
 *       <xsl:attribute name="fred>quotes are " or '</xsl:attribute>
 *     </node>
 * All this is pretty annoying, but it allows the script writer to
 * avoid thinking about quotes and having to understand what's going
 * on underneath.
 */
#define SAS_NONE	0	/* Don't do anything fancy */
#define SAS_XPATH	1	/* Use concat() is you need it */
#define SAS_ATTRIB	2	/* Use <xsl:attribute> if you need it */
#define SAS_SELECT	3	/* Use concat or (non-attribute) text node */

/*
 * Add a simple value attribute.
 */
void slaxAttribAdd (slax_data_t *sdp, int style,
		    const char *name, slax_string_t *value);

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
void slaxAttribAddString (slax_data_t *sdp, const char *name,
			  slax_string_t *, unsigned flags);

/*
 * Add a literal value as an attribute to the current node
 */
void slaxAttribAddLiteral(slax_data_t *sdp, const char *name, const char *val);


/*
 * Add an XPath expression as a statement.  If this is a string,
 * we place it inside an <xsl:text> element.  Otherwise we
 * put the value inside an <xsl:value-of>'s select attribute.
 */
void slaxElementXPath (slax_data_t *sdp, slax_string_t *ssp,
		       int text_as_elt, int disable_escaping);

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
 * Construct a temporary (and unique!) namespace node and put the given
 * for the "func" exslt library and put the given node into
 * that namespace.  We also have to add this as an "extension"
 * namespace.
 */
void
slaxSetFuncNs (slax_data_t *sdp, xmlNodePtr nodep);

void
slaxSetTraceNs (slax_data_t *sdp, xmlNodePtr nodep);

void
slaxSetSlaxNs (slax_data_t *sdp, xmlNodePtr nodep, int local);

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
#define SSF_SINGLEQ	(1<<2)	/* String has single quotes */
#define SSF_DOUBLEQ	(1<<3)	/* String has double quotes */

#define SSF_BOTHQS	(1<<4)	/* String has both single and double quotes */
#define SSF_CONCAT	(1<<5)	/* Turn BOTHQS string into xpath w/ concat */

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

/**
 * Registers the SLAX extensions
 */
void slaxExtRegister (void);

/**
 * See if we need to rewrite a function call into something else.
 * The only current case is "..."/slax:build-sequnce.
 */
int slaxWriteRedoFunction(slax_data_t *, const char *, slax_string_t *);

/* ---------------------------------------------------------------------- */

/*
 * Input/output read/write/print functions
 */

/**
 * Use the input callback to get data
 * @prompt the prompt to be displayed
 */
char *
slaxInput (const char *prompt, unsigned flags);

/**
 * Use the callback to output a string
 * @fmt printf-style format string
 */
void
#ifdef HAVE_PRINTFLIKE
__printflike(1, 2)
#endif /* HAVE_PRINTFLIKE */
slaxOutput (const char *fmt, ...);

/**
 * Output a node
 */
void
slaxOutputNode (xmlNodePtr);

/**
 * Print the given nodeset. First we print the nodeset in a temp file.
 * Then read that file and send the the line to mgd one by one.
 */
void
slaxOutputNodeset (xmlNodeSetPtr nodeset);

/* ---------------------------------------------------------------------- */

/**
 * Called from the debugger when we want to profile a script.
 *
 * @docp document pointer for the script
 * @returns TRUE is there was a problem
 */
int
slaxProfOpen (xmlDocPtr docp);

/**
 * Called when we enter a instruction.
 *
 * @docp document pointer
 * @inst instruction (slax/xslt code) pointer
 */
void
slaxProfEnter (xmlNodePtr inst);

/**
 * Called when we exit an instruction
 */
void
slaxProfExit (void);

typedef int (*slaxProfCallback_t)(void *, const char *fmt, ...);

/**
 * Report the results
 */
void
slaxProfReport (int);

/**
 * Clear all values
 */
void
slaxProfClear (void);

/**
 * Done (free resources)
 */
void
slaxProfClose (void);



