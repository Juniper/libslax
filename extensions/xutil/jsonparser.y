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

/*
 * See json.org and RFC 4627 for details, but note that these
 * two sources violently disagree with each other and with
 * common usage.
 *
 * json -> object | array
 * object -> '{' member-list-or-empty '}'
 * member-list-or-empty -> empty | member-list
 * member-list -> pair | pair ',' member-list
 * pair -> name ':' value
 * value -> string | number | object | array | 'true' | 'false' | 'null'
 * array -> '[' element-list-or-empty ']'
 * element-list-or-empty -> empty | element-list
 * element-list -> value | value ',' element-list
 *
 * This parser uses many of the same concepts and much of the same code
 * as the standard slax parser (libslax/slaxparser.y).
 */

/*
 * Literal tokens
 */
%token L_FIRST			/* First literal  */
%token L_CBRACE			/* '}' */
%token L_CBRACK			/* ] */
%token L_COMMA			/* ',' */
%token L_COLON			/* ':' */
%token L_OBRACE			/* '{' */
%token L_OBRACK			/* '[' */
%token L_LAST			/* Last literal */

/*
 * keywords
 */
%token K_FALSE			/* 'false' */
%token K_NULL			/* 'null' */
%token K_TRUE			/* 'true' */

/*
 * Token types
 */
%token T_NUMBER			/* ieee-style numbers */
%token T_STRING			/* quoted string */
%token T_TOKEN			/* unquoted string (illegal but common) */

 /*
  * Magic tokens
  */
%token M_ERROR			/* An error was detected in the lexer */
%pure_parser
%{

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/xmlsave.h>

#include <libxslt/documents.h>

#include "jsonlexer.h"

extern int extXutilJsonYyDebug;

/*
 * This is a pure parser, allowing this library to link with other users
 * of yacc. To allow this, we pass our data structure (slax_data_t)
 * to yyparse as the argument.
 */
#define yyparse extXutilJsonYyParse
#define YYPARSE_PARAM slax_data
#define YYLEX_PARAM slax_data
#define YYERROR_VERBOSE

#define YYDEBUG 1		/* Enable debug output */
#define yydebug extXutilJsonDebug /* Make debug flag parser specific */
#define YYFPRINTF slaxLog2	/* Log via our function */

    
/*
 * The most common stack operation: clear everthing x and above. Since
 * slaxStackClear() returns NULL, we can say "$$ = STACK_CLEAR($1)"
 * to clear all nodes above _x ($1) off the stack.  Note that we are
 * not really popping them, since yacc/bison owns the stack.  We just
 * need to clear them to avoid double frees, memory leaks, and their
 * accompanying ring of hell.  NULL is returned, suitable for assignment
 * to "$$" (as in "$$ = STACK_CLEAR($1)").
 */
#define STACK_CLEAR(_x) slaxStackClear(slax_data, &(_x), yyvsp)

/*
 * Beginning with version 2.3, bison warns about unused values, which
 * is not appropriate for our grammar, so we need to explicitly mark
 * our mid-rule actions as UNUSED.
 */
#define STACK_UNUSED(_x...) /* nothing */

%}

%start start

%%

start :
	object
		{ $$ = STACK_CLEAR($1); }

	| array
		{ $$ = STACK_CLEAR($1); }
	;

object :
	L_OBRACE member_list_or_empty L_CBRACE
		{ $$ = STACK_CLEAR($1); }
	;

member_list_or_empty :
	/* empty */
		{ $$ = NULL; }

	| member_list
		{ $$ = $1; }
	;

member_list :
	pair
		{ $$ = STACK_CLEAR($1); }

	| pair L_COMMA member_list
		{ $$ = STACK_CLEAR($1); }
	;

pair :
	name L_COLON value
		{ $$ = STACK_CLEAR($1); }
	;

name :
	T_TOKEN
		{ $$ = $1; }

	| T_STRING
		{ $$ = $1; }
	;

value :
	object
		{ $$ = $1; }

	| array
		{ $$ = $1; }

	| T_STRING
		{ $$ = $1; }

	| T_NUMBER
		{ $$ = $1; }

	| K_TRUE
		{ $$ = $1; }

	| K_FALSE
		{ $$ = $1; }

	| K_NULL
		{ $$ = $1; }
	;

array :
	L_OBRACK element_list_or_empty L_CBRACK
		{ $$ = STACK_CLEAR($1); }
	;

element_list_or_empty :
	/* empty */
		{ $$ = NULL; }

	| element_list
		{ $$ = $1; }
	;

element_list :
	value
		{ $$ = STACK_CLEAR($1); }

	| value L_COMMA element_list
		{ $$ = STACK_CLEAR($1); }
	;

%%

const char *extXutilJsonKeywordString[YYNTOKENS];
const char *extXutilJsonTokenNameFancy[YYNTOKENS];

/*
 * Return a human-readable name for a given token type
 */
const char *
extXutilJsonTokenName (int ttype)
{
    return yytname[YYTRANSLATE(ttype)];
}

/*
 * Expose YYTRANSLATE outside the bison file
 */
int
extXutilJsonTokenTranslate (int ttype)
{
    return YYTRANSLATE(ttype);
}

/*
 * Return a better class of error message
 */
char *
extXutilJsonExpectingError (const char *token, int yystate, int yychar)
{
    const int MAX_EXPECT = 5;
    char buf[BUFSIZ], *cp = buf, *ep = buf + sizeof(buf);
    int expect = 0, expecting[MAX_EXPECT + 1];
    int yyn = yypact[yystate];
    int i;
    int yytype;
    int start, stop;

    if (!(YYPACT_NINF < yyn && yyn <= YYLAST))
	return NULL;

    yytype = YYTRANSLATE(yychar);

    start = yyn < 0 ? -yyn : 0;
    stop = YYLAST - yyn + 1;
    if (stop > YYNTOKENS)

	stop = YYNTOKENS;

    for (i = start; i < stop; ++i) {
	if (yycheck[i + yyn] == i && i != YYTERROR) {
	    if (extXutilJsonTokenNameFancy[i] == NULL)
		continue;

	    expecting[expect++] = i;
	    if (expect > MAX_EXPECT)
		break;
	}
    }

    if (expect > MAX_EXPECT)
	expect += 1;		/* Avoid "or" below */

    SNPRINTF(cp, ep, "unexpected input");
    if (token)
	SNPRINTF(cp, ep, ": %s", token);

    if (expect > 0) {
	for (i = 0; i < expect; i++) {
	    const char *pre = (i == 0) ? "; expected"
		                       : (i == expect - 1) ? " or" : ",";
	    const char *value = extXutilJsonTokenNameFancy[expecting[i]];
	    if (value)
		SNPRINTF(cp, ep, "%s %s", pre, value);

	    if (i >= MAX_EXPECT) {
		SNPRINTF(cp, ep, ", etc.");
		break;
	    }
	}
    }

    return xmlStrdup2(buf);
}
