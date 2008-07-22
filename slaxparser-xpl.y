/*
 * $Id: slaxparser-xp.y,v 1.1 2006/11/01 21:27:20 phil Exp $
 *
 * Copyright (c) 2006-2008, Juniper Networks, Inc.
 * All rights reserved.
 * See ./Copyright for the status of this software
 *
 * XPath expression rules.  These are mostly direct from the XPath REC.
 *
 * This file contains the XPath rules that must be repeated for
 * the limited XPath expressions available for use in attributes.
 *
 * The combination of XPath and SLAX leads to one potential
 * ambiguity where the syntax for attributes conflicts with
 * the XPath expression "/", which returns the entire document.
 * The SLAX syntax for attributes looks like:
 *
 *     <element attribute = "foo">;
 *     <another one = "foo" two = "goo">;
 *
 * Unlike XSLT, quoted strings are text, but the normal SLAX syntax
 * for variables is supported:
 *
 *     <yet another = $this>;
 *
 * SLAX includes the XSLT attribute value template capability inside
 * its expression syntax:
 *
 *     <more feature = "than " _ $user _ "thought " _ possible>;
 *
 * (which is equivalent to: feature="than {$user} thought {possible}")
 *
 * Then conflict occurs with cases like:
 *
 *     <problem here=/ there=foo>;
 *
 * where "there" makes a conflict between "here=/there" and "there=foo".
 * The fortunate reality is that this case is completely meaningless, since
 * the act of putting the entire document into an attribute is ludicrous.
 * We avoid this by doctoring the syntax for XPath's "expr" production to
 * disallow "/" as an option.
 *
 * Unfortunately, this puts us in the position of repeating a boatload
 * of productions, which makes a maintanence nightmare.  We automate
 * this by letting "make" reproduce this files twice in the middle of
 * the "slaxparser.y" output file, once for "xpl_" rules (normal XPath)
 * and once for "xpl_" rules (XPath lite).  The productions in
 * slaxparser-head.y funnel into the appropriate set of productions
 * and it all Just Works (tm).
 */

xpl_location_path :
	xpc_relative_location_path
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}

	| xpl_absolute_location_path
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}
	;

xpl_absolute_location_path :
	L_SLASH xpl_relative_location_path_optional
		{
		    KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}

	| xpc_abbreviated_absolute_location_path
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}
	;

xpl_expr :
	xpl_or_expr
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}
	;

xpl_primary_expr :
	L_OPAREN xpc_expr L_CPAREN
		{
		    KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}

	| xpc_variable_reference
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}

	| xpc_literal
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}

	| xpc_number
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}

	| xpc_function_call
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}
	;

xpl_union_expr :
	xpl_path_expr
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}

	| xpl_union_expr L_VBAR xpl_path_expr
		{
		    KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}
	;

xpl_path_expr :
	xpl_location_path
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}

	| xpl_filter_expr
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}

	| xpl_filter_expr L_SLASH xpc_relative_location_path
		{
		    KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}

	| xpl_filter_expr L_DSLASH xpc_relative_location_path
		{
		    KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}
	;

xpl_filter_expr :
	xpl_primary_expr
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}

	| xpl_filter_expr xpc_predicate
		{
		    KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}
	;

xpl_or_expr :
	xpl_and_expr
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}

	| xpl_or_expr or_operator xpl_and_expr
		{
		    KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}
	;

xpl_and_expr :
	xpl_equality_expr
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}

	| xpl_and_expr and_operator xpl_equality_expr
		{
		    KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}
	;

xpl_equality_expr :
	xpl_relational_expr
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}

	| xpl_equality_expr equals_operator xpl_relational_expr
		{
		    KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}

	| xpl_equality_expr L_NOTEQUALS xpl_relational_expr
		{
		    KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}
	;

xpl_additive_expr :
	xpl_multiplicative_expr
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}

	| xpl_additive_expr L_PLUS xpl_multiplicative_expr
		{
		    KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}

	| xpl_additive_expr L_MINUS xpl_multiplicative_expr
		{
		    KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}
	;

xpl_multiplicative_expr :
	xpl_unary_expr
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}

	| xpl_multiplicative_expr L_STAR xpl_unary_expr
		{
		    KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}

	| xpl_multiplicative_expr K_DIV xpl_unary_expr
		{
		    KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}

	| xpl_multiplicative_expr K_MOD xpl_unary_expr
		{
		    KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}
	;

xpl_unary_expr :
	xpl_union_expr
		{
		    KEYWORDS_OFF();
		    $$ = $1;
		}

	| L_MINUS xpl_unary_expr
		{
		    KEYWORDS_OFF();
		    $$ = STACK_LINK($1);
		}
	;
