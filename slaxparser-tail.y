%%
#line 2 "slaxparser-tail.y"

/*
 * $Id: slaxparser-tail.y,v 1.1 2006/11/01 21:27:20 phil Exp $
 *
 * Copyright (c) 2006-2008, Juniper Networks, Inc.
 * All rights reserved.
 * See ./Copyright for the status of this software
 *
 * Tail of slaxparser.y
 */

const char *keywordString[YYNTOKENS];

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
