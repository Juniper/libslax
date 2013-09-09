/*
 * $Id$
 *
 * Copyright (c) 2012, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

#if 0
/*
 * XXX This flag needs to be moved from sd_flags (owned by libslax) to
 * a caller-oriented field in slax_data_t, which will have to wait for
 * a new release.  We use "16" as "high enough to stay out of the
 * way".
 */

#define ELT_ELEMENT	"element"
#define ELT_MEMBER	"member"
#define ELT_PRETTY	"pretty"
#define ELT_QUOTES	"quotes"

#define ATT_TYPE	"type"

#define VAL_ARRAY	"array"
#define VAL_FALSE	"false"
#define VAL_NULL	"null"
#define VAL_MEMBER	"member"
#define VAL_NUMBER	"number"
#define VAL_OPTIONAL	"optional"
#define VAL_TRUE	"true"

/*
 * YYSTYPE defines our stack frame.  Since we're all about strings,
 * we use strings as a natural token stack element.
 */
#define YYSTYPE slax_string_t *	/* This is our bison stack frame */
#endif

void
slaxJsonAddTypeInfo (slax_data_t *sdp, const char *value);

void
slaxJsonClearMember (slax_data_t *sdp);

#if 0
/**
 * Callback from bison when an error is detected.
 *
 * @param sdp main json data structure
 * @param str error message
 * @param value stack entry from bison's lexical stack
 * @return zero
 */
int
slaxJsonYyerror (slax_data_t *sdp, const char *str, slax_string_t *yylval,
	     int yystate, slax_string_t **vstack, slax_string_t **vtop);
#undef yyerror			/* Remove slaxinternals.h definition */
#define yyerror(str) \
    slaxJsonYyerror(slax_data, str, yylval, yystate, yyvs, yyvsp)

/*
 * Return a better class of error message
 */
char *
slaxJsonExpectingError (const char *token, int yystate, int yychar);
#endif

/**
 * Make a child node and assign it proper file/line number info.
 */
xmlNodePtr
slaxJsonAddChildLineNo (xmlParserCtxtPtr ctxt, xmlNodePtr parent, xmlNodePtr cur);

#if 0
/**
 * Lexer function called from bison's yyparse()
 *
 * We lexically analyze the input and return the token type to
 * bison's yyparse function, which we've renamed to slaxJsonParse.
 *
 * @param sdp main json data structure
 * @param yylvalp pointer to bison's lval (stack value)
 * @return token type
 */
int
slaxJsonYylex (slax_data_t *sdp, YYSTYPE *yylvalp);

/*
 * We redefine yylex to give our parser a specific name, avoiding
 * conflicts with other yacc/bison based parsers.
 */
#undef yylex			/* Remove slaxinternals. definition */
#define yylex(sp, v) slaxJsonYylex(slax_data, sp)
#endif

void
slaxJsonElementOpen (slax_data_t *sdp, const char *name);

void
slaxJsonElementOpenName (slax_data_t *sdp, char *name);

/* ----------------------------------------------------------------------
 * Functions exposed in jsonparser.y (no better place than here)
 */

/*
 * The bison-based parser's main function
 */
int
slaxJsonYyParse (slax_data_t *);

/*
 * Return a human-readable name for a given token type
 */
const char *
slaxJsonTokenName (int ttype);

/*
 * Expose YYTRANSLATE outside the bison file
 */
int
slaxJsonTokenTranslate (int ttype);

xmlDocPtr
slaxJsonDataToXml (const char *data, const char *root_name,
		       unsigned flags);

xmlDocPtr
slaxJsonFileToXml (const char *fname, const char *root_name,
		       unsigned flags);

void
slaxJsonElementValue (slax_data_t *sdp, slax_string_t *value);

int
slaxJsonLexer (slax_data_t *sdp);
