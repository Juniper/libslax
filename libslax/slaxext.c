/*
 * $Id$
 *
 * Copyright (c) 2010, Juniper Networks, Inc.
 * All rights reserved.
 * See ../Copyright for the status of this software
 *
 * SLAX extension functions, including:
 *
 * - <trace> support
 */

#include "slaxinternals.h"
#include <libslax/slax.h>
#include "slaxparser.h"
#include <ctype.h>
#include <errno.h>

#include <libxslt/extensions.h>
#include <libxslt/xsltutils.h>
#include <libxslt/transform.h>
#include <libxml/xpathInternals.h>

typedef struct trace_precomp_s {
    xsltElemPreComp tp_comp;	/* Standard precomp header */
    xmlXPathCompExprPtr tp_select; /* Compiled select expression */
    xmlNsPtr *tp_nslist;	   /* Prebuilt namespace list */
    int tp_nscount;		   /* Number of namespaces in tp_nslist */
} trace_precomp_t;

/*
 * This is the callback function that we use to pass trace data
 * up to the caller.
 */
static slaxTraceCallback_t slaxTraceCallback;
static void *slaxTraceCallbackData;

/**
 * Deallocates a trace_precomp_t
 *
 * @comp the precomp data to free (a trace_precomp_t)
 */
static void
slaxTraceFreeComp (trace_precomp_t *comp)
{
    if (comp == NULL)
	return;

    if (comp->tp_select)
	xmlXPathFreeCompExpr(comp->tp_select);

    xmlFreeAndEasy(comp->tp_nslist);
    xmlFree(comp);
}

/**
 * Precompile a trace element, so make running it faster
 *
 * @style the current stylesheet
 * @inst this instance
 * @function the transform function (opaquely passed to xsltInitElemPreComp)
 */
static xsltElemPreCompPtr
slaxTraceCompile (xsltStylesheetPtr style, xmlNodePtr inst,
	       xsltTransformFunction function)
{
    xmlChar *sel;
    trace_precomp_t *comp;

    comp = xmlMalloc(sizeof(*comp));
    if (comp == NULL) {
        xsltGenericError(xsltGenericErrorContext, "trace: malloc failed\n");
        return NULL;
    }

    memset(comp, 0, sizeof(*comp));

    xsltInitElemPreComp((xsltElemPreCompPtr) comp, style, inst, function,
			 (xsltElemPreCompDeallocator) slaxTraceFreeComp);
    comp->tp_select = NULL;

    /* Precompile the select attribute */
    sel = xmlGetNsProp(inst, (const xmlChar *) ATT_SELECT, NULL);
    if (sel != NULL) {
	comp->tp_select = xmlXPathCompile(sel);
	xmlFree(sel);
    }

    /* Prebuild the namespace list */
    comp->tp_nslist = xmlGetNsList(inst->doc, inst);
    if (comp->tp_nslist != NULL) {
        int i = 0;
        while (comp->tp_nslist[i] != NULL)
	    i++;
	comp->tp_nscount = i;
    }

    return &comp->tp_comp;
}

/**
 * Handle a <trace> element, as manufactured by the "trace" statement
 *
 * @ctxt transform context
 * @node current input node
 * @inst the <trace> element
 * @comp the precompiled info (a trace_precomp_t)
 */
static void
slaxTraceElement (xsltTransformContextPtr ctxt,
		  xmlNodePtr node UNUSED, xmlNodePtr inst,
		  trace_precomp_t *comp)
{
    xmlXPathObjectPtr value;

    if (slaxTraceCallback == NULL)
	return;

    if (comp->tp_select) {
	/*
	 * Adjust the context to allow the XPath expresion to
	 * find the right stuff.  Save old info like namespace,
	 * install fake ones, eval the expression, then restore
	 * the saved values.
	 */
	xmlNsPtr *save_nslist = ctxt->xpathCtxt->namespaces;
	int save_nscount = ctxt->xpathCtxt->nsNr;
	xmlNodePtr save_context = ctxt->xpathCtxt->node;

	ctxt->xpathCtxt->namespaces = comp->tp_nslist;
	ctxt->xpathCtxt->nsNr = comp->tp_nscount;
	ctxt->xpathCtxt->node = node;

	value = xmlXPathCompiledEval(comp->tp_select, ctxt->xpathCtxt);

	ctxt->xpathCtxt->node = save_context;
	ctxt->xpathCtxt->nsNr = save_nscount;
	ctxt->xpathCtxt->namespaces = save_nslist;

    } else if (inst->children) {
	/*
	 * The content of the element is a template that generates
	 * the value.
	 */
	xmlNodePtr save_insert;
	xmlDocPtr container;

	/* Fake an RVT to hold the output of the template */
	container = xsltCreateRVT(ctxt);
	if (container == NULL) {
	    xsltGenericError(xsltGenericErrorContext, "trace: no memory\n");
	    return;
	}

	xsltRegisterLocalRVT(ctxt, container);	

	save_insert = ctxt->insert;
	ctxt->insert = (xmlNodePtr) container;

	/* Apply the template code inside the element */
	xsltApplyOneTemplate(ctxt, node,
			      inst->children, NULL, NULL);
	ctxt->insert = save_insert;

	value = xmlXPathNewValueTree((xmlNodePtr) container);
    } else return;

    if (value == NULL)
	return;

    /*
     * Mark it as a function result in order to avoid garbage
     * collecting of tree fragments before the function exits.
     */
    xsltExtensionInstructionResultRegister(ctxt, value);


    switch (value->type) {
    case XPATH_STRING:
	if (value->stringval)
	    slaxTraceCallback(slaxTraceCallbackData, NULL,
			      "%s", value->stringval);
	break;

    case XPATH_NUMBER:
	slaxTraceCallback(slaxTraceCallbackData, NULL, "%f", value->floatval);
	break;

    case XPATH_XSLT_TREE:
	if (value->nodesetval) {
	    int i;
	    xmlNodeSetPtr tab = value->nodesetval;
	    for (i = 0; i < tab->nodeNr; i++) {
		slaxTraceCallback(slaxTraceCallbackData,
				  tab->nodeTab[i], NULL);
	    }
	}
	break;

    case XPATH_BOOLEAN:
	slaxTraceCallback(slaxTraceCallbackData, NULL, "%s",
			  value->boolval ? "true" : "false");
	break;

    case XPATH_UNDEFINED:
    case XPATH_NODESET:
    case XPATH_POINT:
    case XPATH_RANGE:
    case XPATH_LOCATIONSET:
    case XPATH_USERS:
    default:
	;
    }
}

/**
 * Enable tracing with a callback
 *
 * @func callback function
 * @data opaque data passed to callback
 */
void
slaxTraceEnable (slaxTraceCallback_t func, void *data)
{
    slaxTraceCallback = func;
    slaxTraceCallbackData = data;
}

static void
slaxExtBuildSequence (xmlXPathParserContextPtr ctxt, int nargs)
{
    long long num, low, high, step = 1;
    xmlNodeSetPtr ns;
    xmlNodePtr nodep;
    char buf[BUFSIZ];

    if (nargs != 2) {
        xsltTransformError(xsltXPathGetTransformContext(ctxt), NULL, NULL,
                "slax:build-sequence() expects two arguments\n");
        ctxt->error = XPATH_INVALID_ARITY;
        return;
    }

    /* Pop args in reverse order */
    high = xmlXPathPopNumber(ctxt);
    if (xmlXPathCheckError(ctxt))
        return;

    low = xmlXPathPopNumber(ctxt);
    if (xmlXPathCheckError(ctxt))
        return;

    if (high < low)
	step = -1;

    ns = xmlXPathNodeSetCreate(NULL);

    slaxTrace("build-sequence: %qd ... %qd + %qd", low, high, step);

    for (num = low; num <= high; num += step) {
	snprintf(buf, sizeof(buf), "%qd", num);
	nodep = xmlNewText((const xmlChar *) buf);
	if (nodep)
	    xmlXPathNodeSetAddUnique(ns, nodep);
    }

    xmlXPathReturnNodeSet(ctxt, ns);
}

/**
 * Registers the SLAX extensions
 */
void
slaxExtRegister (void)
{
    xsltRegisterExtModuleElement ((const xmlChar *) ELT_TRACE,
				  (const xmlChar *) TRACE_URI,
			  (xsltPreComputeFunction) slaxTraceCompile,
			  (xsltTransformFunction) slaxTraceElement);

    xsltRegisterExtModuleFunction((const xmlChar *) FUNC_BUILD_SEQUENCE,
                                  (const xmlChar *) SLAX_URI,
                                  slaxExtBuildSequence);
}
