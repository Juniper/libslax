%%
#line 3 "slaxparser-tail.y"

/*
 * $Id: slaxparser-tail.y,v 1.1 2006/11/01 21:27:20 phil Exp $
 *
 * Copyright (c) 2006-2010, Juniper Networks, Inc.
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

/*
 * Return a better class of error message
 */
char *
slaxExpectingError (const char *token, int yystate, int yychar)
{
    const int MAX_EXPECT = 5;
    char buf[BUFSIZ], *cp = buf, *ep = buf + sizeof(buf);
    int expect = 0, expecting[MAX_EXPECT + 1];
    int yyn = yypact[yystate];
    int i;
    int yytype;
    int start, stop;
    int v_state = 0;

    if (!(YYPACT_NINF < yyn && yyn <= YYLAST))
	return NULL;

    yytype = YYTRANSLATE(yychar);

    start = yyn < 0 ? -yyn : 0;
    stop = YYLAST - yyn + 1;
    if (stop > YYNTOKENS)

	stop = YYNTOKENS;

    for (i = start; i < stop; ++i) {
	if (yycheck[i + yyn] == i && i != YYTERROR) {
	    if (i > YYTRANSLATE(V_FIRST) && i < YYTRANSLATE(V_LAST))
		v_state = i;

	    if (slaxTokenNameFancy[i] == NULL)
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

