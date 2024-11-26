/*
 * $Id$
 *
 * Copyright (c) 2011-2012, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * ext_bit.c -- extension functions for bit operations
 */

#include <math.h>

#include "slaxinternals.h"
#include <libslax/slax.h>

#include <libxml/xpathInternals.h>
#include <libxml/tree.h>
#include <libxslt/extensions.h>
#include <libslax/slaxdyn.h>

#define URI_BIT  "http://xml.libslax.org/bit"

static xmlChar *
extBitStringVal (xmlXPathParserContextPtr ctxt, xmlXPathObjectPtr xop,
		 int parse_the_string)
{
    unsigned long long val, v2;

    if (xop->type == XPATH_NUMBER) {
	val = xop->floatval;

	if (xop->floatval < 0 || xop->floatval >= pow(2, 64))
	    val = ULLONG_MAX;	/* No other error value we can use */

    } else if (parse_the_string && xop->type == XPATH_STRING) {
	const char *s = (const char *) xop->stringval;
	val = strtoull(s, NULL, 10);

    } else {
	/*
	 * We just want the string to be a string, so we can make
	 * libxml do the work for us.
	 */
	valuePush(ctxt, xop);
	return xmlXPathPopString(ctxt);
    }

    xmlXPathFreeObject(xop);

    xmlChar *res;
    int width;

    for (width = 0, v2 = val; v2; width++, v2 /= 2)
	continue;

    if (width == 0)		/* Gotta have one zero */
	width = 1;

    res = xmlMalloc(width + 1);
    if (res == NULL)
	return NULL;

    res[width] = '\0';
    for (width--, v2 = val; width >= 0; width--, v2 /= 2)
	res[width] = (v2 & 1) ? '1' : '0';

    return res;
}

typedef xmlChar (*slax_bit_callback_t)(xmlChar, xmlChar);

static void
extBitOperation (xmlXPathParserContextPtr ctxt, int nargs,
		 slax_bit_callback_t func, const char *name)
{
    xmlChar *lv, *rv, *res;
    int llen, rlen, width, i;
    xmlXPathObjectPtr xop;

    if (nargs != 2) {
	xmlXPathSetArityError(ctxt);
	return;
    }

    /* Pop args in reverse order */
    xop = valuePop(ctxt);
    if (xop == NULL || xmlXPathCheckError(ctxt))
	return;
    rv = extBitStringVal(ctxt, xop, FALSE);
    if (rv == NULL)
	return;

    xop = valuePop(ctxt);
    if (xop == NULL || xmlXPathCheckError(ctxt))
	return;
    lv = extBitStringVal(ctxt, xop, FALSE);
    if (lv == NULL) {
	xmlFree(rv);
	return;
    }

    llen = xmlStrlen(lv);
    rlen = xmlStrlen(rv);
    width = (llen > rlen) ? llen : rlen;

    res = xmlMalloc(width + 1);
    if (res) {
	res[width] = '\0';
	for (i = 0; i < width; i++) {
	    xmlChar lb = (i >= width - llen) ? lv[i - (width - llen)] : '0';
	    xmlChar rb = (i >= width - rlen) ? rv[i - (width - rlen)] : '0';
	    res[i] = (*func)(lb, rb);
	}
    }

    slaxLog("bit:%s:: %d [%s] -> [%s] == [%s]", name, width, lv, rv, res);

    xmlFree(lv);
    xmlFree(rv);

    xmlXPathReturnString(ctxt, res);
}

static xmlChar
extBitOpAnd (xmlChar lb, xmlChar rb)
{
    return (lb == '1' && rb == '1') ? '1' : '0';
}

static void
extBitAnd (xmlXPathParserContextPtr ctxt, int nargs)
{
    extBitOperation(ctxt, nargs, extBitOpAnd, "and");
}

static xmlChar
extBitOpOr (xmlChar lb, xmlChar rb)
{
    return (lb == '1' || rb == '1') ? '1' : '0';
}

static void
extBitOr (xmlXPathParserContextPtr ctxt, int nargs)
{
    extBitOperation(ctxt, nargs, extBitOpOr, "or");
}

static xmlChar
extBitOpNand (xmlChar lb, xmlChar rb)
{
    return (lb == '1' && rb == '1') ? '0' : '1';
}

static void
extBitNand (xmlXPathParserContextPtr ctxt, int nargs)
{
    extBitOperation(ctxt, nargs, extBitOpNand, "nand");
}

static xmlChar
extBitOpNor (xmlChar lb, xmlChar rb)
{
    return (lb == '1' || rb == '1') ? '0' : '1';
}

static void
extBitNor (xmlXPathParserContextPtr ctxt, int nargs)
{
    extBitOperation(ctxt, nargs, extBitOpNor, "nor");
}

static xmlChar
extBitOpXor (xmlChar lb, xmlChar rb)
{
    return (lb != rb) ? '1' : '0';
}

static void
extBitXor (xmlXPathParserContextPtr ctxt, int nargs)
{
    extBitOperation(ctxt, nargs, extBitOpXor, "xor");
}

static xmlChar
extBitOpXnor (xmlChar lb, xmlChar rb)
{
    return (lb == rb) ? '1' : '0';
}

static void
extBitXnor (xmlXPathParserContextPtr ctxt, int nargs)
{
    extBitOperation(ctxt, nargs, extBitOpXnor, "xnor");
}

static void
extBitNot (xmlXPathParserContextPtr ctxt, int nargs)
{
    xmlChar *res;
    int width, i;
    xmlXPathObjectPtr xop;

    if (nargs != 1) {
	xmlXPathSetArityError(ctxt);
	return;
    }

    /* Pop args in reverse order */
    xop = valuePop(ctxt);
    if (xop == NULL || xmlXPathCheckError(ctxt))
	return;
    res = extBitStringVal(ctxt, xop, FALSE);
    if (res == NULL)
	return;

    width = xmlStrlen(res);
    for (i = 0; i < width; i++) {
	xmlChar lb = res[i];
	res[i] = (lb == '0') ? '1' : '0';
    }

    xmlXPathReturnString(ctxt, res);
}

static void
extBitMask (xmlXPathParserContextPtr ctxt, int nargs)
{
    xmlChar *res;
    int width, maxw = 0;

    if (nargs != 1 && nargs != 2) {
	xmlXPathSetArityError(ctxt);
	return;
    }

    /* Pop args in reverse order */
    if (nargs == 2) {
	maxw = xmlXPathPopNumber(ctxt);
	if (maxw < 0 || xmlXPathCheckError(ctxt))
	    return;
    }

    width = xmlXPathPopNumber(ctxt);
    if (width < 0 || xmlXPathCheckError(ctxt))
	return;

    if (maxw < width)		/* maxw cannot be < width */
	maxw = width;
    if (maxw < 1)		/* At least make one zero */
	maxw = 1;

    res = xmlMalloc(maxw + 1);
    if (res) {
	res[maxw] = '\0';
	if (maxw > width)
	    memset(res, '0', maxw - width);
	memset(res + maxw - width, '1', width);
    }

    xmlXPathReturnString(ctxt, res);
}

static void
extBitToInt (xmlXPathParserContextPtr ctxt, int nargs)
{
    xmlChar *res;
    xmlXPathObjectPtr xop;
    unsigned long long val;
    int i;

    if (nargs != 1) {
	xmlXPathSetArityError(ctxt);
	return;
    }

    xop = valuePop(ctxt);
    if (xop == NULL || xmlXPathCheckError(ctxt))
	return;
    res = extBitStringVal(ctxt, xop, FALSE);
    if (res == NULL)
	return;

    for (i = 0, val = 0; i <= 64 && res[i]; i++) {
	val <<= 1;
	if (res[i] == '1')
	    val += 1;
    }

    if (i > 64)
	xmlXPathReturnNumber(ctxt, (double) -1);
    else
	xmlXPathReturnNumber(ctxt, (double) val);
}

static void
extBitFromInt (xmlXPathParserContextPtr ctxt, int nargs)
{
    xmlChar *res;
    xmlXPathObjectPtr xop;
    int width = 0, len;

    if (nargs != 1 && nargs != 2) {
	xmlXPathSetArityError(ctxt);
	return;
    }

    /* Pop args in reverse order */
    if (nargs == 2) {
	width = xmlXPathPopNumber(ctxt);
	if (width < 0 || xmlXPathCheckError(ctxt))
	    return;
    }

    xop = valuePop(ctxt);
    if (xop == NULL || xmlXPathCheckError(ctxt))
	return;
    res = extBitStringVal(ctxt, xop, TRUE);
    if (res == NULL)
	return;

    len = xmlStrlen(res);
    if (width > len) {
	res = xmlRealloc(res, width + 1);
	if (res) {
	    int count = width - len;
	    memmove(res + count, res, len + 1);
	    memset(res, '0', count);
	}
    }

    xmlXPathReturnString(ctxt, res);
}

static void
extBitToHex (xmlXPathParserContextPtr ctxt, int nargs)
{
    xmlChar *res;
    xmlXPathObjectPtr xop;
    unsigned long long val;
    int i, len1, len2;

    if (nargs != 1) {
	xmlXPathSetArityError(ctxt);
	return;
    }

    /* Pop args in reverse order */
    xop = valuePop(ctxt);
    if (xop == NULL || xmlXPathCheckError(ctxt))
	return;
    res = extBitStringVal(ctxt, xop, FALSE);
    if (res == NULL)
	return;

    for (i = 0, val = 0; i <= 64 && res[i]; i++) {
	val <<= 1;
	if (res[i] == '1')
	    val += 1;
    }

    if (i > 64)
	xmlXPathReturnNumber(ctxt, (double) -1);
    else {
	len1 = xmlStrlen(res);
	len2 = snprintf((char *) res, len1 + 1, "0x%qx", val);
	if (len2 > len1) {
	    res = xmlRealloc(res, len2 + 1);
	    if (res)
		snprintf((char *) res, len2 + 1, "0x%qx", val);
	}

	xmlXPathReturnString(ctxt, res);
    }
}

static void
extBitFromHex (xmlXPathParserContextPtr ctxt, int nargs)
{
    xmlChar *res;
    int maxw = 0, width = 0, i;
    unsigned long long val, v2;

    if (nargs != 1 && nargs != 2) {
	xmlXPathSetArityError(ctxt);
	return;
    }

    /* Pop args in reverse order */
    if (nargs == 2) {
	maxw = xmlXPathPopNumber(ctxt);
	if (maxw < 0 || xmlXPathCheckError(ctxt))
	    return;
    }

    res = xmlXPathPopString(ctxt);
    if (res == NULL || xmlXPathCheckError(ctxt))
	return;

    /* An 'e' means we are seeing a large floating point number */
    if (strchr((char *) res, 'e') != NULL)
	val = 0;
    else
	val = strtoull((char *) res, NULL, 0);
    
    for (width = 0, v2 = val; v2; width++, v2 /= 2)
	continue;

    if (width == 0)		/* Gotta have one zero */
	width = 1;

    if (maxw < width)
	maxw = width;

    res = xmlRealloc(res, maxw + 1);
    if (res) {
	res[maxw] = '\0';

	for (i = maxw - 1, v2 = val; i >= 0; i--) {
	    if (width-- <= 0)
		res[i] = '0';
	    else {
		res[i] = (v2 & 1) ? '1' : '0';
		v2 /= 2;
	    }
	}
    }

    xmlXPathReturnString(ctxt, res);
}

static void
extBitClearOrSet (xmlXPathParserContextPtr ctxt, int nargs, xmlChar value)
{
    xmlChar *res;
    int width, bitnum = 0, delta;
    xmlXPathObjectPtr xop;

    if (nargs != 1 && nargs != 2) {
	xmlXPathSetArityError(ctxt);
	return;
    }

    /* Pop args in reverse order */
    if (nargs == 2) {
	bitnum = xmlXPathPopNumber(ctxt);
	if (bitnum < 0 || xmlXPathCheckError(ctxt))
	    return;
    }

    xop = valuePop(ctxt);
    if (xop == NULL || xmlXPathCheckError(ctxt))
	return;
    res = extBitStringVal(ctxt, xop, FALSE);
    if (res == NULL)
	return;

    width = xmlStrlen(res);
    delta = width - bitnum - 1;

    if (delta < 0) {
	xmlChar *newp = xmlRealloc(res, bitnum + 2);
	if (newp == NULL)
	    return;

	delta = -delta;
	memmove(newp + delta, newp, width + 1);
	newp[0] = value;
	memset(newp + 1, '0', delta - 1);
	res = newp;

    } else {
	res[delta] = value;
    }

    xmlXPathReturnString(ctxt, res);
}

static void
extBitClear (xmlXPathParserContextPtr ctxt, int nargs)
{
    extBitClearOrSet(ctxt, nargs, '0');
}

static void
extBitSet (xmlXPathParserContextPtr ctxt, int nargs)
{
    extBitClearOrSet(ctxt, nargs, '1');
}

static void
extBitCompare (xmlXPathParserContextPtr ctxt, int nargs)
{
    xmlChar *res1, *res2;
    xmlXPathObjectPtr xop;
    int width1, width2, off1, off2, rc, delta;

    if (nargs != 2) {
	xmlXPathSetArityError(ctxt);
	return;
    }

    /* Pop args in reverse order */
    xop = valuePop(ctxt);
    if (xop == NULL || xmlXPathCheckError(ctxt))
	return;
    res2 = extBitStringVal(ctxt, xop, FALSE);
    if (res2 == NULL)
	return;

    xop = valuePop(ctxt);
    if (xop == NULL || xmlXPathCheckError(ctxt))
	return;
    res1 = extBitStringVal(ctxt, xop, FALSE);
    if (res1 == NULL)
	return;

    width1 = xmlStrlen(res1);
    width2 = xmlStrlen(res2);
    delta = width1 - width2;

    rc = 0;
    off1 = off2 = 0;
    if (delta < 0) {
	for ( ; delta < 0; delta++, off2++) {
	    if (res2[off2] != '0') {
		rc = -1;
		goto done;
	    }
	}

    } else if (delta > 0) {
	for ( ; delta > 0; delta--, off1++) {
	    if (res1[off1] != '0') {
		rc = 1;
		goto done;
	    }
	}
    }

    rc = xmlStrcmp(res1 + off1, res2 + off2);

 done:
    xmlFree(res1);
    xmlFree(res2);

    xmlXPathReturnNumber(ctxt, rc);
}

slax_function_table_t slaxBitTable[] = {
    {
	"and", extBitAnd,
	"Bit-wise AND operator",
	"(bit-string, bit-string)", XPATH_STRING,
    },
    {
	"clear", extBitClear,
	"Clear a bit inside a bit string",
	"(bit-string, bit-number)", XPATH_STRING,
    },
    {
	"compare", extBitCompare,
	"Compare two bit strings, returning 1, 0, or -1",
	"(bit-string, bit-string)", XPATH_NUMBER,
    },
    {
	"from-hex", extBitFromHex,
	"Return a bit string based on a hexidecimal number",
	"(hex-value, len?)", XPATH_STRING,
    },
    {
	"from-int", extBitFromInt,
	"Return a bit string based on a number",
	"(value, len?)", XPATH_STRING,
    },
    {
	"mask", extBitMask,
	"Return a bit string of len with the low count bits set",
	"(count, len?)", XPATH_STRING,
    },
    {
	"nand", extBitNand,
	"Bit-wise NAND operator",
	"(bit-string, bit-string)", XPATH_STRING,
    },
    {
	"nor", extBitNor,
	"Bit-wise NOR operator",
	"(bit-string, bit-string)", XPATH_STRING,
    },
    {
	"not", extBitNot,
	"Bit-wise NOT operation",
	"(bit-string)", XPATH_STRING,
    },
    {
	"or", extBitOr,
	"Bit-wise OR operator",
	"(bit-string, bit-string)", XPATH_STRING,
    },
    {
	"set", extBitSet,
	"Set a bit within a bit-string",
	"(bit-string, bit-number)", XPATH_STRING,
    },
    {
	"to-hex", extBitToHex,
	"Return the hexidecimal value of a bit-string",
	"(bit-string)", XPATH_STRING,
    },
    {
	"to-int", extBitToInt,
	"Return the numeric value of a bit-string",
	"(bit-string)", XPATH_NUMBER,
    },
    {
	"xnor", extBitXnor,
	"Bit-wise XNOR operator",
	"(bit-string, bit-string)", XPATH_STRING,
    },
    {
	"xor", extBitXor,
	"Bit-wise XOR operator",
	"(bit-string, bit-string)", XPATH_STRING,
    },
    { NULL, NULL, NULL, NULL, XPATH_UNDEFINED },
};

SLAX_DYN_FUNC(slaxDynLibInit)
{
    arg->da_functions = slaxBitTable; /* Fill in our function table */

    return SLAX_DYN_VERSION;
}
