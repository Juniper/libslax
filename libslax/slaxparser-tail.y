%%
#line 2 "slaxparser-tail.y"

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
slaxSyntaxError (slax_data_t *sdp, const char *token, int yystate, int yychar)
{
    int yyn = yypact[yystate];
    int i;
    int yytype;
    int start, stop;
    const int MAX_EXPECT = 5;
    int expect = 0, expecting[MAX_EXPECT];
    char buf[BUFSIZ], *cp = buf, *ep = buf + sizeof(buf);

    if (!(YYPACT_NINF < yyn && yyn <= YYLAST))
	return NULL;

    /*
     * If yystate is 1, then we're in our initial state and have
     * not seen any valid input.  We handle this state specifically
     * to give better error messages.
     */
    if (yystate == 1) {
	if (yychar == -1)
	    return xmlStrdup2("unexpected end-of-file found (empty input)");

	if (yychar == L_LESS)
	    return xmlStrdup2("unexpected '<'; file may be XML/XSLT");

	SNPRINTF(cp, ep, "missing 'version' statement");
	if (token)
	    SNPRINTF(cp, ep, "; '%s' in not legal", token);

	return xmlStrdup2(buf);
    }

    yytype = YYTRANSLATE(yychar);

    start = yyn < 0 ? -yyn : 0;
    stop = YYLAST - yyn + 1;
    if (stop > YYNTOKENS)
	stop = YYNTOKENS;

    for (i = start; i < stop; ++i) {
	if (yycheck[i + yyn] == i && i != YYTERROR) {
	    expecting[expect++] = i;
	    if (expect > MAX_EXPECT)
		break;
	}
    }

    if (yychar == -1) {
	if (sdp->sd_flags & SDF_OPEN_COMMENT)
	    return xmlStrdup2("unexpected end-of-file due to open comment");

	SNPRINTF(cp, ep, "unexpected end-of-file");

    } else {
	SNPRINTF(cp, ep, "unexpected input token");
	if (token)
	    SNPRINTF(cp, ep, " '%s'", token);
    }	

    if (expect && expect < MAX_EXPECT) {
	const char *pre;

	for (i = 0; i < expect; i++) {
	    pre = (i == 0) ? "; expected"
		: (i == expect - 1) ? " or" : ",";
	    SNPRINTF(cp, ep, "%s %s", pre, slaxTokenNameFancy[expecting[i]]);
	}
    }

    return xmlStrdup2(buf);
}

