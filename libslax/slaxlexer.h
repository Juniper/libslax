/*
 * $Id$
 *
 * Copyright (c) 2006-2011, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

#include <ctype.h>		/* isalnum() */

/*
 * This is a should-not-occur error string if you ever see it, we've
 * done something wrong (but it beats a core dump).
 */
#define UNKNOWN_EXPR "<<<<slax error>>>>"

extern int slaxLogIsEnabled;	/* Global logging output switch */
extern const char *slaxKeywordString[];
extern const char *slaxTokenNameFancy[];

/*
 * YYSTYPE defines our stack frame.  Since we're all about strings,
 * we use strings as a natural token stack element.
 */
#define YYSTYPE slax_string_t *	/* This is our bison stack frame */

/*
 * Our main data structure
 */
struct slax_data_s {
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
    xmlNodePtr sd_nodep;	/* Node for looking up ternary expressions */
    xmlNodePtr sd_insert;	/* List of nodes to be inserted shortly */
};

/* Flags for sd_flags */
#define SDF_EOF			(1<<0)	/* EOF seen */
#define SDF_NO_SLAX_KEYWORDS	(1<<1)	/* Do not allow slax keywords */
#define SDF_NO_XPATH_KEYWORDS	(1<<2)	/* Do not allow xpath keywords */
#define SDF_OPEN_COMMENT	(1<<3)	/* EOF with open comment */

#define SDF_JSON_KEYWORDS	(1<<4) /* Allow JSON keywords */
#define SDF_NO_TYPES		(1<<5)/* Do not decorate nodes w/ type info */
#define SDF_CLEAN_NAMES		(1<<6) /* Clean element names, if needed */

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
#define JSON_KEYWORDS_ALLOWED(_x) ((_x)->sd_flags & SDF_JSON_KEYWORDS)

/**
 * Callback from bison when an error is detected.
 *
 * @param sdp main slax data structure
 * @param str error message
 * @param value stack entry from bison's lexical stack
 * @return zero
 */
int
slaxYyerror (slax_data_t *sdp, const char *str, slax_string_t *yylval,
	     int yystate, slax_string_t **vstack, slax_string_t **vtop);
#define yyerror(str) slaxYyerror(slax_data, str, yylval, yystate, yyvs, yyvsp)

/*
 * Return a better class of error message
 */
char *
slaxExpectingError (const char *token, int yystate, int yychar);

/**
 * Make a child node and assign it proper file/line number info.
 */
xmlNodePtr
slaxAddChildLineNo (xmlParserCtxtPtr ctxt, xmlNodePtr parent, xmlNodePtr cur);

/**
 * Add an item to the insert list
 *
 * @param sdp Main parsing data structure
 * @param nodep Node to add
 */
void
slaxAddInsert (slax_data_t *sdp, xmlNodePtr nodep);

/**
 * Add a child to the current node context
 *
 * @param sdp Main parsing data structure
 * @param parent Parent node (if NULL defaults to the context node)
 * @param nodep Child node
 */
xmlNodePtr
slaxAddChild (slax_data_t *sdp, xmlNodePtr parent, xmlNodePtr nodep);

/**
 * Issue an error if the axis name is not valid
 *
 * @param sdp main slax data structure
 * @param axis name of the axis to check
 */
void
slaxCheckAxisName (slax_data_t *sdp, slax_string_t *axis);

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
slaxYylex (slax_data_t *sdp, YYSTYPE *yylvalp);

/*
 * We redefine yylex to give our parser a specific name, avoiding
 * conflicts with other yacc/bison based parsers.
 */
#define yylex(sp, v) slaxYylex(slax_data, sp)

/* ----------------------------------------------------------------------
 * Functions exposed in slaxparser.y (no better place than here)
 */

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

/* ---------------------------------------------------------------------- */

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
 * Fill the input buffer, shifting data forward if we need the room
 * and dynamically reallocating the buffer if we still need room.
 */
int
slaxGetInput (slax_data_t *sdp, int final);

int
slaxParseIsSlax (slax_data_t *sdp);

int
slaxParseIsXpath (slax_data_t *sdp);

/*
 * Set up the lexer's lookup tables
 */
void
slaxSetupLexer (void);
