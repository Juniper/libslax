%{
#line 2 "slaxparser.y"
%}

/*
 * $Id: slaxparser.y,v 1.3 2008/05/21 02:06:12 phil Exp $
 *
 * Copyright (c) 2006-2010, Juniper Networks, Inc.
 * All rights reserved.
 * See ../Copyright for the status of this software
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
 * These tokens are used for validity tests to see where we are in
 * the syntax tree.  We use these to simplify error messages.  These
 * tokens are never really parsed, and the lexer will never return them.
 */
%token V_FIRST			/* First of the "V_*" tokens */

%token V_TOP_LEVEL		/* A top-level statement */
%token V_BLOCK_LEVEL		/* A block-level statement */
%token V_XPATH			/* An XPath expression */
%token V_PATTERN		/* An XPath pattern */

%token V_LAST			/* Last of the "V_*" tokens */

/*
 * Literal tokens which may _not_ preceed the multiplication operator
 */
%token L_ASSIGN			/* ':=' */
%token L_AT			/* '@' */
%token L_CBRACE			/* '}' */
%token L_COMMA			/* ',' */
%token L_COLON			/* ':' */
%token L_DAMPER			/* '&&' */
%token L_DCOLON			/* '::' */
%token L_DEQUALS		/* '==' */
%token L_DOTDOT			/* '..' */
%token L_DOTDOTDOT		/* '...' */
%token L_DSLASH			/* '//' */
%token L_DVBAR			/* '||' */
%token L_EOS			/* ';' */
%token L_EQUALS			/* '=' */
%token L_GRTR			/* '>' */
%token L_GRTREQ			/* '>=' */
%token L_LESS			/* '<' */
%token L_LESSEQ			/* '<=' */
%token L_MINUS			/* '-' */
%token L_NOT			/* '!' */
%token L_NOTEQUALS		/* '!=' */
%token L_OBRACE			/* '{' */
%token L_OBRACK			/* '[' */
%token L_OPAREN			/* '(' */
%token L_PLUS			/* '+' */
%token L_PLUSEQUALS		/* '+=' */
%token L_QUESTION		/* '?' */
%token L_SLASH			/* '/' */
%token L_STAR			/* '*' */
%token L_UNDERSCORE		/* '_' */
%token L_VBAR			/* '|' */

/*
 * Keyword tokens
 */
%token K_APPEND			/* 'append' */
%token K_APPLY_IMPORTS		/* 'apply-imports' */
%token K_APPLY_TEMPLATES	/* 'apply-templates' */
%token K_ATTRIBUTE		/* 'attribute' */
%token K_ATTRIBUTE_SET		/* 'attribute-set' */
%token K_CALL			/* 'call' */
%token K_CASE_ORDER		/* 'case-order' */
%token K_CDATA_SECTION_ELEMENTS	/* 'cdata-section-elements' */
%token K_COMMENT		/* 'comment' */
%token K_COPY_NODE		/* 'copy-node' */
%token K_COPY_OF		/* 'copy-of' */
%token K_COUNT			/* 'count' */
%token K_DATA_TYPE		/* 'data-type' */
%token K_DECIMAL_FORMAT		/* 'decimal-format' */
%token K_DECIMAL_SEPARATOR	/* 'decimal-separator' */
%token K_DIE			/* 'die' */
%token K_DIGIT			/* 'digit' */
%token K_DOCTYPE_PUBLIC		/* 'doctype-public' */
%token K_DOCTYPE_SYSTEM		/* 'doctype-system' */
%token K_ELEMENT		/* 'element' */
%token K_ELSE			/* 'else' */
%token K_ENCODING		/* 'encoding' */
%token K_EXCLUDE		/* 'exclude' */
%token K_EXPR			/* 'expr' */
%token K_EXTENSION		/* 'extension' */
%token K_FALLBACK		/* 'fallback' */
%token K_FORMAT			/* 'format' */
%token K_FOR			/* 'for' */
%token K_FOR_EACH		/* 'for-each' */
%token K_FROM			/* 'from' */
%token K_FUNCTION		/* 'function' */
%token K_GROUPING_SEPARATOR	/* 'grouping-separator' */
%token K_GROUPING_SIZE		/* 'grouping-size' */
%token K_ID			/* 'id' */
%token K_IF			/* 'if' */
%token K_IMPORT			/* 'import' */
%token K_INCLUDE		/* 'include' */
%token K_INDENT			/* 'indent' */
%token K_INFINITY		/* 'infinity' */
%token K_KEY			/* 'key' */
%token K_LANGUAGE		/* 'language' */
%token K_LETTER_VALUE		/* 'letter-value' */
%token K_LEVEL			/* 'level' */
%token K_MATCH			/* 'match' */
%token K_MEDIA_TYPE		/* 'media-type' */
%token K_MESSAGE		/* 'message' */
%token K_MINUS_SIGN		/* 'minus-sign' */
%token K_MODE			/* 'mode' */
%token K_MVAR			/* 'mvar' */
%token K_NAN			/* 'nan' */
%token K_NODE			/* 'node' */
%token K_NS			/* 'ns' */
%token K_NS_ALIAS		/* 'ns-alias' */
%token K_NS_TEMPLATE		/* 'ns-template' */
%token K_NUMBER			/* 'number' */
%token K_OMIT_XML_DECLARATION	/* 'omit-xml-declaration' */
%token K_ORDER			/* 'order' */
%token K_OUTPUT_METHOD		/* 'output-method' */
%token K_PARAM			/* 'param' */
%token K_PATTERN_SEPARATOR	/* 'pattern-separator' */
%token K_PERCENT		/* 'percent' */
%token K_PER_MILLE		/* 'per-mille' */
%token K_PRESERVE_SPACE		/* 'preserve-space' */
%token K_PRIORITY		/* 'priority' */
%token K_PROCESSING_INSTRUCTION /* 'processing-instruction' */
%token K_RESULT			/* 'result' */
%token K_SET			/* 'set' */
%token K_SORT			/* 'sort' */
%token K_STANDALONE		/* 'standalone' */
%token K_STRIP_SPACE		/* 'strip-space' */
%token K_TEMPLATE		/* 'template' */
%token K_TERMINATE		/* 'terminate' */
%token K_TEXT			/* 'text' */
%token K_TRACE			/* 'trace' */
%token K_UEXPR			/* 'uexpr' */
%token K_USE_ATTRIBUTE_SETS	/* 'use-attribute-set' */
%token K_VALUE			/* 'value' */
%token K_VAR			/* 'var' */
%token K_VERSION		/* 'version' */
%token K_WHILE			/* 'while' */
%token K_WITH			/* 'with' */
%token K_ZERO_DIGIT		/* 'zero-digit' */

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
%token T_AXIS_NAME		/* a built-in axis name */
%token T_BARE			/* a bare word string (bare-word) */
%token T_FUNCTION_NAME		/* a function name (bare-word) */
%token T_NUMBER			/* a number (4) */
%token T_QUOTED			/* a quoted string ("foo") */
%token T_VAR			/* a variable name ($foo) */

/*
 * Magic tokens (used for special purposes).  M_* tokens are used to
 * trigger explicit behavior in the parser.  These tokens are never
 * really parsed, and the lexer will never return them, except for
 * M_ERROR.
 */
%token M_SEQUENCE		/* A $x...$y sequence */
%token M_CONCAT			/* underscore -> concat mapping */
%token M_TERNARY		/* "?:" -> <xsl:choose> mapping */
%token M_TERNARY_END		/* End of a M_TERNARY chain */
%token M_ERROR			/* An error was detected in the lexer */
%token M_XPATH			/* Building an XPath expression */
%token M_PARSE_FULL		/* Parse a slax document */
%token M_PARSE_SLAX		/* Parse a SLAX-style XPath expression */
%token M_PARSE_XPATH		/* Parse an XPath expression */
%token M_PARSE_PARTIAL		/* Parse partial SLAX contents */

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
#include <libexslt/exslt.h>

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
#define yydebug slaxYyDebug	/* Make debug flag parser specific */
#define YYFPRINTF slaxLog2	/* Log via our function */

static int
slaxParseIsSlax (slax_data_t *sdp)
{
    return (sdp->sd_parse == M_PARSE_FULL || sdp->sd_parse == M_PARSE_SLAX);
}

static int
slaxParseIsXpath (slax_data_t *sdp)
{
    return (sdp->sd_parse == M_PARSE_XPATH);
}

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
 *
 */
#define STACK_CLEAR2(_x, _y) slaxStackClear2(slax_data, &(_x), yyvsp, &(_y))

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
	M_PARSE_FULL stylesheet
		{ $$ = STACK_CLEAR($1); }

	| M_PARSE_XPATH xpath_expression
		{
		    slax_data->sd_xpath = $2;
		    $2 = NULL;	/* Avoid double free */
		    $$ = STACK_CLEAR($1);
		}

	| M_PARSE_SLAX xpath_expression
		{
		    slax_data->sd_xpath = $2;
		    $2 = NULL;	/* Avoid double free */
		    $$ = STACK_CLEAR($1);
		}

	| M_PARSE_PARTIAL partial_list
		{ $$ = STACK_CLEAR($1); }
	;

partial_list :
	/* empty */
		{ $$ = NULL; }

	| partial_list partial_stmt
		{
		    ALL_KEYWORDS_ON();
		    $$ = NULL;
		}
	;

partial_stmt :
	version_stmt
		{ $$ = NULL; }

	| ns_stmt
		{ $$ = NULL; }

	| slax_stmt
		{ $$ = NULL; }

	| block
		{ $$ = NULL; }

	| apply_imports_stmt
		{ $$ = NULL; }

	| apply_templates_stmt
		{ $$ = NULL; }

	| attribute_stmt
		{ $$ = NULL; }

	| call_stmt
		{ $$ = NULL; }

	| comment_stmt
		{ $$ = NULL; }

	| copy_node_stmt
		{ $$ = NULL; }

	| copy_of_stmt
		{ $$ = NULL; }

	| expr_stmt
		{ $$ = NULL; }

	| fallback_stmt
		{ $$ = NULL; }

	| for_stmt
		{ $$ = NULL; }

	| for_each_stmt
		{ $$ = NULL; }

	| if_stmt
		{ $$ = NULL; }

	| message_stmt
		{ $$ = NULL; }

	| processing_instruction_stmt
		{ $$ = NULL; }

	| number_stmt
		{ $$ = NULL; }

	| result_stmt
		{ $$ = NULL; }

	| set_mvar_stmt
		{ $$ = NULL; }

	| sort_stmt
		{ $$ = NULL; }

	| terminate_stmt
		{ $$ = NULL; }

	| trace_stmt
		{ $$ = NULL; }

	| uexpr_stmt
		{ $$ = NULL; }

	| use_attribute_stmt
		{ $$ = NULL; }

	| while_stmt
		{ $$ = NULL; }
	;

error_conditions :
	error L_EOS
		{
		    yyclearin;
		    yyerrok;
		    $$ = NULL;
		}

	| error L_OBRACE rack_up_the_errors_list L_CBRACE
		{
		    yyerror("error recovery ignores input until this point");
		    yyclearin;
		    yyerrok;
		    $$ = NULL;
		}
	;

rack_up_the_errors_list :
	/* Empty */
		{ $$ = NULL; }

	| rack_up_the_errors_list rack_up_the_errors
		{ $$ = NULL; }
	;

rack_up_the_errors :
	error
		{ $$ = NULL; }

	| L_OBRACK rack_up_the_errors_list L_CBRACE
		{ $$ = NULL; }
	;

stylesheet :
	version_stmt ns_list slax_stmt_list
		{ $$ = NULL; }
	;

version_stmt :
	K_VERSION version_number L_EOS
		{
		    ALL_KEYWORDS_ON();
		    $$ = STACK_CLEAR($1);
		}
	;

version_number :
	T_NUMBER
		{
		    slaxVersionMatch(slax_data, $1->ss_token);
		    $$ = STACK_CLEAR($1);
		}
	;

ns_list :
	/* empty */
		{ $$ = NULL; }

	| ns_list ns_stmt
		{
		    ALL_KEYWORDS_ON();
		    $$ = NULL;
		}
	;

ns_stmt :
	ns_decl
		{ $$ = NULL; }

	| ns_alias_stmt
		{ $$ = NULL; }
	;

ns_decl :
	K_NS T_BARE
		{
		    /* Stash the namespace */
		    slax_data->sd_ns = $2;
		    ALL_KEYWORDS_ON();
		    $$ = NULL;
		}
	    ns_option_list L_EQUALS T_QUOTED L_EOS
		{
		    slax_data->sd_ns = NULL;

		    slaxNsAdd(slax_data, $2->ss_token, $6->ss_token);

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

ns_decl_local :
	K_NS T_BARE
		{
		    /* Stash the namespace */
		    slax_data->sd_ns = $2;
		    ALL_KEYWORDS_ON();
		    $$ = NULL;
		}
	    ns_option_list_local L_EQUALS T_QUOTED L_EOS
		{
		    slax_data->sd_ns = NULL;

		    slaxNsAdd(slax_data, $2->ss_token, $6->ss_token);

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

ns_option_list :
	/* empty */
		{ $$ = NULL; }

	| ns_option_list ns_option
		{ $$ = NULL; }
	;

ns_option :
	K_EXCLUDE
		{
		    slaxAttribExtend(slax_data,
				     ATT_EXCLUDE_RESULT_PREFIXES,
				     slax_data->sd_ns->ss_token);
		    $$ = STACK_CLEAR($1);
		}

	| K_EXTENSION
		{
		    slaxAttribExtend(slax_data,
				     ATT_EXTENSION_ELEMENT_PREFIXES,
				     slax_data->sd_ns->ss_token);
		    $$ = STACK_CLEAR($1);
		}
	;

ns_option_list_local :
	/* empty */
		{ $$ = NULL; }

	| ns_option_list_local ns_option_local
		{ $$ = NULL; }
	;

ns_option_local :
	K_EXTENSION
		{
		    slaxAttribExtendXsl(slax_data,
					ATT_EXTENSION_ELEMENT_PREFIXES,
					slax_data->sd_ns->ss_token);
		    $$ = STACK_CLEAR($1);
		}
	;

ns_alias_stmt :
	K_NS_ALIAS T_BARE T_BARE L_EOS
		{
		    ALL_KEYWORDS_ON();
		    slaxNamespaceAlias(slax_data, $2, $3);
		    $$ = STACK_CLEAR($1);
		}
	;

slax_stmt_list :
	/* empty */
		{ $$ = NULL; }

	| L_EOS
		{ $$ = NULL; }

	| slax_stmt_list slax_stmt
		{
		    ALL_KEYWORDS_ON();
		    $$ = NULL;
		}
	;

slax_stmt :
	V_TOP_LEVEL
		{ $$ = NULL; }

	| param_decl
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

	| output_method_stmt
		{ $$ = NULL; }

	| decimal_format_stmt
		{ $$ = NULL; }

	| key_stmt
		{ $$ = NULL; }

	| attribute_set_stmt
		{ $$ = NULL; }

	| element_stmt
		{ $$ = NULL; }

	| match_template
		{ $$ = NULL; }

	| named_template
		{ $$ = NULL; }

	| function_definition
		{ $$ = NULL; }

	| error_conditions
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
		    slaxElementAddString(slax_data, $1->ss_token,
				   ATT_ELEMENTS, $2);
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
		    slaxElementAddString(slax_data, $1->ss_token,
				       ATT_ELEMENTS, $2);

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
		    SLAX_KEYWORDS_OFF();
		    slaxElementPush(slax_data, ELT_PARAM,
				    ATT_NAME, $2->ss_token + 1);
		    $$ = NULL;
		}
	    initial_value
		{
		    ALL_KEYWORDS_ON();
		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($4);
		}

	| K_PARAM T_VAR L_ASSIGN
		{
		    SLAX_KEYWORDS_OFF();
		    slaxElementPush(slax_data, ELT_PARAM,
				    ATT_NAME, $2->ss_token + 1);
		    $$ = NULL;
		}
	    initial_value
		{
		    ALL_KEYWORDS_ON();
		    slaxAvoidRtf(slax_data);
		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($4);
		}
	;

var_or_mvar :
	K_VAR
		{ $$ = $1; }
	| K_MVAR
		{ $$ = $1; }
	;

var_decl :
	var_or_mvar T_VAR L_EOS
		{
		    slaxElementPush(slax_data, ELT_VARIABLE,
					   ATT_NAME, $2->ss_token + 1);

		    if ($1->ss_ttype == K_MVAR)
			slaxMvarCreateSvar(slax_data, $2->ss_token + 1);

		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		}

	| var_or_mvar T_VAR L_EQUALS
		{
		    SLAX_KEYWORDS_OFF();
		    slaxElementPush(slax_data, ELT_VARIABLE,
				    ATT_NAME, $2->ss_token + 1);
		    $$ = NULL;
		}
	    initial_value
		{
		    ALL_KEYWORDS_ON();
		    if ($1->ss_ttype == K_MVAR)
			slaxMvarCreateSvar(slax_data, $2->ss_token + 1);

		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($4);
		}

	| var_or_mvar T_VAR L_ASSIGN
		{
		    SLAX_KEYWORDS_OFF();
		    slaxElementPush(slax_data, ELT_VARIABLE,
				    ATT_NAME, $2->ss_token + 1);
		    $$ = NULL;
		}
	    initial_value
		{
		    ALL_KEYWORDS_ON();

		    if ($1->ss_ttype == K_MVAR)
			slaxMvarCreateSvar(slax_data, $2->ss_token + 1);
		    else
			slaxAvoidRtf(slax_data);

		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($4);
		}
	;

initial_value :
	xpath_value L_EOS
		{
		    slaxAttribAdd(slax_data, SAS_SELECT, ATT_SELECT, $1);
		    $$ = STACK_CLEAR($1);
		}

	| block
		{
		    ALL_KEYWORDS_ON();
		    $$ = NULL;
		}

	| element_stmt
		{
		    ALL_KEYWORDS_ON();
		    $$ = NULL;
		}

	| call_stmt
		{
		    ALL_KEYWORDS_ON();
		    $$ = NULL;
		}
	;


set_mvar_stmt :
	set_mvar_preface initial_value
		{
		    ALL_KEYWORDS_ON();
		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		}
	;

set_mvar_preface :
	K_SET T_VAR L_EQUALS
		{
		    xmlNodePtr nodep;

		    SLAX_KEYWORDS_OFF();
		    nodep = slaxElementPush(slax_data, ELT_SET_VARIABLE,
					    ATT_NAME, $2->ss_token + 1);
		    if (nodep) {
			slaxSetSlaxNs(slax_data, nodep, TRUE);
			slaxMvarAddSvarName(slax_data, nodep);
		    }

		    $$ = STACK_CLEAR($1);
		}

	| K_APPEND T_VAR L_PLUSEQUALS
		{
		    xmlNodePtr nodep;

		    SLAX_KEYWORDS_OFF();
		    nodep = slaxElementPush(slax_data, ELT_APPEND_TO_VARIABLE,
					    ATT_NAME, $2->ss_token + 1);
		    if (nodep) {
			slaxSetSlaxNs(slax_data, nodep, TRUE);
			slaxMvarAddSvarName(slax_data, nodep);
		    }

		    $$ = STACK_CLEAR($1);
		}
	;

match_template :
	K_MATCH xs_pattern
		{
		    ALL_KEYWORDS_ON();
		    if (slaxElementPush(slax_data, ELT_TEMPLATE, NULL, NULL))
			slaxAttribAddString(slax_data, ATT_MATCH,
					    $2, SSF_QUOTES);
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
	match_preamble_list block_preamble_list block_stmt_list
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
		    slaxAttribAddString(slax_data, ATT_MODE, $2, 0);
		    $$ = STACK_CLEAR($1);
		}
	;

priority_stmt :
	K_PRIORITY xpc_full_number L_EOS
		{
		    slaxAttribAddString(slax_data, ATT_PRIORITY, $2, 0);
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
            opt_match_stmt
		{
		    if ($3)
			slaxAttribAddString(slax_data, ATT_MATCH,
					    $3, SSF_QUOTES);
		    $$ = NULL;
		}
	    named_template_arguments block
		{
		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($2, $4);
		}
	;

template_name_stmt :
	template_name
		{ $$ = $1; }

	| K_TEMPLATE template_name
		{
		    ALL_KEYWORDS_ON();
		    $$ = STACK_CLEAR2($1, $2);
		}
	;

opt_match_stmt :
	/* empty */
		{ $$ = NULL; }
	| K_MATCH xs_pattern
		{ $$ = STACK_CLEAR2($1, $2); }
	| K_MATCH T_FUNCTION_NAME
		{ $$ = STACK_CLEAR2($1, $2); }
	;

named_template_arguments :
	/* empty */
		{ $$ = NULL; }

	| L_OPAREN named_template_argument_list_optional L_CPAREN
		{ $$ = STACK_CLEAR($1); }
	;

named_template_argument_list_optional :
	/* empty */
		{ $$ = NULL; }

	| named_template_argument_list
		{ $$ = NULL; }
	;

named_template_argument_list :
	named_template_argument_decl
		{ $$ = NULL; }

	| named_template_argument_list L_COMMA named_template_argument_decl
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
		    SLAX_KEYWORDS_OFF();
		    $$ = NULL;
		}
	    xpath_value
		{
		    xmlNodePtr nodep;

		    ALL_KEYWORDS_ON();
		    nodep = slaxElementAdd(slax_data, ELT_PARAM,
					   ATT_NAME, $1->ss_token + 1);
		    if (nodep) {
			nodePush(slax_data->sd_ctxt, nodep);
			slaxAttribAdd(slax_data, SAS_SELECT, ATT_SELECT, $4);
			nodePop(slax_data->sd_ctxt);
		    }
		    /* XXX else error */
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($3);
		}
	;

function_definition :
	K_FUNCTION template_name
		{
		    xmlNodePtr nodep;
		    nodep = slaxElementPush(slax_data, ELT_FUNCTION,
					    ELT_NAME, $2->ss_token);
		    if (nodep)
			slaxSetFuncNs(slax_data, nodep);
		    $$ = NULL;
		}
	    function_def_arguments block
		{ 
		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($3);
		}
	;

function_def_arguments :
	L_OPAREN function_argument_list_optional L_CPAREN
		{ $$ = STACK_CLEAR($1); }
	;

function_argument_list_optional :
	/* empty */
		{ $$ = NULL; }

	| function_argument_list
		{ $$ = NULL; }
	;

function_argument_list :
	function_argument_decl
		{ $$ = NULL; }

	| function_argument_list L_COMMA function_argument_decl
		{ $$ = STACK_CLEAR($1); }
	;

function_argument_decl :
	named_template_argument_decl
		{ $$ = NULL; }
	;

block :
	L_OBRACE
		{
		    ALL_KEYWORDS_ON();
		    $$ = NULL;
		}
	    block_contents L_CBRACE
		{
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($2);
		}
	;

block_contents :
	block_preamble_list block_stmt_list
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
	ns_decl_local
		{ $$ = NULL; }
	;

block_stmt_list :
	/* empty */
		{ $$ = NULL; }

	| block_stmt_list block_stmt
		{
		    ALL_KEYWORDS_ON();
		    $$ = NULL;
		}
	;

block_stmt :
	V_BLOCK_LEVEL
		{ $$ = NULL; }

	| apply_imports_stmt
		{ $$ = NULL; }

	| apply_templates_stmt
		{ $$ = NULL; }

	| attribute_stmt
		{ $$ = NULL; }

	| call_stmt
		{ $$ = NULL; }

	| comment_stmt
		{ $$ = NULL; }

	| copy_node_stmt
		{ $$ = NULL; }

	| copy_of_stmt
		{ $$ = NULL; }

	| element_stmt
		{ $$ = NULL; }

	| expr_stmt
		{ $$ = NULL; }

	| fallback_stmt
		{ $$ = NULL; }

	| for_stmt
		{ $$ = NULL; }

	| for_each_stmt
		{ $$ = NULL; }

	| if_stmt
		{ $$ = NULL; }

	| message_stmt
		{ $$ = NULL; }

	| param_decl
		{ $$ = NULL; }

	| processing_instruction_stmt
		{ $$ = NULL; }

	| number_stmt
		{ $$ = NULL; }

	| result_stmt
		{ $$ = NULL; }

	| set_mvar_stmt
		{ $$ = NULL; }

	| sort_stmt
		{ $$ = NULL; }

	| terminate_stmt
		{ $$ = NULL; }

	| trace_stmt
		{ $$ = NULL; }

	| uexpr_stmt
		{ $$ = NULL; }

	| use_attribute_stmt
		{ $$ = NULL; }

	| var_decl
		{ $$ = NULL; }

	| while_stmt
		{ $$ = NULL; }

	| L_EOS
		{ $$ = STACK_CLEAR($1); }

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
		    slaxElementPush(slax_data, ELT_APPLY_TEMPLATES,
					    NULL, NULL);
		    $$ = NULL;
		}
	    xpath_expr_optional apply_template_arguments
		{
		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($2);
		}
	;

xpath_expr_optional :
	/* empty */
		{ $$ = NULL; }

	| xpath_expr
		{
		    slaxAttribAdd(slax_data, SAS_XPATH, ATT_SELECT, $1);
		    $$ = STACK_CLEAR($1);
		}
	;

expr_stmt :
	K_EXPR xpath_value L_EOS
		{
		    slaxElementXPath(slax_data, $2, TRUE, FALSE);
		    $$ = STACK_CLEAR($1);
		}
	;

uexpr_stmt :
	K_UEXPR xpath_value L_EOS
		{
		    slaxElementXPath(slax_data, $2, TRUE, TRUE);
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

	| L_UNDERSCORE
		{ $$ = $1; }
	;

apply_template_arguments :
	L_EOS /* This is the semi-colon to end the statement */
		{ $$ = STACK_CLEAR($1); }

	| L_OBRACE
		{
		    ALL_KEYWORDS_ON();
		    $$ = NULL;
		}
	    apply_template_sub_stmt_list_optional L_CBRACE
		{
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($2);
		}
	;

apply_template_sub_stmt_list_optional :
	/* empty */
		{ $$ = NULL; }

	| apply_template_sub_stmt_list
		{ $$ = NULL; }
	;

apply_template_sub_stmt_list :
	apply_template_sub_stmt
		{ $$ = NULL; }

	| apply_template_sub_stmt_list apply_template_sub_stmt
		{
		    ALL_KEYWORDS_ON();
		    $$ = NULL;
		}
	;

apply_template_sub_stmt :
	sort_stmt
		{ $$ = NULL; }

	| call_argument_braces_member
		{ $$ = NULL; }
	;

call_arguments :
	call_arguments_parens_style call_arguments_braces_style
		{ $$ = NULL; }
	;

call_arguments_parens_style :
	/* empty */
		{ $$ = NULL; }

	| L_OPAREN call_argument_list_optional L_CPAREN
		{ $$ = STACK_CLEAR($1); }
	;

call_argument_list_optional :
	/* empty */
		{ $$ = NULL; }

	| call_argument_list
		{ $$ = NULL; }
	;

call_argument_list :
	call_argument_member
		{ $$ = NULL; }

	| call_argument_list L_COMMA call_argument_member
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
			slaxAttribAdd(slax_data, SAS_NONE, ATT_SELECT, $1);
			nodePop(slax_data->sd_ctxt);
		    }
		    /* XXX else error */
		    $$ = STACK_CLEAR($1);
		}

	| T_VAR L_EQUALS
		{
		    SLAX_KEYWORDS_OFF();
		    $$ = NULL;
		}
	    xpath_value
		{
		    xmlNodePtr nodep;

		    nodep = slaxElementAdd(slax_data, ELT_WITH_PARAM,
				   ATT_NAME, $1->ss_token + 1);
		    if (nodep) {
			nodePush(slax_data->sd_ctxt, nodep);
			slaxAttribAdd(slax_data, SAS_SELECT, ATT_SELECT, $4);
			nodePop(slax_data->sd_ctxt);
		    }
		    /* XXX else error */

		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($3);
		}
	;

call_arguments_braces_style :
	L_EOS /* This is the semi-colon to end the statement */
		{ $$ = STACK_CLEAR($1); }

	| L_OBRACE
		{
		    ALL_KEYWORDS_ON();
		    $$ = NULL;
		}
	    call_argument_braces_list_optional L_CBRACE
		{
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($2);
		}
	;

call_argument_braces_list_optional :
	call_argument_braces_list
		{ $$ = NULL; }
	;

call_argument_braces_list :
	call_argument_braces_member
		{ $$ = NULL; }

	| call_argument_braces_list call_argument_braces_member
		{ $$ = STACK_CLEAR($1); }
	;

call_argument_braces_member :
	K_WITH T_VAR L_EOS
		{
		    xmlNodePtr nodep;

		    ALL_KEYWORDS_ON();
		    nodep = slaxElementAdd(slax_data, ELT_WITH_PARAM,
				   ATT_NAME, $2->ss_token + 1);
		    if (nodep) {
			nodePush(slax_data->sd_ctxt, nodep);
			slaxAttribAdd(slax_data, SAS_NONE, ATT_SELECT, $2);
			nodePop(slax_data->sd_ctxt);
		    }
		    /* XXX else error */
		    $$ = STACK_CLEAR($1);
		}

	| K_WITH T_VAR L_EQUALS
		{
		    xmlNodePtr nodep;

		    SLAX_KEYWORDS_OFF();
		    nodep = slaxElementAdd(slax_data, ELT_WITH_PARAM,
				   ATT_NAME, $2->ss_token + 1);
		    if (nodep)
			nodePush(slax_data->sd_ctxt, nodep);

		    $$ = NULL;
		}
	    initial_value
		{
		    ALL_KEYWORDS_ON();
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
	K_COMMENT
		{
		    slaxElementPush(slax_data, ELT_COMMENT, NULL, NULL);
		    $$ = NULL;
		} 
	    simple_contents
		{
		    ALL_KEYWORDS_ON();
		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($2);
		}
	;

copy_of_stmt :
	K_COPY_OF xpath_expr L_EOS
		{
		    xmlNodePtr nodep;

		    nodep = slaxElementAdd(slax_data, ELT_COPY_OF, NULL, NULL);
		    if (nodep) {
			nodePush(slax_data->sd_ctxt, nodep);
			slaxAttribAdd(slax_data, SAS_XPATH, ATT_SELECT, $2);
			nodePop(slax_data->sd_ctxt);
		    }
		    /* XXX else error */
		    $$ = STACK_CLEAR($1);
		}
	;

result_stmt :
	K_RESULT
		{
		    xmlNodePtr nodep;

		    SLAX_KEYWORDS_OFF();

		    nodep = slaxElementPush(slax_data, ELT_RESULT, NULL, NULL);
		    if (nodep)
			slaxSetFuncNs(slax_data, nodep);
		    $$ = NULL;
		}
	    initial_value
		{
		    ALL_KEYWORDS_ON();
		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($2);
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
		    SLAX_KEYWORDS_OFF();
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
		    slaxElementXPath(slax_data, $2, FALSE, FALSE);
		    slaxElementClose(slax_data);
		    $$ = STACK_CLEAR($1);
		}

	| K_ELEMENT xpath_value L_EOS
		{
		    xmlNodePtr nodep;
		    nodep = slaxElementPush(slax_data, ELT_ELEMENT,
					    NULL, NULL);
		    if (nodep) {
			slaxAttribAddValue(slax_data, ATT_NAME, $2);
			slaxElementPop(slax_data);
		    }

		    $$ = STACK_CLEAR($1);
		}

	| K_ELEMENT xpath_value L_OBRACE
		{
		    xmlNodePtr nodep;

		    nodep = slaxElementPush(slax_data, ELT_ELEMENT,
					    NULL, NULL);
		    if (nodep)
			slaxAttribAddValue(slax_data, ATT_NAME, $2);
		    $$ = NULL;
		}
	    element_stmt_contents L_CBRACE
		{
		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($4);
		}
	;

element_stmt_contents :
	element_stmt_preamble_list block_stmt_list
		{ $$ = NULL; }
	;

element_stmt_preamble_list :
	/* empty */
		{ $$ = NULL; }

	| element_stmt_preamble_list element_stmt_preamble_stmt
		{
		    ALL_KEYWORDS_ON();
		    $$ = NULL;
		}
	;

element_stmt_preamble_stmt :
	ns_decl_local
		{ $$ = NULL; }

	| ns_template
		{ $$ = NULL; }
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
		    ALL_KEYWORDS_OFF();
		    $$ = NULL;
		}
	    xpath_lite_value
		{
		    slaxAttribAddValue(slax_data, $1->ss_token, $4);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($3);
		}
	;

attribute_stmt :
	K_ATTRIBUTE xpath_value L_OBRACE
		{
		    xmlNodePtr nodep;

		    nodep = slaxElementPush(slax_data, ELT_ATTRIBUTE,
					    NULL, NULL);
		    slaxAttribAddValue(slax_data, ATT_NAME, $2);
		    $$ = NULL;
		}
	    attribute_stmt_contents L_CBRACE
		{
		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($4);
		}
	;

attribute_stmt_contents :
	attribute_stmt_preamble_list block_stmt_list
		{ $$ = NULL; }
	;

attribute_stmt_preamble_list :
	/* empty */
		{ $$ = NULL; }

	| attribute_stmt_preamble_list attribute_stmt_preamble_stmt
		{
		    ALL_KEYWORDS_ON();
		    $$ = NULL;
		}
	;

attribute_stmt_preamble_stmt :
	ns_decl_local
		{ $$ = NULL; }

	| ns_template
		{ $$ = NULL; }
	;

ns_template :
	K_NS_TEMPLATE xpath_value L_EOS
		{
		    slaxAttribAddValue(slax_data, ATT_NAMESPACE, $2);
		    $$ = STACK_CLEAR($1);
		}
	;

for_each_stmt :
	K_FOR_EACH L_OPAREN xpath_expr_dotdotdot L_CPAREN
		{
		    xmlNodePtr nodep;
		    nodep = slaxElementPush(slax_data, ELT_FOR_EACH,
					    NULL, NULL);
		    slaxAttribAdd(slax_data, SAS_XPATH, ATT_SELECT, $3);

		    if ($3->ss_ttype == M_SEQUENCE)
			slaxSetSlaxNs(slax_data,
				      slax_data->sd_ctxt->node, FALSE);

		    $$ = NULL;
		}
	    block
		{
		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($5);
		}
	;

for_stmt :
	K_FOR T_VAR L_OPAREN xpath_expr_dotdotdot L_CPAREN
		{
		    /*
		     * The for loop is a little tricky.  We are creating
		     * the following logic:
		     * var $slax-dot = .;
		     * for-each (xpath_expr) {
		     *     var $var = .;
		     *     for-each ($slax-dot) {
		     *         ...
		     *     }
		     * }
		     * This allows "." to remain unchanged.
		     */
		    static unsigned for_count;
		    char buf[BUFSIZ];

		    /* var $slax-dot-xxx = . */
		    snprintf(buf, sizeof(buf), "%s%u",
			     FOR_VARIABLE_PREFIX, ++for_count);
		    slaxElementPush(slax_data, ELT_VARIABLE,
				    ATT_NAME, buf + 1);
		    slaxAttribAddLiteral(slax_data, ATT_SELECT, ".");
		    slaxElementPop(slax_data);

		    /* Outer for-each loop */
		    slaxElementPush(slax_data, ELT_FOR_EACH, NULL, NULL);
		    slaxAttribAdd(slax_data, SAS_XPATH, ATT_SELECT, $4);

		    if ($4->ss_ttype == M_SEQUENCE)
			slaxSetSlaxNs(slax_data,
				      slax_data->sd_ctxt->node, FALSE);

		    /* Inner variable */
		    slaxElementPush(slax_data, ELT_VARIABLE,
				    ATT_NAME, $2->ss_token + 1);
		    slaxAttribAddLiteral(slax_data, ATT_SELECT, ".");
		    slaxElementPop(slax_data);

		    /* Inner for-each loop */
		    slaxElementPush(slax_data, ELT_FOR_EACH, ATT_SELECT, buf);

		    $$ = NULL;
		}
	    block
		{
		    slaxElementPop(slax_data); /* Inner for-each loop */
		    slaxElementPop(slax_data); /* Outer for-each loop */
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($6);
		}
	;

while_stmt :
	K_WHILE L_OPAREN xpath_expr L_CPAREN
		{
		    xmlNodePtr nodep;
		    nodep = slaxElementPush(slax_data, ELT_WHILE,
					    NULL, NULL);
		    slaxAttribAdd(slax_data, SAS_XPATH, ATT_TEST, $3);

		    if (nodep)
			slaxSetSlaxNs(slax_data, nodep, TRUE);

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
	K_IF L_OPAREN xpath_expr L_CPAREN
		{
		    slaxElementPush(slax_data, ELT_CHOOSE, NULL, NULL);
		    slaxElementPush(slax_data, ELT_WHEN, NULL, NULL);
		    slaxAttribAdd(slax_data, SAS_XPATH, ATT_TEST, $3);
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
	K_ELSE K_IF L_OPAREN xpath_expr L_CPAREN 
		{
		    slaxElementPush(slax_data, ELT_WHEN, NULL, NULL);
		    slaxAttribAdd(slax_data, SAS_XPATH, ATT_TEST, $4);
		    $$ = NULL;
		}
	    block
		{
		    slaxElementPop(slax_data); /* Pop when */
		    $$ = STACK_CLEAR($1);
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
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($2);
		}
	;

message_stmt :
	K_MESSAGE
		{
		    slaxElementPush(slax_data, ELT_MESSAGE, NULL, NULL);
		    $$ = NULL;
		}
	    simple_contents
		{
		    ALL_KEYWORDS_ON();
		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($2);
		}
	;

trace_stmt :
	K_TRACE
		{
		    xmlNodePtr nodep;

		    nodep = slaxElementPush(slax_data, ELT_TRACE, NULL, NULL);
		    if (nodep)
			slaxSetSlaxNs(slax_data, nodep, TRUE);
		    $$ = NULL;
		}
	    initial_value
		{
		    ALL_KEYWORDS_ON();
		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($2);
		}
	;

terminate_stmt :
	terminate_keyword
		{
		    if (slaxElementPush(slax_data, ELT_MESSAGE, NULL, NULL))
			slaxAttribAddLiteral(slax_data, ATT_TERMINATE, "yes");
		    $$ = NULL;
		}
	    simple_contents
		{
		    ALL_KEYWORDS_ON();
		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($2);
		}
	;

terminate_keyword:
	K_TERMINATE
		{ $$ = $1; }
	| K_DIE
		{ $$ = $1; }
	  ;

simple_contents :
	L_EOS
		{ $$ = STACK_CLEAR($1); }

	| xpath_value L_EOS
		{
		    slaxElementXPath(slax_data, $1, FALSE, FALSE);
		    $$ = STACK_CLEAR($1);
		}

	| block
		{ $$ = STACK_CLEAR($1); }
	;

attribute_set_stmt :
	K_ATTRIBUTE_SET q_name L_OBRACE
		{
		    ALL_KEYWORDS_ON();
		    slaxElementPush(slax_data, ELT_ATTRIBUTE_SET,
					   ATT_NAME, $2->ss_token);
		    $$ = NULL;
		}
	    attribute_set_sub_stmt_list_optional L_CBRACE
		{
		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($4);
		}
	;

attribute_set_sub_stmt_list_optional :
	/* empty */
		{ $$ = NULL; }

	| attribute_set_sub_stmt_list
		{ $$ = NULL; }
	;

attribute_set_sub_stmt_list :
	attribute_set_sub_stmt
		{ $$ = NULL; }

	| attribute_set_sub_stmt_list attribute_set_sub_stmt
		{ $$ = NULL; }
	;

attribute_set_sub_stmt :
	use_attribute_stmt
		{ $$ = NULL; }

	| attribute_stmt
		{ $$ = NULL; }
	;

use_attribute_stmt :
	K_USE_ATTRIBUTE_SETS use_attribute_list L_EOS
		{
		    ALL_KEYWORDS_ON();
		    slaxAttribAddString(slax_data, ATT_USE_ATTRIBUTE_SETS,
					$2, 0);
		    $$ = STACK_CLEAR($1);
		}
	;

use_attribute_list :
	q_name
		{ $$ = STACK_LINK($1); }

	| use_attribute_list q_name
		{ $$ = STACK_LINK($1); }
	;

output_method_stmt :
	output_method_intro output_method_block
		{
		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		}
	;

output_method_intro :
	K_OUTPUT_METHOD T_BARE
		{
		    ALL_KEYWORDS_ON();
		    slaxElementPush(slax_data, ELT_OUTPUT,
					   ATT_METHOD, $2->ss_token);
		    $$ = STACK_CLEAR($1);
		}

	| K_OUTPUT_METHOD
		{
		    ALL_KEYWORDS_ON();
		    slaxElementPush(slax_data, ELT_OUTPUT, NULL, NULL);
		    $$ = STACK_CLEAR($1);
		}
	;

output_method_block :
	L_EOS
		{ $$ = STACK_CLEAR($1); }

	| L_OBRACE output_method_block_sub_stmt_list_optional L_CBRACE
		{ $$ = STACK_CLEAR($1); }
	;

output_method_block_sub_stmt_list_optional :
	/* empty */
		{ $$ = NULL; }

	| output_method_block_sub_stmt_list
		{ $$ = NULL; }
	;

output_method_block_sub_stmt_list :
	output_method_block_sub_stmt
		{ $$ = NULL; }

	| output_method_block_sub_stmt_list output_method_block_sub_stmt
		{ $$ = NULL; }
	;

output_method_block_sub_stmt :
	L_EOS
		{ $$ = STACK_CLEAR($1); }

	| K_VERSION data_value L_EOS
		{
		    slaxAttribAddLiteral(slax_data,
					 $1->ss_token, $2->ss_token);
		    $$ = STACK_CLEAR($1);
		}

	| K_ENCODING data_value L_EOS
		{
		    slaxAttribAddLiteral(slax_data,
					 $1->ss_token, $2->ss_token);
		    $$ = STACK_CLEAR($1);
		}

	| K_OMIT_XML_DECLARATION data_value L_EOS
		{
		    slaxAttribAddLiteral(slax_data,
					 $1->ss_token, $2->ss_token);
		    $$ = STACK_CLEAR($1);
		}

	| K_STANDALONE data_value L_EOS
		{
		    slaxAttribAddLiteral(slax_data,
					 $1->ss_token, $2->ss_token);
		    $$ = STACK_CLEAR($1);
		}

	| K_DOCTYPE_PUBLIC data_value L_EOS
		{
		    slaxAttribAddLiteral(slax_data,
					 $1->ss_token, $2->ss_token);
		    $$ = STACK_CLEAR($1);
		}

	| K_DOCTYPE_SYSTEM data_value L_EOS
		{
		    slaxAttribAddLiteral(slax_data,
					 $1->ss_token, $2->ss_token);
		    $$ = STACK_CLEAR($1);
		}

	| K_CDATA_SECTION_ELEMENTS cdata_section_element_list L_EOS
		{
		    ALL_KEYWORDS_ON();
		    slaxAttribAddString(slax_data, $1->ss_token,
					$2, 0);
		    $$ = STACK_CLEAR($1);
		}

	| K_INDENT data_value L_EOS
		{
		    slaxAttribAddLiteral(slax_data,
					 $1->ss_token, $2->ss_token);
		    $$ = STACK_CLEAR($1);
		}

	| K_MEDIA_TYPE data_value L_EOS
		{
		    slaxAttribAddLiteral(slax_data,
					 $1->ss_token, $2->ss_token);
		    $$ = STACK_CLEAR($1);
		}

	;

cdata_section_element_list :
	q_name
		{ $$ = STACK_LINK($1); }

	| cdata_section_element_list q_name
		{ $$ = STACK_LINK($1); }
	;

data_value :
	T_QUOTED
		{
		    ALL_KEYWORDS_ON();
		    $$ = $1;
		}
	| T_BARE
		{
		    ALL_KEYWORDS_ON();
		    $$ = $1;
		}
	;



decimal_format_stmt :
	K_DECIMAL_FORMAT T_BARE
		{
		    ALL_KEYWORDS_ON();
		    slaxElementPush(slax_data, $1->ss_token,
					   ATT_NAME, $2->ss_token);
		    $$ = NULL;
		}
	    decimal_format_block
		{
		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($3);
		}
	;

decimal_format_block :
	L_EOS
		{ $$ = STACK_CLEAR($1); }

	| L_OBRACE decimal_format_block_sub_stmt_list_optional L_CBRACE
		{ $$ = STACK_CLEAR($1); }
	;

decimal_format_block_sub_stmt_list_optional :
	/* empty */
		{ $$ = NULL; }

	| decimal_format_block_sub_stmt_list
		{ $$ = NULL; }
	;

decimal_format_block_sub_stmt_list :
	decimal_format_block_sub_stmt
		{ $$ = NULL; }

	| decimal_format_block_sub_stmt_list decimal_format_block_sub_stmt
		{ $$ = NULL; }
	;

decimal_format_block_sub_stmt :
	L_EOS
		{ $$ = STACK_CLEAR($1); }

	| K_DECIMAL_SEPARATOR data_value L_EOS
		{
		    slaxAttribAddLiteral(slax_data,
					 $1->ss_token, $2->ss_token);
		    $$ = STACK_CLEAR($1);
		}

	| K_GROUPING_SEPARATOR data_value L_EOS
		{
		    slaxAttribAddLiteral(slax_data,
					 $1->ss_token, $2->ss_token);
		    $$ = STACK_CLEAR($1);
		}

	| K_INFINITY data_value L_EOS
		{
		    slaxAttribAddLiteral(slax_data,
					 $1->ss_token, $2->ss_token);
		    $$ = STACK_CLEAR($1);
		}

	| K_MINUS_SIGN data_value L_EOS
		{
		    slaxAttribAddLiteral(slax_data,
					 $1->ss_token, $2->ss_token);
		    $$ = STACK_CLEAR($1);
		}

	| K_NAN data_value L_EOS
		{
		    slaxAttribAddLiteral(slax_data,
					 ATT_NAN, $2->ss_token);
		    $$ = STACK_CLEAR($1);
		}

	| K_PERCENT data_value L_EOS
		{
		    slaxAttribAddLiteral(slax_data,
					 $1->ss_token, $2->ss_token);
		    $$ = STACK_CLEAR($1);
		}

	| K_PER_MILLE data_value L_EOS
		{
		    slaxAttribAddLiteral(slax_data,
					 $1->ss_token, $2->ss_token);
		    $$ = STACK_CLEAR($1);
		}

	| K_ZERO_DIGIT data_value L_EOS
		{
		    slaxAttribAddLiteral(slax_data,
					 $1->ss_token, $2->ss_token);
		    $$ = STACK_CLEAR($1);
		}

	| K_DIGIT data_value L_EOS
		{
		    slaxAttribAddLiteral(slax_data,
					 $1->ss_token, $2->ss_token);
		    $$ = STACK_CLEAR($1);
		}

	| K_PATTERN_SEPARATOR data_value L_EOS
		{
		    slaxAttribAddLiteral(slax_data,
					 $1->ss_token, $2->ss_token);
		    $$ = STACK_CLEAR($1);
		}

;

number_stmt :
	K_NUMBER L_EOS
		{
		    ALL_KEYWORDS_ON();
		    if (slaxElementPush(slax_data, $1->ss_token, NULL, NULL))
			slaxElementPop(slax_data);

		    $$ = STACK_CLEAR($1);
		}

	| K_NUMBER xpath_value L_EOS
		{
		    if (slaxElementPush(slax_data, $1->ss_token, NULL, NULL)) {
			slaxAttribAddString(slax_data, ATT_VALUE,
					    $2, SSF_QUOTES);
			slaxElementPop(slax_data);
		    }

		    $$ = STACK_CLEAR($1);
		}

	| K_NUMBER xpath_value L_OBRACE
		{
		    if (slaxElementPush(slax_data, $1->ss_token, NULL, NULL))
			slaxAttribAddString(slax_data, ATT_VALUE,
					    $2, SSF_QUOTES);
		    $$ = NULL;
		}
	    number_expression_details_optional L_CBRACE
		{
		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($4);
		}

	| K_NUMBER L_OBRACE
		{
		    ALL_KEYWORDS_ON();
		    slaxElementPush(slax_data, $1->ss_token, NULL, NULL);
		    $$ = NULL;
		}
	    number_constructed_details_optional L_CBRACE
		{
		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($3);
		}
	;

number_expression_details_optional :
	/* empty */
		{ $$ = NULL; }

	| number_expression_details_list
		{ $$ = NULL; }
	;

number_expression_details_list :
	number_expression_detail
		{ $$ = NULL; }

	| number_expression_details_list number_expression_detail
		{ $$ = NULL; }
	;

number_expression_detail :
	number_conversion_stmt
		{ $$ = NULL; }
	;

number_constructed_details_optional :
	/* empty */
		{ $$ = NULL; }

	| number_constructed_details_list
		{ $$ = NULL; }
	;

number_constructed_details_list :
	number_constructed_detail
		{ $$ = NULL; }

	| number_constructed_details_list number_constructed_detail
		{ $$ = NULL; }
	;

number_constructed_detail :
	number_conversion_stmt
		{ $$ = NULL; }
	
	| K_LEVEL data_value L_EOS
		{
		    slaxAttribAddLiteral(slax_data,
					 $1->ss_token, $2->ss_token);
		    $$ = STACK_CLEAR($1);
		}

	| K_FROM xpath_expr L_EOS
		{
		    slaxAttribAdd(slax_data, SAS_XPATH, $1->ss_token, $2);
		    $$ = STACK_CLEAR($1);
		}

	| K_COUNT xpath_expr L_EOS
		{
		    slaxAttribAdd(slax_data, SAS_XPATH, $1->ss_token, $2);
		    $$ = STACK_CLEAR($1);
		}
	;

number_conversion_stmt :
	K_FORMAT xpath_value L_EOS
		{
		    slaxAttribAddValue(slax_data, $1->ss_token, $2);
		    $$ = STACK_CLEAR($1);
		}

	| K_LETTER_VALUE xpath_value L_EOS
		{
		    /*
		     * As of libxslt 1.1.26, the "letter-value" attribute is
		     * not implemented.
		     */
		    slaxAttribAddValue(slax_data, $1->ss_token, $2);
		    $$ = STACK_CLEAR($1);
		}

	| K_GROUPING_SIZE xpath_value L_EOS
		{
		    slaxAttribAddValue(slax_data, ATT_GROUPING_SIZE, $2);
		    $$ = STACK_CLEAR($1);
		}

	| K_GROUPING_SEPARATOR xpath_value L_EOS
		{
		    slaxAttribAddValue(slax_data, ATT_GROUPING_SEPARATOR, $2);
		    $$ = STACK_CLEAR($1);
		}

	| K_LANGUAGE xpath_value L_EOS
		{
		    /*
		     * As of libxslt 1.1.26, the "lang" attribute is
		     * not implemented.
		     */
		    slaxAttribAddValue(slax_data, ATT_LANG, $2);
		    $$ = STACK_CLEAR($1);
		}
	;
      
processing_instruction_stmt :
	K_PROCESSING_INSTRUCTION xpath_value L_EOS
		{
		    if (slaxElementPush(slax_data, $1->ss_token, NULL, NULL)) {
			slaxAttribAddValue(slax_data, ATT_VALUE, $2);
			slaxElementPop(slax_data);
		    }
		    $$ = STACK_CLEAR($1);
		}

	| K_PROCESSING_INSTRUCTION xpath_value L_OBRACE
		{
		    ALL_KEYWORDS_ON();
		    if (slaxElementPush(slax_data, $1->ss_token, NULL, NULL))
			slaxAttribAddValue(slax_data, ATT_NAME, $2);
		    $$ = NULL;
		}
	    block_contents L_CBRACE
		{
		    ALL_KEYWORDS_ON();
		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($4);
		}
	;

key_stmt :
	K_KEY T_BARE
		{
		    ALL_KEYWORDS_ON();
		    slaxElementPush(slax_data, $1->ss_token,
				    ATT_NAME, $2->ss_token);
		    $$ = NULL;
		}
	    L_OBRACE key_sub_stmt_list_optional L_CBRACE
		{
		    ALL_KEYWORDS_ON();
		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($3);
		}
	;
	    

key_sub_stmt_list_optional :
	/* empty */
		{ $$ = NULL; }

	| key_sub_stmt_list
		{ $$ = NULL; }
	;

key_sub_stmt_list :
	key_sub_stmt
		{ $$ = NULL; }

	| key_sub_stmt_list key_sub_stmt
		{ $$ = NULL; }
	;

key_sub_stmt :
	K_MATCH xpath_expr L_EOS
		{
		    slaxAttribAdd(slax_data, SAS_XPATH, $1->ss_token, $2);
		    $$ = STACK_CLEAR($1);
		}

	| K_VALUE xpath_expr L_EOS
		{
		    slaxAttribAdd(slax_data, SAS_XPATH, ATT_USE, $2);
		    $$ = STACK_CLEAR($1);
		}
	;

copy_node_stmt :
	K_COPY_NODE
		{
		    ALL_KEYWORDS_ON();
		    slaxElementPush(slax_data, ELT_COPY, NULL, NULL);
		    $$ = NULL;
		}
	    copy_node_rest
		{
		    ALL_KEYWORDS_ON();
		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($2);
		}
	;

copy_node_rest :
	L_EOS
		{ $$ = STACK_CLEAR($1); }

	| block
		{ $$ = STACK_CLEAR($1); }
	;

sort_stmt :
	K_SORT L_EOS
		{
		    if (slaxElementPush(slax_data, $1->ss_token, NULL, NULL)) {
			slaxRelocateSort(slax_data);
			slaxElementPop(slax_data);
		    }

		    $$ = STACK_CLEAR($1);
		}

	| K_SORT xpath_value L_EOS
		{
		    if (slaxElementPush(slax_data, $1->ss_token, NULL, NULL)) {
			slaxAttribAddString(slax_data, ATT_SELECT,
					    $2, SSF_QUOTES);
			slaxRelocateSort(slax_data);
			slaxElementPop(slax_data);
		    }

		    $$ = STACK_CLEAR($1);
		}

	| K_SORT xpath_value L_OBRACE
		{
		    if (slaxElementPush(slax_data, $1->ss_token, NULL, NULL))
			slaxAttribAddString(slax_data, ATT_SELECT,
					    $2, SSF_QUOTES);
		    $$ = NULL;
		}
	    sort_sub_stmt_list_optional L_CBRACE
		{
		    slaxRelocateSort(slax_data);
		    slaxElementPop(slax_data);

		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($4);
		}
	| K_SORT L_OBRACE
		{
		    ALL_KEYWORDS_ON();

		    slaxElementPush(slax_data, $1->ss_token, NULL, NULL);
		    $$ = NULL;
		}
	    sort_sub_stmt_list_optional L_CBRACE
		{
		    slaxRelocateSort(slax_data);
		    slaxElementPop(slax_data);

		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($3);
		}
	;

sort_sub_stmt_list_optional :
	/* empty */
		{ $$ = NULL; }

	| sort_sub_stmt_list
		{ $$ = NULL; }
	;

sort_sub_stmt_list :
	sort_sub_stmt
		{ $$ = NULL; }

	| sort_sub_stmt_list sort_sub_stmt
		{ $$ = NULL; }
	;

sort_sub_stmt :
	L_EOS
		{ $$ = STACK_CLEAR($1); }

	| K_LANGUAGE xpath_value L_EOS
		{
		    /*
		     * As of libxslt 1.1.26, the "lang" attribute is
		     * not implemented.
		     */
		    slaxAttribAddValue(slax_data, ATT_LANG, $2);
		    $$ = STACK_CLEAR($1);
		}

	| K_DATA_TYPE data_value L_EOS
		{
		    /* "text" or "number" or qname-but-not-ncname */
		    ALL_KEYWORDS_ON();
		    slaxAttribAddValue(slax_data, $1->ss_token, $2);
		    $$ = STACK_CLEAR($1);
		}

	| K_ORDER data_value L_EOS
		{
		    /* "ascending" or "descending" */
		    ALL_KEYWORDS_ON();
		    slaxAttribAddValue(slax_data, $1->ss_token, $2);
		    $$ = STACK_CLEAR($1);
		}

	| K_CASE_ORDER data_value L_EOS
		{
		    /* "upper-first" or "lower-first" */
		    ALL_KEYWORDS_ON();
		    slaxAttribAddValue(slax_data, $1->ss_token, $2);
		    $$ = STACK_CLEAR($1);
		}
	;

fallback_stmt :
	K_FALLBACK
		{
		    slaxElementPush(slax_data, $1->ss_token, NULL, NULL);
		    $$ = NULL;
		}
	    block
		{
		    slaxElementPop(slax_data);
		    $$ = STACK_CLEAR($1);
		    STACK_UNUSED($2);
		}
	;

/* ---------------------------------------------------------------------- */

and_operator :
	K_AND
		{
		    if (slaxParseIsXpath(slax_data)) {
			STACK_CLEAR($1);
			$$ = slaxStringLiteral("&&", L_DAMPER);
		    } else $$ = $1;
		}

	| L_DAMPER
		{
		    if (slaxParseIsSlax(slax_data)) {
			STACK_CLEAR($1);
			$$ = slaxStringLiteral("and", L_DAMPER);
		    } else $$ = $1;
		}
	;

or_operator :
	K_OR
		{
		    if (slaxParseIsXpath(slax_data)) {
			STACK_CLEAR($1);
			$$ = slaxStringLiteral("||", L_DVBAR);
		    } else $$ = $1;
		}

	| L_DVBAR
		{
		    if (slaxParseIsSlax(slax_data)) {
			STACK_CLEAR($1);
			$$ = slaxStringLiteral("or", L_DVBAR);
		    } else $$ = $1;
		}
	;

equals_operator :
	L_EQUALS
		{
		    if (slaxParseIsXpath(slax_data)) {
			STACK_CLEAR($1);
			$$ = slaxStringLiteral("==", L_DEQUALS);
		    } else $$ = $1;
		}

	| L_DEQUALS
		{
		    if (slaxParseIsSlax(slax_data)) {
			STACK_CLEAR($1);
			$$ = slaxStringLiteral("=", L_DEQUALS);
		    } else $$ = $1;
		}
	;

/*
 * Use "xpath_expr" and "xpath_value" to clear the keyword flags
 * automatically.  We can rely on slaxYylex turning them on for
 * us as keywords are recognized, but the end-of-expression
 * detection relys on the next token (L_EOS) so xp_* productions
 * can play with flags in way that prevent us from having L_EOS
 * turn the keywords back on automatically.  But putting this
 * into a production, we know it will be fired off at an appropriate
 * time.
 */

xpath_expr :
	xpath_expression
		{
		    ALL_KEYWORDS_ON();
		    $$ = $1;
		}
	;

xpath_expr_dotdotdot :
	xpath_expression
		{
		    ALL_KEYWORDS_ON();
		    $$ = $1;
		}

	| xpath_expression L_DOTDOTDOT xpath_expression
		{
		    slax_string_t *res[7];

		    ALL_KEYWORDS_ON();

		    /* Build all the parts */
		    res[0] = slaxStringLiteral(
					SLAX_PREFIX ":" FUNC_BUILD_SEQUENCE,
					T_FUNCTION_NAME);
		    res[1] = slaxStringLiteral("(", L_OPAREN);
		    res[2] = $1;
		    res[3] = slaxStringLiteral(",", L_COMMA);
		    res[4] = $3;
		    res[5] = slaxStringLiteral(")", L_CPAREN);
		    res[6] = NULL;

		    /* Now assemble them */
		    $$ = slaxStringLink(slax_data, res, &res[6]);
		    slaxStringFree($2);
		    $1 = $2 = $3 = NULL;
		    $$->ss_ttype = M_SEQUENCE;
		}
    	;

xpath_value :
	xpath_expression
		{
		    ALL_KEYWORDS_ON();
		    slaxLog("xpath: %s", $1->ss_token);
		    $$ = $1;
		}
	;

xpath_expression :
	V_XPATH
		{ $$ = NULL; }

	| xp_expr
		{ $$ = $1; }
	;

xpath_lite_value :
	xpl_expr
		{
		    slaxLog("xpath: %s", $1->ss_token);
		    $$ = $1;
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
	K_ID L_OPAREN xpath_expression L_CPAREN
		{ $$ = STACK_LINK($1); }

	| K_KEY L_OPAREN xpath_expression L_COMMA xpath_expression L_CPAREN
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
 * These two productions are the root of the difference between "xpl"
 * and "xp".  The "xpl" version of xp_relative_location_path_optional
 * is _not_ optional
 */

xp_relative_location_path_optional :
	/* empty */
		{ $$ = NULL; }

	| xpc_relative_location_path
		{
		    SLAX_KEYWORDS_OFF();
		    $$ = $1;
		}
	;

xpl_relative_location_path_optional :
	xpc_relative_location_path
		{
		    SLAX_KEYWORDS_OFF();
		    $$ = $1;
		}
	;

xpc_function_call :
	T_FUNCTION_NAME L_OPAREN xpc_argument_list_optional L_CPAREN
		{
		    SLAX_KEYWORDS_OFF();

		    /* If we're turning XPath into SLAX, handle "..." */
		    if (slaxParseIsXpath(slax_data)) {
			if (slaxWriteRedoFunction(slax_data,
						  $1->ss_token, $3)) {
			    slax_string_t *save = $3;
			    $3 = NULL;
			    STACK_CLEAR($1);
			    $$ = save;
			} else {
			    slax_string_t *newp;

			    $$ = STACK_LINK($1);

			    newp = slaxWriteRedoTernary(slax_data, $$);
			    if (newp)
				$$ = newp;
			    else {
				newp = slaxWriteRedoConcat(slax_data, $$);
				if (newp)
				    $$ = newp;
			    }
			}
		    } else {
			$$ = STACK_LINK($1);
		    }
		}

	| xs_id_key_pattern
		{ $$ = $1; }
	;

xpc_argument_list_optional :
	/* empty */
		{ $$ = NULL; }

	| xpc_argument_list
		{
		    SLAX_KEYWORDS_OFF();
		    $$ = $1;
		}
	;

xpc_argument_list :
	xpath_value
		{
		    SLAX_KEYWORDS_OFF();
		    $$ = $1;
		}

	| xpc_argument_list L_COMMA xpath_value
		{
		    SLAX_KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}
	;

xpc_axis_specifier_optional :
	/* empty */
		{ $$ = NULL; }

	| T_AXIS_NAME L_DCOLON
		{
		    slaxCheckAxisName(slax_data, $1);
		    $$ = STACK_LINK($1);
		}

	| xpc_abbreviated_axis_specifier
		{
		    SLAX_KEYWORDS_OFF();
		    $$ = $1;
		}
	;

xpc_predicate_list :
	/* empty */
		{ $$ = NULL; }

	| xpc_predicate_list xpc_predicate
		{
		    SLAX_KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}
	;

xpc_predicate :
	L_OBRACK xpath_expression L_CBRACK
		{
		    SLAX_KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}
	;

xpc_relative_location_path :
	xpc_step
		{
		    SLAX_KEYWORDS_OFF();
		    $$ = $1;
		}

	| xpc_relative_location_path L_SLASH xpc_step
		{
		    SLAX_KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}

	| xpc_abbreviated_relative_location_path
		{
		    SLAX_KEYWORDS_OFF();
		    $$ = $1;
		}
	;

xpc_step :
	xpc_axis_specifier_optional xpc_node_test xpc_predicate_list
		{
		    SLAX_KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}

	| xpc_abbreviated_step
		{
		    SLAX_KEYWORDS_OFF();
		    $$ = $1;
		}
	;

xpc_abbreviated_absolute_location_path :
	L_DSLASH xpc_relative_location_path
		{
		    SLAX_KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}
	;

xpc_abbreviated_relative_location_path :
	xpc_relative_location_path L_DSLASH xpc_step
		{
		    SLAX_KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}
	;

xpc_literal :
	T_QUOTED
		{
		    SLAX_KEYWORDS_OFF();
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
		    SLAX_KEYWORDS_OFF();
		    $$ = $1;
		}
	;

xpc_variable_reference :
	T_VAR
		{
		    SLAX_KEYWORDS_OFF();
		    $$ = $1;
		}
	;

/*
 * Using V_PATTERN here is a bit of a waste, but there are enough
 * optional production (meaning possibly empty) that when the bison
 * engine see an invalid one, it runs the default action for the
 * current state and bison magic routes us to the state matching
 * this production.  It's an odd place for two reasons: (1) it's not
 * pretty to hide the V_*-based validity tests this low in the grammar,
 * and (2) because it prevents disambiguation between XPath patterns
 * (ala "match") and XPath expressions (ala "expr"), which forces
 * slaxExpectingError() to label them both the same.  Not ideal.
 */

xpc_node_test :
	V_PATTERN
		{ $$ = NULL; }

	| xpc_name_test	
		{
		    SLAX_KEYWORDS_OFF();
		    $$ = $1;
		}

	| xpc_node_type L_OPAREN L_CPAREN
		{
		    SLAX_KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}

	| K_PROCESSING_INSTRUCTION L_OPAREN xpc_literal_optional L_CPAREN
		{
		    SLAX_KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}
	;

xpc_name_test :
	q_name
		{
		    SLAX_KEYWORDS_OFF();
		    $$ = $1;
		}

	| L_ASTERISK /* L_STAR */
		{
		    SLAX_KEYWORDS_OFF();
		    $$ = $1;
		}
	;

xpc_node_type :
	K_COMMENT
		{
		    SLAX_KEYWORDS_OFF();
		    $$ = $1;
		}

	| K_TEXT
		{
		    SLAX_KEYWORDS_OFF();
		    $$ = $1;
		}

	| K_NODE
		{
		    SLAX_KEYWORDS_OFF();
		    $$ = $1;
		}
	;

xpc_abbreviated_step :
	L_DOT
		{
		    SLAX_KEYWORDS_OFF();
		    $$ = $1;
		}

	| L_DOTDOT
		{
		    SLAX_KEYWORDS_OFF();
		    $$ = $1;
		}
	;

xpc_abbreviated_axis_specifier :
	L_AT
		{
		    SLAX_KEYWORDS_OFF();
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
	xp_concative_expr
		{
		    SLAX_KEYWORDS_OFF();
		    $$ = $1;
		}

	| xp_relational_expr L_LESS xp_concative_expr
		{
		    SLAX_KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}

	| xp_relational_expr L_GRTR xp_concative_expr
		{
		    SLAX_KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}

	| xp_relational_expr L_LESSEQ xp_concative_expr
		{
		    SLAX_KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}

	| xp_relational_expr L_GRTREQ xp_concative_expr
		{
		    SLAX_KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}
	;

xpl_relational_expr :
	xpl_concative_expr
		{
		    SLAX_KEYWORDS_OFF();
		    $$ = $1;
		}
	;
