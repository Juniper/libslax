/*
 * $Id$
 *
 * Copyright (c) 2011, Juniper Networks, Inc.
 * All rights reserved.
 * See ../Copyright for the status of this software
 *
 * ext_bit.c -- extension functions for bit operations
 */

#include "slaxinternals.h"
#include <libslax/slax.h>

#include "config.h"

#include <libxml/xpathInternals.h>
#include <libxml/tree.h>
#include <libxslt/extensions.h>
#include <libslax/slaxdyn.h>

#define URI_BIT  "http://xml.juniper.net/extension/bit"

static xmlChar *
extBitStringVal (xmlXPathParserContextPtr ctxt, xmlXPathObjectPtr xop)
{
    if (xop->type == XPATH_NUMBER) {
	xmlChar *res;
	int width;
	unsigned long val = xop->floatval, v2;

	xmlXPathFreeObject(xop);

	for (width = 1, v2 = val; v2; width++, v2 /= 2)
	    continue;

	res = xmlMalloc(width + 1);
	res[width] = '\0';
	for (width--, v2 = val; width >= 0; width--, v2 /= 2)
	    res[width] = (v2 & 1) ? '1' : '0';
	
	return res;
    }

    /* Make libxml do the work for us */
    valuePush(ctxt, xop);
    return xmlXPathPopString(ctxt);
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
    rv = extBitStringVal(ctxt, xop);
    if (rv == NULL)
	return;

    xop = valuePop(ctxt);
    if (xop == NULL || xmlXPathCheckError(ctxt))
	return;
    lv = extBitStringVal(ctxt, xop);
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
    return (lb == '0' && rb == '0') ? '1' : '0';
}

static void
extBitNand (xmlXPathParserContextPtr ctxt, int nargs)
{
    extBitOperation(ctxt, nargs, extBitOpNand, "nand");
}

static xmlChar
extBitOpNor (xmlChar lb, xmlChar rb)
{
    return (lb == '0' || rb == '0') ? '1' : '0';
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
    res = extBitStringVal(ctxt, xop);
    if (res == NULL)
	return;

    width = xmlStrlen(res);
    for (i = 0; i < width; i++) {
	xmlChar lb = res[i];
	res[i] = (lb == '0') ? '1' : '0';
    }

    xmlXPathReturnString(ctxt, res);
}

slax_function_table_t slaxBitTable[] = {
    { "and", extBitAnd },
    { "or", extBitOr },
    { "nand", extBitNand },
    { "nor", extBitNor },
    { "xor", extBitXor },
    { "xnor", extBitXnor },
    { "not", extBitNot },
    { NULL, NULL },
};

SLAX_DYN_FUNC(slaxDynLibInit)
{
    arg->da_functions = slaxBitTable; /* Fill in our function table */

    return SLAX_DYN_VERSION;
}
