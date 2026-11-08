%%
#line 3 "slaxparser-tail.y"

/*
 * Copyright (c) 2006-2013, 2021, Juniper Networks, Inc.
 * All rights reserved.
 * See ../Copyright for the status of this software
 *
 * Tail of slaxparser.y
 */

const char *slaxKeywordString[YYNTOKENS];
const char *slaxTokenNameFancy[YYNTOKENS];

/*
 * Return a human-readable name for a given token type
 */
const char *
slaxTokenName (int ttype)
{
    return yytname[YYTRANSLATE(ttype)];
}

/*
 * Expose YYTRANSLATE outside the bison file
 */
int
slaxTokenTranslate (int ttype)
{
    return YYTRANSLATE(ttype);
}

#ifndef YYTERROR
#define YYTERROR YYSYMBOL_YYerror /* the new enum */
#endif /* YYTERROR */

/*
 * Snapshotting the parser stack costs a copy per token; this flag
 * turns it off, so we can measure that cost.  Without a snapshot,
 * slaxExpectingError() falls back to looking at yystate alone.
 */
int slaxParserSnapStack = TRUE;

/*
 * Our yylex() wrapper (see the "#define yylex" in slaxlexer.h): copy
 * the parser's state stack into sd_snapshot_stack[], then lex the next token.
 * The buffer is reused, so the steady state is a single memcpy.
 */
int
slaxYylexSnap (slax_data_t *sdp, YYSTYPE *yylvalp,
	       const int16_t *ss_base, const int16_t *ss_top)
{
    sdp->sd_snapshot_stack_count = 0;

    if (slaxParserSnapStack && ss_base != NULL) {
	uint32_t depth = (uint32_t) (ss_top - ss_base) + 1;

	if (depth > sdp->sd_snapshot_stack_max) {
	    uint32_t new_max = sdp->sd_snapshot_stack_max;
	    if (new_max == 0)
		new_max = 64;
	    while (new_max < depth)
		new_max <<= 1;

	    int16_t *sp = xmlRealloc(sdp->sd_snapshot_stack, new_max * sizeof(*sp));
	    if (sp) {
		sdp->sd_snapshot_stack = sp;
		sdp->sd_snapshot_stack_max = new_max;
	    }
	}

	if (depth <= sdp->sd_snapshot_stack_max) {
	    memcpy(sdp->sd_snapshot_stack, ss_base, depth * sizeof(*ss_base));
	    sdp->sd_snapshot_stack_count = depth;
	}
    }

    return slaxYylex(sdp, yylvalp);
}

/*
 * Our public parser entry point: run the bison parser, then free
 * the stack snapshot.  Callers build slax_data_t in several places,
 * not all of which use slaxDataCleanup(), so we free it here.
 */
int
slaxParse (slax_data_t *sdp)
{
    int rc = yyparse(sdp);

    if (sdp->sd_snapshot_stack) {
	xmlFree(sdp->sd_snapshot_stack);
	sdp->sd_snapshot_stack = NULL;
    }
    sdp->sd_snapshot_stack_count = sdp->sd_snapshot_stack_max = 0;

    return rc;
}

/*
 * Limits for slaxTokenOk()'s simulation.  Each step either reduces
 * or decides, so SLAX_SIM_MAX_STEPS just guards against a table loop
 * we don't expect to have.
 */
#define SLAX_SIM_LOCAL_DEPTH 128 /* Simulated stack held on the C stack */
#define SLAX_SIM_MAX_STEPS 1000	/* Max reductions per simulation */

/*
 * Would 'token' (a translated symbol number) be shifted if the parser
 * read it with the snapshot stack in sd_snapshot_stack[]?  We run bison's own
 * table logic (see yybackup/yydefault/yyreduce in the generated
 * parser), doing the reductions on a scratch copy of the stack, until
 * 'token' is shifted (TRUE) or hits an error (FALSE).  Unlike looking
 * at a single state, this follows default reductions and ignores the
 * extra lookaheads that LALR state merging gives to reductions, so the
 * answer depends only on the grammar and the input, not on how the
 * tables were built.
 */
static int
slaxTokenOk (slax_data_t *sdp, int token)
{
    int16_t local[SLAX_SIM_LOCAL_DEPTH];
    int16_t *stack = local;
    uint32_t stack_max = SLAX_SIM_LOCAL_DEPTH;
    uint32_t depth = sdp->sd_snapshot_stack_count;
    int rc = FALSE;
    int steps, yyn, state;

    /* Leave room for pushes past the snapshot (empty rules) */
    if (depth + 1 > stack_max) {
	stack_max = depth + SLAX_SIM_LOCAL_DEPTH;
	stack = xmlMalloc(stack_max * sizeof(*stack));
	if (stack == NULL)
	    return FALSE;
    }

    memcpy(stack, sdp->sd_snapshot_stack, depth * sizeof(*stack));

    for (steps = 0; steps < SLAX_SIM_MAX_STEPS; steps++) {
	state = stack[depth - 1];

	yyn = yypact[state];
	if (!yypact_value_is_default(yyn)) {
	    yyn += token;
	    if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == token) {
		yyn = yytable[yyn];
		if (yyn > 0) {	/* Shift: the token is acceptable */
		    rc = TRUE;
		    break;
		}

		if (yytable_value_is_error(yyn))
		    break;

		yyn = -yyn;	/* Reduce by an explicit rule */
		goto reduce;
	    }
	}

	/* Default reduction; if there's none, it's an error */
	yyn = yydefact[state];
	if (yyn == 0)
	    break;

    reduce:
	/* Reduce by rule yyn: pop the rhs, then push the goto state */
	{
	    uint32_t len = (uint32_t) yyr2[yyn];
	    if (len >= depth)	/* Can't happen */
		break;
	    depth -= len;
	    state = stack[depth - 1];

	    int lhs = yyr1[yyn] - YYNTOKENS;
	    int idx = yypgoto[lhs] + state;

	    if (0 <= idx && idx <= YYLAST && yycheck[idx] == state)
		state = yytable[idx];
	    else
		state = yydefgoto[lhs];

	    if (depth >= stack_max) /* Empty rules can grow the stack */
		break;
	    stack[depth++] = (int16_t) state;
	}
    }

    if (stack != local)
	xmlFree(stack);

    return rc;
}

/*
 * Is 'token' a valid lookahead for 'yystate', looking only at that
 * single state's yypact row?  This is the fallback for when we have
 * no stack snapshot.  It misses tokens that are accepted only after
 * a default reduction.
 */
static int
slaxTokenOkState (int yystate, int token)
{
    int yyn = yypact[yystate];

    if (yypact_value_is_default(yyn))
	return FALSE;

    yyn += token;
    return (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == token);
}

/*
 * Return a better class of error message
 */
char *
slaxExpectingError (slax_data_t *sdp, const char *token,
		    int yystate, int yychar UNUSED)
{
#define MAX_EXPECT 5
    char buf[BUFSIZ], *cp = buf, *ep = buf + sizeof(buf);
    int expect = 0, expecting[MAX_EXPECT + 1];
    int use_stack = (sdp->sd_snapshot_stack_count > 0);
    int i, pass;
    int v_state = 0;
    char ok[YYNTOKENS];		/* Is the token acceptable?  0, 1, or 2 */

    if (!use_stack && yypact_value_is_default(yypact[yystate]))
	return NULL;

    /*
     * Find the acceptable tokens.  With a snapshot, we mark the ones
     * the error state also accepts as 2.  These are the tokens that
     * finish the innermost construct (like ']' or ';'), and are usually
     * the most useful ones to report, so they go first.  The rest are
     * typically binary operators that could continue the expression.
     */
    for (i = 0; i < YYNTOKENS; ++i) {
	ok[i] = 0;
	if (i == YYTERROR)
	    continue;

	int is_virtual = (i > YYTRANSLATE(V_FIRST) && i < YYTRANSLATE(V_LAST));

	/* Skip the simulation for tokens we won't report */
	if (!is_virtual && slaxTokenNameFancy[i] == NULL)
	    continue;

	if (!use_stack) {
	    ok[i] = slaxTokenOkState(yystate, i) ? 2 : 0;
	} else if (slaxTokenOk(sdp, i)) {
	    ok[i] = slaxTokenOkState(yystate, i) ? 2 : 1;
	}

	if (ok[i] && is_virtual)
	    v_state = i;
    }

    for (pass = 2; pass > 0 && expect <= MAX_EXPECT; pass--) {
	for (i = 0; i < YYNTOKENS; ++i) {
	    if (ok[i] != pass || slaxTokenNameFancy[i] == NULL)
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

    if (v_state) {
	if (v_state == YYTRANSLATE(V_TOP_LEVEL))
	    SNPRINTF(cp, ep, "; expected valid top-level statement");
	else if (v_state == YYTRANSLATE(V_BLOCK_LEVEL))
	    SNPRINTF(cp, ep, "; expected valid statement");
	else if (v_state == YYTRANSLATE(V_XPATH))
	    SNPRINTF(cp, ep, "; expected valid XPath expression");
	else if (v_state == YYTRANSLATE(V_PATTERN))
	    SNPRINTF(cp, ep, "; expected valid XPath expression");
	else
	    SNPRINTF(cp, ep, "; expected valid input");

    } else if (expect > 0) {
	for (i = 0; i < expect; i++) {
	    const char *pre = (i == 0) ? "; expected"
		                       : (i == expect - 1) ? " or" : ",";
	    const char *value = slaxTokenNameFancy[expecting[i]];
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
