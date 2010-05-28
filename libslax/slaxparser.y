%{
#line 2 "slaxparser.y"
%}

/*
 * $Id: slaxparser.y,v 1.3 2008/05/21 02:06:12 phil Exp $
 *
 * Copyright (c) 2006-2008, Juniper Networks, Inc.
 * All rights reserved.
 * See ./Copyright for the status of this software
 *
 * SLAX:  Stylesheet Language Alternative syntaX
 *
 * This bison file parses a language called SLAX, which is an alternative
 * syntax for XSLT.  SLAX follows a more C- and perl-like syntax, rather
 * than the XML encoding that normal XSLT uses.
 *
 * More information is in "slax.txt".
 */

/*
 * SLAX uses traditional XPath expressions, which have a number of
 * minor ambiguities that the lexer is responsible for handling.
 *
 * From the XPath spec:
 *
 * | When tokenizing, the longest possible token is always returned.
 * |
 * | For readability, whitespace may be used in expressions even though
 * | not explicitly allowed by the grammar: ExprWhitespace may be freely
 * | added within patterns before or after any ExprToken.
 * |
 * | The following special tokenization rules must be applied in the
 * | order specified to disambiguate the ExprToken grammar:
 * |
 * | - If there is a preceding token and the preceding token is not one
 * |   of @, ::, (, [, , or an Operator, then a * must be recognized as a
 * |   MultiplyOperator and an NCName must be recognized as an
 * |   OperatorName.
 * |
 * | - If the character following an NCName (possibly after intervening
 * |   ExprWhitespace) is (, then the token must be recognized as a
 * |   NodeType or a FunctionName.
 * |
 * | - If the two characters following an NCName (possibly after
 * |   intervening ExprWhitespace) are ::, then the token must be
 * |   recognized as an AxisName.
 * |
 * | - Otherwise, the token must not be recognized as a
 * |   MultiplyOperator, an OperatorName, a NodeType, a FunctionName, or
 * |   an AxisName.
 *
 * We simplify the tokenization a bit by returning the prefix and axis
 * in the NCName (T_BARE) token, but need to handle expressions like
 *
 *     div[ mod div div == div mod mod]
 *
 * This is guided by the first and last points in the spec segment above.
 * Unfortunately the list of operators for XSLT is long and SLAX adds a
 * few more, so we arrive our tokens (which bison numbers incrementally)
 * to allow us to use range tests.
 */

/*
 * Literal tokens which may _not_ preceed the multiplication operator
 */
%token L_ASSIGN			/* ':=' */
%token L_AT			/* '@' */
%token L_CBRACE			/* } */
%token L_COMMA			/* , */
%token L_DAMPER			/* '&&' */
%token L_DEQUALS		/* == */
%token L_DOTDOT			/* '..' */
%token L_DSLASH			/* '//' */
%token L_DVBAR			/* '||' */
%token L_EOS			/* ; */
%token L_EQUALS			/* = */
%token L_GRTR			/* '>' */
%token L_GRTREQ			/* '>=' */
%token L_LESS			/* '<' */
%token L_LESSEQ			/* '<=' */
%token L_MINUS			/* '-' */
%token L_NOTEQUALS		/* '!=' */
%token L_OBRACE			/* { */
%token L_OBRACK			/* [ */
%token L_OPAREN			/* ( */
%token L_PLUS			/* '+' */
%token L_SLASH			/* '/' */
%token L_STAR			/* '*' */
%token L_UNDERSCORE		/* '_' */
%token L_VBAR			/* '|' */

/*
 * Keyword tokens
 */
%token K_APPLY_IMPORTS		/* 'apply-imports' */
%token K_APPLY_TEMPLATES	/* 'apply-templates' */
%token K_CALL			/* 'call' */
%token K_COMMENT		/* 'comment' */
%token K_COPY_OF		/* 'copy-of' */
%token K_EXCLUDE		/* 'exclude' */
%token K_ELSE			/* 'else' */
%token K_EXPR			/* 'expr' */
%token K_EXTENSION		/* 'extension' */
%token K_FOR_EACH		/* 'for-each' */
%token K_ID			/* 'id' */
%token K_IF			/* 'if' */
%token K_IMPORT			/* 'import' */
%token K_INCLUDE		/* 'include' */
%token K_KEY			/* 'key' */
%token K_MATCH			/* 'match' */
%token K_MODE			/* 'mode' */
%token K_NODE			/* 'node' */
%token K_NS			/* 'ns' */
%token K_PARAM			/* 'param' */
%token K_PRESERVE_SPACE		/* 'preserve-space' */
%token K_PRIORITY		/* 'priority' */
%token K_PROCESSING_INSTRUCTION /* 'processing-instruction' */
%token K_STRIP_SPACE		/* 'strip-space' */
%token K_TEMPLATE		/* 'template' */
%token K_TEXT			/* 'text' */
%token K_VAR			/* 'var' */
%token K_VERSION		/* 'version' */
%token K_WITH			/* 'with' */

/*
 * Operator keyword tokens, which might be NCNames if they appear inside an
 * XPath expression
 */
%token M_OPERATOR_FIRST		/* Magic marker: first operator keyword */
%token K_AND			/* 'and' */
%token K_DIV			/* 'div' */
%token K_MOD			/* 'mod' */
%token K_OR			/* 'or' */
%token M_OPERATOR_LAST		/* Magic marker: last operator keyword */

/*
 * Literal tokens which _may_ preceed the multiplication operator
 */
%token M_MULTIPLICATION_TEST_LAST /* Magic marker: highest token number */
%token L_ASTERISK		/* '*' */
%token L_CBRACK			/* ] */
%token L_CPAREN			/* ) */
%token L_DOT			/* . */

/*
 * Token types: generic tokens (returned via ss_token)
 */
%token T_BARE			/* a bare word string (bare-word) */
%token T_FUNCTION_NAME		/* a function name (bare-word) */
%token T_NUMBER			/* a number (4) */
%token T_QUOTED			/* a quoted string ("foo") */
%token T_VAR			/* a variable name ($foo) */

/*
 * Magic tokens (used for special purposes)
 */
%token M_ERROR			/* An error was detected in the lexer */
%token M_XPATH			/* Building an XPath expression */
%token M_PARSE_SLAX		/* Parse a slax document */
%token M_PARSE_XPATH		/* Parse an XPath expression */

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

#include <libslax/slax.h>
#include "slaxinternals.h"
#include "slaxparser.h"

/*
 * This is a pure parser, allowing this library to link with other users
 * of yacc. To allow this, we pass our data structure (slax_data_t)
 * to yyparse as the argument.
 */
#define yyparse slaxParse
#define YYPARSE_PARAM slax_data
#define YYLEX_PARAM slax_data
#define YYERROR_VERBOSE

#define YYDEBUG 1		/* Enable debug output */
#define yydebug slaxDebug	/* Make debug flag parser specific */
#define YYFPRINTF fprintf	/* Should do something better */

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
 * STACK_LINK links the items on the stacks at and above "_x"
 * into a linked list, which is left in "_x" on the stack.
 * This allows us to turn a complete production of text nodes
 * into a single stack node, allowing simplified handling of
 * XPath expressions.
 */
#define STACK_LINK(_x) slaxStringLink(slax_data, &(_x), yyvsp)

/*
 * Beginning with version 2.3, bison warns about unused values, which
 * is not appropriate for our grammar, so we need to explicitly mark
 * our mid-rule actions as UNUSED.
 */
#define STACK_UNUSED(_x...) /* nothing */

%}

/* -------------------------------------------------------------------
 * This parser is a dual mode parser, capable of parsing either full
 * SLAX documents or strings containing XPath expressions.  SLAX already
 * includes full XPath rules, and we need to parse and rebuild XPath
 * expressions for the slaxwriter code, so reusing these rules makes
 * our life easier.
 *
 * This choice of modes is driven by the value seeded into sd_ttype,
 * one of the M_PARSE_* values.  When this value is returned
 * by the lexer (slaxYylex()), it drives the "start" production
 * to fork onto the appropriate path below.
 */

%start start

%%

start :
	M_PARSE_SLAX stylesheet
		{ $$ = STACK_CLEAR($1); }

	| M_PARSE_XPATH xp_expr
		{
		    slax_data->sd_xpath = $2;
		    $2 = NULL;	/* Avoid double free */
		    $$ = STACK_CLEAR($1);
		}
	;

stylesheet :
	version_stmt ns_list slax_stmt_list
		{ $$ = NULL; }
	;

version_stmt :
	K_VERSION version_number L_EOS
		{ $$ = STACK_CLEAR($1); }
	;

version_number :
	T_NUMBER L_DOT T_NUMBER
		{
		    slaxVersionMatch($1->ss_token, $3->ss_token);
		    $$ = STACK_CLEAR($1);
		}
	;

ns_list :
	/* empty */
		{ $$ = NULL; }

	| ns_list ns_decl
		{
		    ALL_KEYWORDS_ON();
		    $$ = NULL;
		}
	;

ns_decl :
	K_NS T_BARE
		{
		    ALL_KEYWORDS_ON();
		    $$ = NULL;
		}
	    ns_options L_EQUALS T_QUOTED L_EOS
		{
		    slaxNsAdd(slax_data, $2->ss_token, $6->ss_token);
		    if ($4) {
			slaxAttribExtend(slax_data,
					 $4->ss_token, $2->ss_token);
		    }

		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($3);
		}

	| K_NS T_QUOTED L_EOS
		{
		    ALL_KEYWORDS_ON();
		    slaxNsAdd(slax_data, NULL, $2->ss_token);
		    $$ = STACK_CLEAR($1);
		}
	;

ns_options :
	/* empty */
		{ $$ = NULL; }

	| K_EXCLUDE
		{ $$ = slaxStringLiteral(ATT_EXCLUDE_RESULT_PREFIXES, 0); }

	| K_EXTENSION
		{ $$ = slaxStringLiteral(ATT_EXTENSION_ELEMENT_PREFIXES, 0); }
	;

slax_stmt_list :
	/* empty */
		{ $$ = NULL; }

	| slax_stmt_list slax_stmt
		{
		    ALL_KEYWORDS_ON();
		    $$ = NULL;
		}
	;

slax_stmt :
	param_decl
		{ $$ = NULL; }

	| var_decl
		{ $$ = NULL; }

	| import_stmt
		{ $$ = NULL; }

	| include_stmt
		{ $$ = NULL; }

	| strip_space_stmt
		{ $$ = NULL; }

	| preserve_space_stmt
		{ $$ = NULL; }

	| element_stmt
		{ $$ = NULL; }

	| match_template
		{ $$ = NULL; }

	| named_template
		{ $$ = NULL; }

	| error L_EOS
		{ $$ = STACK_CLEAR($1); }
	;

import_stmt :
	K_IMPORT T_QUOTED L_EOS
		{
		    slaxElementAdd(slax_data, $1->ss_token,
				  ATT_HREF, $2->ss_token);
		    $$ = STACK_CLEAR($1);
		}
	;

include_stmt :
	K_INCLUDE T_QUOTED L_EOS
		{
		    slaxElementAdd(slax_data, $1->ss_token,
				  ATT_HREF, $2->ss_token);
		    $$ = STACK_CLEAR($1);
		}
	;

strip_space_stmt :
	K_STRIP_SPACE element_list L_EOS
		{
		    ALL_KEYWORDS_ON();
		    slaxElementAdd(slax_data, $1->ss_token,
				   ATT_ELEMENTS, $2->ss_token);
		    $$ = STACK_CLEAR($1);
		}
	;

element_list :
	xpc_name_test
		{ $$ = STACK_LINK($1); }

	| element_list xpc_name_test
		{ $$ = STACK_LINK($1); }
	;

preserve_space_stmt :
	K_PRESERVE_SPACE element_list L_EOS
		{
		    ALL_KEYWORDS_ON();
		    slaxElementAdd(slax_data, $1->ss_token,
				   ATT_ELEMENTS, $2->ss_token);
		    $$ = STACK_CLEAR($1);
		}
	;

param_decl :
	K_PARAM T_VAR L_EOS
		{
		    slaxElementAdd(slax_data, ELT_PARAM,
					   ATT_NAME, $2->ss_token + 1);
		    $$ = STACK_CLEAR($1);
		}

	| K_PARAM T_VAR L_EQUALS
		{
		    KEYWORDS_OFF();
		    slaxElementPush(slax_data, ELT_PARAM,
				    ATT_NAME, $2->ss_token + 1);
		    $$ = NULL;
		}
	    initial_value
		{
		    KEYWORDS_ON();
		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($4);
		}

	| K_PARAM T_VAR L_ASSIGN
		{
		    KEYWORDS_OFF();
		    slaxElementPush(slax_data, ELT_PARAM,
				    ATT_NAME, $2->ss_token + 1);
		    $$ = NULL;
		}
	    initial_value
		{
		    KEYWORDS_ON();
		    slaxAvoidRtf(slax_data);
		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($4);
		}
	;

var_decl :
	K_VAR T_VAR L_EOS
		{
		    slaxElementAdd(slax_data, ELT_VARIABLE,
					   ATT_NAME, $2->ss_token + 1);
		    $$ = STACK_CLEAR($1);
		}

	| K_VAR T_VAR L_EQUALS
		{
		    KEYWORDS_OFF();
		    slaxElementPush(slax_data, ELT_VARIABLE,
				    ATT_NAME, $2->ss_token + 1);
		    $$ = NULL;
		}
	    initial_value
		{
		    KEYWORDS_ON();
		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($4);
		}

	| K_VAR T_VAR L_ASSIGN
		{
		    KEYWORDS_OFF();
		    slaxElementPush(slax_data, ELT_VARIABLE,
				    ATT_NAME, $2->ss_token + 1);
		    $$ = NULL;
		}
	    initial_value
		{
		    KEYWORDS_ON();
		    slaxAvoidRtf(slax_data);
		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($4);
		}
	;

initial_value :
	xpath_value L_EOS
		{
		    KEYWORDS_ON();
		    slaxAttribAdd(slax_data, ATT_SELECT, $1);
		    $$ = STACK_CLEAR($1);
		}

	| block
		{
		    KEYWORDS_ON();
		    $$ = NULL;
		}

	| element_stmt
		{
		    KEYWORDS_ON();
		    $$ = NULL;
		}
	;

match_template :
	K_MATCH xs_pattern
		{
		    xmlNodePtr nodep;

		    KEYWORDS_ON();
		    nodep = slaxElementPush(slax_data, ELT_TEMPLATE,
					    NULL, NULL);
		    if (nodep)
			slaxAttribAddString(slax_data, ATT_MATCH, $2);
		    /* XXX else error */

		    $$ = NULL;
		}
	    L_OBRACE match_block_contents L_CBRACE
		{
		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($3);
		}
	;

match_block_contents :
	match_preamble_list block_preamble_list block_statement_list
		{ $$ = NULL; }
	;

match_preamble_list :
	/* empty */
		{ $$ = NULL; }

	| match_preamble_list match_block_preamble_stmt
		{
		    ALL_KEYWORDS_ON();
		    $$ = NULL;
		}
	;

match_block_preamble_stmt :
	mode_stmt
		{ $$ = NULL; }

	| priority_stmt
		{ $$ = NULL; }
	;

mode_stmt :
	K_MODE T_QUOTED L_EOS
		{
		    slaxAttribAdd(slax_data, ATT_MODE, $2);
		    $$ = STACK_CLEAR($1);
		}
	;

priority_stmt :
	K_PRIORITY xpc_full_number L_EOS
		{
		    slaxAttribAdd(slax_data, ATT_PRIORITY, $2);
		    $$ = STACK_CLEAR($1);
		}
	;

named_template :
	template_name_stmt
		{
		    slaxElementPush(slax_data, ELT_TEMPLATE,
				    ATT_NAME, $1->ss_token);
		    $$ = NULL;
		}
	    named_template_arguments block
		{
		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($2);
		}
	;

template_name_stmt :
	template_name
		{ $$ = $1; }

	| K_TEMPLATE template_name
		{
		    ALL_KEYWORDS_ON();
		    $$ = $2;
		}
	;

named_template_arguments :
	/* empty */
		{ $$ = NULL; }

	| L_OPAREN named_template_argument_list L_CPAREN
		{ $$ = NULL; }
	;

named_template_argument_list :
	/* empty */
		{ $$ = NULL; }

	| named_template_argument_list_not_empty
		{ $$ = NULL; }
	;

named_template_argument_list_not_empty :
	named_template_argument_decl
		{ $$ = NULL; }

	| named_template_argument_list_not_empty
		    L_COMMA named_template_argument_decl
		{ $$ = STACK_CLEAR($1); }
	;

named_template_argument_decl :
	T_VAR
		{
		    slaxElementAdd(slax_data, ELT_PARAM,
				    ATT_NAME, $1->ss_token + 1);
		    $$ = STACK_CLEAR($1);
		}

	| T_VAR L_EQUALS
		{
		    KEYWORDS_OFF();
		    $$ = NULL;
		}
	    xpath_value
		{
		    xmlNodePtr nodep;

		    KEYWORDS_ON();
		    nodep = slaxElementAdd(slax_data, ELT_PARAM,
					   ATT_NAME, $1->ss_token + 1);
		    if (nodep) {
			nodePush(slax_data->sd_ctxt, nodep);
			slaxAttribAdd(slax_data, ATT_SELECT, $4);
			nodePop(slax_data->sd_ctxt);
		    }
		    /* XXX else error */
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($3);
		}
	;

block :
	L_OBRACE
		{
		    KEYWORDS_ON();
		    $$ = NULL;
		}
	    block_contents L_CBRACE
		{
		    $$ = NULL;
		    STACK_UNUSED($2);
		}
	;

block_contents :
	block_preamble_list block_statement_list
		{ $$ = NULL; }
	;

block_preamble_list :
	/* empty */
		{ $$ = NULL; }

	| block_preamble_list block_preamble_stmt
		{
		    ALL_KEYWORDS_ON();
		    $$ = NULL;
		}
	;

block_preamble_stmt :
	ns_decl
		{ $$ = NULL; }
	;

block_statement_list :
	/* empty */
		{ $$ = NULL; }

	| block_statement_list block_statement
		{
		    ALL_KEYWORDS_ON();
		    $$ = NULL;
		}
	;

block_statement :
	apply_imports_stmt
		{ $$ = NULL; }

	| apply_templates_stmt
		{ $$ = NULL; }

	| call_stmt
		{ $$ = NULL; }

	| comment_stmt
		{ $$ = NULL; }

	| copy_of_stmt
		{ $$ = NULL; }

	| element_stmt
		{ $$ = NULL; }

	| for_each_stmt
		{ $$ = NULL; }

	| if_stmt
		{ $$ = NULL; }

	| param_decl
		{ $$ = NULL; }

	| var_decl
		{ $$ = NULL; }

	| expr_stmt
		{ $$ = STACK_CLEAR($1); }

	| L_EOS
		{ $$ = NULL; }

	| error L_EOS
		{ $$ = NULL; }
	;

apply_imports_stmt :
	K_APPLY_IMPORTS L_EOS
		{
		    slaxElementAdd(slax_data, ELT_APPLY_IMPORTS,
					    NULL, NULL);
		    $$ = STACK_CLEAR($1);
		}
	;

apply_templates_stmt :
	K_APPLY_TEMPLATES
		{
		    KEYWORDS_OFF();
		    slaxElementPush(slax_data, ELT_APPLY_TEMPLATES,
					    NULL, NULL);
		    $$ = NULL;
		}
	    xpath_expr_optional apply_template_arguments
		{
		    KEYWORDS_OFF();
		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($2);
		}
	;

xpath_expr_optional :
	/* empty */
		{ $$ = NULL; }

	| xp_expr
		{
		    KEYWORDS_ON();
		    slaxAttribAddString(slax_data, ATT_SELECT, $1);
		    $$ = STACK_CLEAR($1);
		}
	;

expr_stmt :
	K_EXPR xpath_value L_EOS
		{
		    KEYWORDS_ON();
		    slaxElementXPath(slax_data, $2, TRUE);
		    $$ = STACK_CLEAR($1);
		}
	;

call_stmt :
	K_CALL template_name
		{
		    ALL_KEYWORDS_ON();
		    slaxElementPush(slax_data, ELT_CALL_TEMPLATE,
					   ATT_NAME, $2->ss_token);
		    $$ = NULL;
		}
	    call_arguments
		{
		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($3);
		}
	;

q_name :
	T_BARE
		{ $$ = $1; }
	;

template_name :
	q_name
		{ $$ = $1; }

	| T_FUNCTION_NAME
		{ $$ = $1; }
	;

apply_template_arguments :
	call_arguments_braces_style
		{ $$ = NULL; }
	;

call_arguments :
	call_arguments_parens_style call_arguments_braces_style
		{ $$ = NULL; }
	;

call_arguments_parens_style :
	/* empty */
		{ $$ = NULL; }

	| L_OPAREN call_argument_list L_CPAREN
		{ $$ = NULL; }
	;

call_argument_list :
	/* empty */
		{ $$ = NULL; }

	| call_argument_list_not_empty
		{ $$ = NULL; }
	;

call_argument_list_not_empty :
	call_argument_member
		{ $$ = NULL; }

	| call_argument_list_not_empty L_COMMA call_argument_member
		{ $$ = STACK_CLEAR($1); }
	;

call_argument_member :
	T_VAR
		{
		    xmlNodePtr nodep;
		    nodep = slaxElementAdd(slax_data, ELT_WITH_PARAM,
				   ATT_NAME, $1->ss_token + 1);
		    if (nodep) {
			nodePush(slax_data->sd_ctxt, nodep);
			slaxAttribAdd(slax_data, ATT_SELECT, $1);
			nodePop(slax_data->sd_ctxt);
		    }
		    /* XXX else error */
		    $$ = STACK_CLEAR($1);
		}

	| T_VAR L_EQUALS
		{
		    KEYWORDS_OFF();
		    $$ = NULL;
		}
	    xpath_value
		{
		    xmlNodePtr nodep;

		    KEYWORDS_ON();
		    nodep = slaxElementAdd(slax_data, ELT_WITH_PARAM,
				   ATT_NAME, $1->ss_token + 1);
		    if (nodep) {
			nodePush(slax_data->sd_ctxt, nodep);
			slaxAttribAdd(slax_data, ATT_SELECT, $4);
			nodePop(slax_data->sd_ctxt);
		    }
		    /* XXX else error */

		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($3);
		}
	;

call_arguments_braces_style :
	L_EOS /* This is the semi-colon to end the statement */
		{ $$ = NULL; }

	| L_OBRACE
		{
		    KEYWORDS_ON();
		    $$ = NULL;
		}
	    call_argument_braces_list L_CBRACE
		{
		    $$ = NULL;
		    STACK_UNUSED($2);
		}
	;

call_argument_braces_list :
	call_argument_braces_list_not_empty
		{ $$ = NULL; }
	;

call_argument_braces_list_not_empty :
	call_argument_braces_member
		{ $$ = NULL; }

	| call_argument_braces_list_not_empty
			call_argument_braces_member
		{ $$ = STACK_CLEAR($1); }
	;

call_argument_braces_member :
	K_WITH T_VAR L_EOS
		{
		    xmlNodePtr nodep;
		    nodep = slaxElementAdd(slax_data, ELT_WITH_PARAM,
				   ATT_NAME, $2->ss_token + 1);
		    if (nodep) {
			nodePush(slax_data->sd_ctxt, nodep);
			slaxAttribAdd(slax_data, ATT_SELECT, $2);
			nodePop(slax_data->sd_ctxt);
		    }
		    /* XXX else error */
		    $$ = STACK_CLEAR($1);
		}

	| K_WITH T_VAR L_EQUALS
		{
		    KEYWORDS_OFF();
		    xmlNodePtr nodep;
		    nodep = slaxElementAdd(slax_data, ELT_WITH_PARAM,
				   ATT_NAME, $2->ss_token + 1);
		    if (nodep)
			nodePush(slax_data->sd_ctxt, nodep);

		    $$ = NULL;
		}
	    initial_value
		{
		    KEYWORDS_ON();
		    nodePop(slax_data->sd_ctxt);

		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($4);
		}

	| mode_stmt
		{ $$ = NULL; }

	| element_stmt
		{ $$ = NULL; }
	;

comment_stmt :
	K_COMMENT xpath_value L_EOS
		{
		    KEYWORDS_ON();
		    xmlNodePtr nodep;
		    nodep = slaxElementAdd(slax_data, ELT_COMMENT, NULL, NULL);
		    if (nodep) {
			nodePush(slax_data->sd_ctxt, nodep);
			nodep = slaxElementAdd(slax_data, ELT_VALUE_OF,
					       NULL, NULL);
			if (nodep) {
			    nodePush(slax_data->sd_ctxt, nodep);
			    slaxAttribAdd(slax_data, ATT_SELECT, $2);
			    nodePop(slax_data->sd_ctxt);
			}
			nodePop(slax_data->sd_ctxt);
		    }
		    /* XXX else error */
		    $$ = STACK_CLEAR($1);
		}
	;

copy_of_stmt :
	K_COPY_OF xp_expr L_EOS
		{
		    KEYWORDS_ON();
		    xmlNodePtr nodep;
		    nodep = slaxElementAdd(slax_data, ELT_COPY_OF, NULL, NULL);
		    if (nodep) {
			nodePush(slax_data->sd_ctxt, nodep);
			slaxAttribAddString(slax_data, ATT_SELECT, $2);
			nodePop(slax_data->sd_ctxt);
		    }
		    /* XXX else error */
		    $$ = STACK_CLEAR($1);
		}
	;

element :
	L_LESS
		{
		    ALL_KEYWORDS_OFF();
		    $$ = NULL;
		}
	    q_name
		{
		    slaxElementOpen(slax_data, $3->ss_token);
		    $$ = NULL;
		}
	    attribute_list L_GRTR
		{
		    ALL_KEYWORDS_ON();
		    KEYWORDS_OFF();
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($2, $4);
		}
	;

element_stmt :
	element L_EOS
		{
		    ALL_KEYWORDS_ON();
		    slaxElementClose(slax_data);
		    $$ = STACK_CLEAR($1);
		}

	| element block
		{
		    ALL_KEYWORDS_ON();
		    slaxElementClose(slax_data);
		    $$ = STACK_CLEAR($1);
		}

	| element xpath_value L_EOS
		{
		    ALL_KEYWORDS_ON();
		    slaxElementXPath(slax_data, $2, FALSE);
		    slaxElementClose(slax_data);
		    $$ = STACK_CLEAR($1);
		}
	;

attribute_list :
	/* empty */
		{ $$ = NULL; }

	| attribute_list attribute
		{ $$ = NULL; }
	;

attribute :
	q_name L_EQUALS
		{
		    $$ = NULL;
		}
	    xpath_lite_value
		{
		    slaxAttribAddValue(slax_data, $1->ss_token, $4);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($3);
		}
	;

for_each_stmt :
	K_FOR_EACH L_OPAREN xp_expr L_CPAREN
		{
		    xmlNodePtr nodep;
		    KEYWORDS_ON();
		    nodep = slaxElementPush(slax_data, ELT_FOR_EACH,
					    NULL, NULL);
		    slaxAttribAddString(slax_data, ATT_SELECT, $3);
		    $$ = NULL;
		}
	    block
		{
		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($5);
		}
	;

if_stmt :
	K_IF L_OPAREN xp_expr L_CPAREN
		{
		    KEYWORDS_ON();
		    slaxElementPush(slax_data, ELT_CHOOSE, NULL, NULL);
		    slaxElementPush(slax_data, ELT_WHEN, NULL, NULL);
		    slaxAttribAddString(slax_data, ATT_TEST, $3);
		    $$ = NULL;
		}
	    block
		{
		    slaxElementPop(slax_data); /* Pop when */
		    $$ = NULL;
		}
	    elsif_stmt_list else_stmt
		{
		    xmlNodePtr choosep = slax_data->sd_ctxt->node;
		    slaxElementPop(slax_data); /* Pop choose */
		    slaxCheckIf(slax_data, choosep);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($5, $7);
		}
	;

elsif_stmt_list :
	/* empty */
		{ $$ = NULL; }

	| elsif_stmt_list elsif_stmt
		{ $$ = NULL; }
	;

elsif_stmt :
	K_ELSE K_IF L_OPAREN xp_expr L_CPAREN 
		{
		    KEYWORDS_ON();
		    slaxElementPush(slax_data, ELT_WHEN, NULL, NULL);
		    slaxAttribAddString(slax_data, ATT_TEST, $4);
		    $$ = NULL;
		}
	    block
		{
		    slaxElementPop(slax_data); /* Pop when */
		    $$ = NULL;
		    STACK_UNUSED($6);
		}
	;

else_stmt :
	/* empty */
		{ $$ = NULL; }

	| K_ELSE 
		{
		    slaxElementPush(slax_data, ELT_OTHERWISE, NULL, NULL);
		    $$ = NULL;
		}
	    block
		{
		    slaxElementPop(slax_data); /* Pop otherwise */
		    $$ = NULL;
		    STACK_UNUSED($2);
		}
	;

and_operator :
	K_AND
		{
		    if (slax_data->sd_parse == M_PARSE_XPATH) {
			STACK_CLEAR($1);
			$$ = slaxStringLiteral("&&", L_DAMPER);
		    } else $$ = $1;
		}

	| L_DAMPER
		{
		    if (slax_data->sd_parse == M_PARSE_SLAX) {
			STACK_CLEAR($1);
			$$ = slaxStringLiteral("and", L_DAMPER);
		    } else $$ = $1;
		}
	;

or_operator :
	K_OR
		{
		    if (slax_data->sd_parse == M_PARSE_XPATH) {
			STACK_CLEAR($1);
			$$ = slaxStringLiteral("||", L_DVBAR);
		    } else $$ = $1;
		}

	| L_DVBAR
		{
		    if (slax_data->sd_parse == M_PARSE_SLAX) {
			STACK_CLEAR($1);
			$$ = slaxStringLiteral("or", L_DVBAR);
		    } else $$ = $1;
		}
	;

equals_operator :
	L_EQUALS
		{
		    if (slax_data->sd_parse == M_PARSE_XPATH) {
			STACK_CLEAR($1);
			$$ = slaxStringLiteral("==", L_DEQUALS);
		    } else $$ = $1;
		}

	| L_DEQUALS
		{
		    if (slax_data->sd_parse == M_PARSE_SLAX) {
			STACK_CLEAR($1);
			$$ = slaxStringLiteral("=", L_DEQUALS);
		    } else $$ = $1;
		}
	;

xpath_value :
	xp_expr
		{
		    /*
		     * Turn the entire xpath expression into a string
		     */
		    slax_string_t *ssp;
		    ssp = slaxStringConcat(slax_data, M_XPATH, &$1);
		    slaxTrace("xpath: %s", ssp ? ssp->ss_token : "");
		    STACK_CLEAR($1);
		    KEYWORDS_ON();
		    $$ = ssp;
		}

	| xpath_value L_UNDERSCORE xp_expr
		{
		    slax_string_t *ssp, *newp;

		    if ($3) {
			newp = slaxStringConcat(slax_data, M_XPATH, &$3);

			/* Append the new xp_path_expr to the list */
			for (ssp = $1; ssp; ssp = ssp->ss_next) {
			    if (ssp->ss_next == NULL) {
				ssp->ss_next = newp;
				break;
			    }
			}
		    }

		    ssp = $1;
		    $1 = NULL;	/* Save from free() */
		    STACK_CLEAR($1);
		    KEYWORDS_ON();
		    $$ = ssp;
		}
	;

xpath_lite_value :
	xpl_expr
		{
		    /*
		     * Turn the entire xpath expression into a string
		     */
		    slax_string_t *ssp;
		    ssp = slaxStringConcat(slax_data, M_XPATH, &$1);
		    slaxTrace("xpath: %s", ssp ? ssp->ss_token : "");
		    STACK_CLEAR($1);
		    KEYWORDS_ON();
		    $$ = ssp;
		}

	| xpath_lite_value L_UNDERSCORE xpl_expr
		{
		    slax_string_t *ssp, *newp;

		    if ($3) {
			newp = slaxStringConcat(slax_data, M_XPATH, &$3);

			/* Append the new xp_path_expr to the list */
			for (ssp = $1; ssp; ssp = ssp->ss_next) {
			    if (ssp->ss_next == NULL) {
				ssp->ss_next = newp;
				break;
			    }
			}
		    }

		    ssp = $1;
		    $1 = NULL;	/* Save from free() */
		    STACK_CLEAR($1);
		    KEYWORDS_ON();
		    $$ = ssp;
		}
	;

/* -------------------------------------------------------------------
 * XSLT productions from the XSLT REC
 */

xs_pattern :
	xs_location_path_pattern
		{ $$ = $1; }

	| xs_pattern L_VBAR xs_location_path_pattern
		{ $$ = STACK_LINK($1); }
	;

xs_location_path_pattern :
	L_SLASH xs_relative_path_pattern_optional
		{ $$ = STACK_LINK($1); }

	| xs_id_key_pattern xs_id_key_pattern_trailer
		{ $$ = STACK_LINK($1); }

	| xs_dslash_optional xs_relative_path_pattern
		{ $$ = STACK_LINK($1); }
	;

xs_id_key_pattern_trailer :
	/* empty */
		{ $$ = NULL; }

	| xs_single_or_double_slash xs_relative_path_pattern_optional
		{ $$ = $1; }
	;

xs_dslash_optional :
	/* empty */
		{ $$ = NULL; }

	| L_DSLASH
		{ $$ = $1; }
	;

xs_id_key_pattern :
	K_ID L_OPAREN xp_expr L_CPAREN
		{ $$ = STACK_LINK($1); }

	| K_KEY L_OPAREN xp_expr L_COMMA xp_expr L_CPAREN
		{ $$ = STACK_LINK($1); }
	;

xs_relative_path_pattern_optional :
	/* empty */
		{ $$ = NULL; }

	| xs_relative_path_pattern
		{ $$ = $1; }
	;

xs_relative_path_pattern :
	xs_step_pattern 	
		{ $$ = $1; }

	| xs_relative_path_pattern xs_single_or_double_slash xs_step_pattern
		{ $$ = STACK_LINK($1); }
	;

xs_single_or_double_slash :
	L_SLASH
		{ $$ = $1; }

	| L_DSLASH
		{ $$ = $1; }
	;

xs_step_pattern :
	xpc_axis_specifier_optional xpc_node_test xs_predicate_list
		{ $$ = STACK_LINK($1); }
	;

xs_predicate_list :
	/* empty */
		{ $$ = NULL; }

	| xs_predicate_list xpc_predicate
		{ $$ = STACK_LINK($1); }
	;

/* -------------------------------------------------------------------
 * XPath productions that are common to both "xp" and "xpl" are
 * called "xpc" (XPath common) and are placed here.  See the
 * description in slaxparser-xp.y for more information.
 */

/*
 * These two productions are the root of the difference between
 * "xpl" and "xp".  For the grammar to be unambiguous, we have
 * to make the 
 * The "xpl" version of xp_relative_location_path_optional is _not_
 * optional
 */

xp_relative_location_path_optional :
	/* empty */
		{ $$ = NULL; }

	| xpc_relative_location_path
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}
	;

xpl_relative_location_path_optional :
	xpc_relative_location_path
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}
	;


/*
 * The following production is the source of all our shift/reduce
 * conflicts.  If the "q_name" is replaced with a specific token
 * type (T_FUNCTION_NAME), the conflicts disappear.  The problem
 * is that "foo:goo($x)" and "foo:goo/foo:hoo" are ambiguous
 * until you see the open paren.  I could fake this in the lexer
 * to allow a pristine grammar, but this seems like a worse
 * hack that just adding "shift_reduce=nn" to the invocation of
 * yacc.sh in the Makefile.
 */

xpc_function_call :
	T_FUNCTION_NAME L_OPAREN xpc_argument_list_optional L_CPAREN
		{
		    KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}

	| xs_id_key_pattern
		{ $$ = $1; }
	;

xpc_argument_list_optional :
	/* empty */
		{ $$ = NULL; }

	| xpc_argument_list
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}
	;

xpc_argument_list :
	xp_expr
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}

	| xpc_argument_list L_COMMA xp_expr
		{
		    KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}
	;

xpc_axis_specifier_optional :
	/* empty */
		{ $$ = NULL; }

	| xpc_abbreviated_axis_specifier
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}
	;

xpc_predicate_list :
	/* empty */
		{ $$ = NULL; }

	| xpc_predicate_list xpc_predicate
		{
		    KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}
	;

xpc_predicate :
	L_OBRACK xp_expr L_CBRACK
		{
		    KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}
	;

xpc_relative_location_path :
	xpc_step
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}

	| xpc_relative_location_path L_SLASH xpc_step
		{
		    KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}

	| xpc_abbreviated_relative_location_path
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}
	;

xpc_step :
	xpc_axis_specifier_optional xpc_node_test xpc_predicate_list
		{
		    KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}

	| xpc_abbreviated_step
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}
	;

xpc_abbreviated_absolute_location_path :
	L_DSLASH xpc_relative_location_path
		{
		    KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}
	;

xpc_abbreviated_relative_location_path :
	xpc_relative_location_path L_DSLASH xpc_step
		{
		    KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}
	;

xpc_literal :
	T_QUOTED
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}
	;

xpc_literal_optional :
	/* Empty */
		{ $$ = NULL; }

	| xpc_literal
		{ $$ = $1; }
	;

xpc_full_number :
	xpc_number_sign_optional xpc_number
		{
		    $$ = STACK_LINK($1);
		}
	;

xpc_number_sign_optional :
	/* Empty */
		{ $$ = NULL; }

	| L_MINUS
		{ $$ = $1; }
	| L_PLUS
		{ $$ = NULL; }
	;

xpc_number :
	T_NUMBER
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}

	| T_NUMBER L_DOT T_NUMBER
		{
		    KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}

	| L_DOT T_NUMBER
		{
		    KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}
	;

xpc_variable_reference :
	T_VAR
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}
	;

xpc_node_test :
	xpc_name_test	
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}

	| xpc_node_type L_OPAREN L_CPAREN
		{
		    KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}

	| K_PROCESSING_INSTRUCTION L_OPAREN xpc_literal_optional L_CPAREN
		{
		    KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}
	;

xpc_name_test :
	q_name
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}

	| L_ASTERISK /* L_STAR */
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}
	;

xpc_node_type :
	K_COMMENT
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}

	| K_TEXT
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}

	| K_NODE
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}
	;

xpc_abbreviated_step :
	L_DOT
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}

	| L_DOTDOT
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}
	;

xpc_abbreviated_axis_specifier :
	L_AT
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}
	;

/*
 * This production gives a mechanism for slaxparser-xp.y to refer to
 * the "real" expr production, rather than the "lite" version.
 */
xpc_expr :
	xp_expr
		{ $$ = $1; }
	;

/*
 * There's another ambiguity between SLAX and attribute value templates
 * involving the use of ">" in an expression, like:
 *
 *      <problem child=one > two>;
 *
 * The conflict is between ">" as an operator in an XPath expression
 * and the closing character of a tag.  Fortunately, an expression
 * like this makes no sense in the attribute context, so we simply
 * block the use of relational operators inside "xpl" expressions.
 */

xp_relational_expr :
	xp_additive_expr
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}

	| xp_relational_expr L_LESS xp_additive_expr
		{
		    KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}

	| xp_relational_expr L_GRTR xp_additive_expr
		{
		    KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}

	| xp_relational_expr L_LESSEQ xp_additive_expr
		{
		    KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}

	| xp_relational_expr L_GRTREQ xp_additive_expr
		{
		    KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}
	;

xpl_relational_expr :
	xpl_additive_expr
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}
	;
