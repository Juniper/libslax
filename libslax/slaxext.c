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

/*
 * Warning:  Do not confuse the "trace" statement and the <trace>
 * element with the SLAX internal slaxLog() function.  The former
 * is for scripts but the latter is internal, with output visible
 * via the "-v" switch (to slaxproc).  The naming is unfortunately
 * similar, but the tracing is at two distinct levels.
 */

/*
 * The following code supports the "trace" statement in SLAX, which is
 * turned into the XML element <slax-trace:trace> by the parser.  If
 * the environment has provided a callback function (via
 * slaxTraceEnable()), then script-level tracing data is passed
 * to that callback.  This makes a great way of keeping trace data
 * in the script in a maintainable way.
 */
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

/* ---------------------------------------------------------------------- */

static void
slaxExtBuildSequence (xmlXPathParserContextPtr ctxt, int nargs)
{
    long long num, low, high, step = 1;
    xsltTransformContextPtr tctxt;
    xmlNodePtr nodep;
    xmlDocPtr container;
    xmlXPathObjectPtr ret = NULL;
    char buf[BUFSIZ];

    if (nargs != 2) {
        xmlXPathSetArityError(ctxt);
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

    slaxLog("build-sequence: %qd ... %qd + %qd", low, high, step);

    /* Return a result tree fragment */
    tctxt = xsltXPathGetTransformContext(ctxt);
    if (tctxt == NULL) {
        xsltTransformError(xsltXPathGetTransformContext(ctxt), NULL, NULL,
	      "exslt:tokenize : internal error tctxt == NULL\n");
	goto fail;
    }

    container = xsltCreateRVT(tctxt);
    if (container == NULL)
	goto fail;
    
    xsltRegisterLocalRVT(tctxt, container);
    ret = xmlXPathNewNodeSet(NULL);
    if (ret == NULL)
	goto fail;

    for (num = low; num <= high; num += step) {
	snprintf(buf, sizeof(buf), "%qd", num);
	nodep = xmlNewDocRawNode(container, NULL,
				 (const xmlChar *) "item", (xmlChar *) buf);
	if (nodep) {
	    xmlAddChild((xmlNodePtr) container, nodep);
	    xmlXPathNodeSetAdd(ret->nodesetval, nodep);
	}
    }

fail:
    if (ret != NULL)
        valuePush(ctxt, ret);
    else
        valuePush(ctxt, xmlXPathNewNodeSet(NULL));
}

/* ---------------------------------------------------------------------- */

/*
 * XSLT has immutable variables.  This was done to support various
 * optimizations and advanced streaming functionality.  But it remains
 * one of the most painful parts of XSLT.  We use SLAX in JUNOS and
 * provide the ability to perform XML-based RPCs to local and remote
 * JUNOS boxes.  One RPC allows the script to store and retrieve
 * values in an SNMP MIB (jnxUtility MIB).  We have users using this
 * to "fake" mutable variables, so for our environment, any
 * theoretical arguments against the value of mutable variables is
 * lost.  They are happening, and the question becomes whether we want
 * to force script writers into mental anguish to allow them.
 *
 * Yes, exactly.  That was an apologetical defense of the following
 * code, which implements mutable variables.  Dio, abbi piet della mia
 * anima.
 */

typedef struct mvar_precomp_s {
    xsltElemPreComp mp_comp;	/* Standard precomp header */
    xmlXPathCompExprPtr mp_select; /* Compiled select expression */
    xmlNsPtr *mp_nslist;	   /* Prebuilt namespace list */
    int mp_nscount;		   /* Number of namespaces in mp_nslist */
    xmlChar *mp_name;		   /* Name of the variable */
    xmlChar *mp_localname;	   /* Pointer to localname _in_ mp_name */
    xmlChar *mp_uri;		   /* Namespace of the variable */
} mvar_precomp_t;

/**
 * Deallocates a mvar_precomp_t
 *
 * @comp the precomp data to free (a mvar_precomp_t)
 */
static void
slaxMvarFreeComp (mvar_precomp_t *comp)
{
    if (comp == NULL)
	return;

    if (comp->mp_select)
	xmlXPathFreeCompExpr(comp->mp_select);

    xmlFreeAndEasy(comp->mp_uri);
    xmlFreeAndEasy(comp->mp_name);
    xmlFreeAndEasy(comp->mp_nslist);
    xmlFree(comp);
}

/**
 * xsltGlobalVariableLookup:
 * @ctxt:  the XSLT transformation context
 * @name:  the variable name
 * @ns_uri:  the variable namespace URI
 *
 * Search in the Variable array of the context for the given
 * variable value.
 *
 * Returns the value or NULL if not found
 */
static xsltStackElemPtr
slaxMvarGlobalLookup (xsltTransformContextPtr ctxt,
		      const xmlChar *name, const xmlChar *uri)
{
    xsltStackElemPtr elem;

    /*
     * Lookup the global variables in XPath global variable hash table
     */
    if (ctxt->xpathCtxt == NULL || ctxt->globalVars == NULL)
	return NULL;

    elem = (xsltStackElemPtr) xmlHashLookup2(ctxt->globalVars, name, uri);
    if (elem == NULL)
	return NULL;

    return elem;
}

static xsltStackElemPtr
slaxMvarLocalLookup (xsltTransformContextPtr ctxt,
		     const xmlChar *name, const xmlChar *uri)
{
    xsltStackElemPtr cur;
    int i;

    if (ctxt == NULL || name == NULL || ctxt->varsNr == 0)
	return NULL;

    /*
     * Do the lookup from the top of the stack, but don't use params
     * being computed in a call-param The lookup expects the variable
     * name and URI strings to come from the dictionary and hence
     * pointer comparison.
     */
    slaxLog("local lookup: ctxt %p %d..%d %p", ctxt, ctxt->varsNr,
	      ctxt->varsBase, ctxt->varsTab);
    for (i = ctxt->varsNr; i > ctxt->varsBase; i--) {
	for (cur = ctxt->varsTab[i - 1]; cur != NULL; cur = cur->next) {
	    if (cur->name == name && cur->nameURI == uri)
		return cur;
	}
    }

    return NULL;
}

static xsltStackElemPtr
slaxMvarLookup (xsltTransformContextPtr ctxt, const xmlChar *name,
		const xmlChar *uri, int *localp)
{
    const xmlChar *dname = xmlDictLookup(ctxt->dict, name, -1);
    const xmlChar *duri = uri ? xmlDictLookup(ctxt->dict, uri, -1) : NULL;
    xsltStackElemPtr res;

    res = slaxMvarLocalLookup(ctxt, dname, duri);
    if (res) {
	if (localp)
	    *localp = TRUE;
	return res;
    }

    res = slaxMvarGlobalLookup(ctxt, dname, duri);
    if (res) {
	if (localp)
	    *localp = FALSE;
	return res;
    }

    return NULL;
}

static int
slaxMvarSet (xsltTransformContextPtr ctxt UNUSED, const xmlChar *name,
	     const xmlChar *uri UNUSED, xsltStackElemPtr var,
	     xmlXPathObjectPtr value)
{
    xmlXPathObjectPtr old_value;

    slaxLog("mvar: set: %s --> %p (%p)", name, value, var->value);

    old_value = var->value;
    var->value = value;
    var->computed = 1;
    xmlXPathFreeObject(old_value);

    /*
     * We don't like RTFs.  At all.  So here is a chance to save our
     * users from seeing them. At all.
     */
    if (value->type == XPATH_XSLT_TREE)
	value->type = XPATH_NODESET;

    return FALSE;
}

static int
slaxMvarCheckVar (xsltTransformContextPtr ctxt UNUSED, xsltStackElemPtr var)
{
    /* Test our assumptions about the contents of "var" */
    if (var == NULL || var->value == NULL) {
	xsltTransformError(NULL, NULL, NULL, "mvar: invalid var (2)\n");
	return TRUE;
    }

    return FALSE;
}

static int
slaxMvarCheckValue (xsltTransformContextPtr ctxt UNUSED,
		    xmlXPathObjectPtr value)
{
    /* Test our assumptions about the contents of "value" */
    if (value == NULL) {
	xsltTransformError(NULL, NULL, NULL, "mvar: invalid value\n");
	return TRUE;
    }

    return FALSE;
}

static const xmlChar *
slaxMvarStringVal (xmlXPathObjectPtr value, xmlChar **fpp)
{
    const xmlChar *cp = NULL;
    static const xmlChar *boolval[2] = {
	(const xmlChar *) "false", (const xmlChar *) "true"
    };

    if (value->type == XPATH_STRING)
	cp = value->stringval;
    else if (value->type == XPATH_BOOLEAN)
	cp = value->boolval ? boolval[1] : boolval[0];
    else if (value->type == XPATH_NUMBER) {
	*fpp = xmlXPathCastNumberToString(value->floatval);
	cp = *fpp;
    }

    return cp;
}

static int
slaxMvarInitVar (xsltTransformContextPtr ctxt, xsltStackElemPtr var UNUSED,
		 xmlNodePtr contents UNUSED)
{
    xmlDocPtr container;

    container = xsltCreateRVT(ctxt);
    if (container == NULL)
	return TRUE;

    /*
     * We do _not_ want to call xsltRegisterLocalRVT() here, since
     * we are putting the value into a variable.  We let the variable
     * cleanup (the popping of stack elements) handle the cleanup.
     */

    if (var->value) {
	xmlXPathObjectPtr vvp = var->value;

	if (vvp->nodesetval == NULL) {
	    if (vvp->stringval)
		xmlFree(vvp->stringval);

	    bzero(vvp, sizeof(*vvp));

	    vvp->type = XPATH_NODESET;
	    vvp->boolval = 1;
	    vvp->user = container;
	    vvp->nodesetval = xmlXPathNodeSetCreate((xmlNodePtr) container);
	} else {
	    xmlXPathNodeSetAdd(vvp->nodesetval, (xmlNodePtr) container);
	}
    } else {
	var->value = xmlXPathNewValueTree((xmlNodePtr) container);
    }

    if (contents) {
	xmlNodePtr newp = xmlDocCopyNode(contents, container, 1);
	if (newp)
	    xmlAddChild((xmlNodePtr) container, newp);
    }

    return FALSE;
}

static int
slaxMvarAppend (xsltTransformContextPtr ctxt, xsltStackElemPtr var,
		xmlXPathObjectPtr value, xmlDocPtr tree)
{
    xmlNodePtr src, dst, newp;

    if (value == NULL && tree == NULL) /* Must have one or the other */
	return TRUE;

    /* Silently promote RTFs into real node sets */
    if (var->value && var->value->type == XPATH_XSLT_TREE)
	var->value->type = XPATH_NODESET;

    if (slaxMvarCheckVar(ctxt, var))
	return TRUE;

    if (value && slaxMvarCheckValue(ctxt, value))
	return TRUE;

    if (var->value->type == XPATH_XSLT_TREE
		|| var->value->type == XPATH_NODESET) {
	if (var->value->nodesetval == NULL
		|| var->value->nodesetval->nodeNr != 1
		|| var->value->nodesetval->nodeTab == NULL) {
	    /* Worthless tree; build our own */
	    slaxMvarInitVar(ctxt, var, NULL);
	}

    } else {
	/*
	 * We're looking at something that is not a tree, so have to
	 * make one ourselves.  If there's some text in the variable,
	 * then make a node from it and put it in our new tree.
	 */
	const xmlChar *cp;
	xmlChar *fp = NULL;

	cp = slaxMvarStringVal(var->value, &fp);
	newp = (cp && *cp) ? xmlNewText(cp) : NULL;
	slaxMvarInitVar(ctxt, var, newp);
	xmlFreeNode(newp);

	if (fp)
	    xmlFree(fp);
    }

    if (var->value->nodesetval == NULL
	|| var->value->nodesetval->nodeTab == NULL)
	return TRUE;

    /* Find destination node */
    dst = var->value->nodesetval->nodeTab[0];

    if (value && value->type != XPATH_XSLT_TREE
		&& value->type != XPATH_NODESET) {
	const xmlChar *cp;
	xmlChar *fp = NULL;

	cp = slaxMvarStringVal(value, &fp);
	if (cp && *cp) {
	    src = xmlNewText(cp);
	    if (src) {
		newp = xmlDocCopyNode(src, dst->doc, 1);
		if (newp)
		    xmlAddChild(dst, newp);
		xmlFreeNode(src);
	    }
	}

	if (fp)
	    xmlFree(fp);
	xmlXPathFreeObject(value);

	return FALSE;
    }

    src = value ? value->nodesetval->nodeTab[0]->children : tree->children;
    for ( ; src; src = src->next) {
	newp = xmlDocCopyNode(src, dst->doc, 1);
	if (newp)
	    xmlAddChild(dst, newp);
    }

    if (value)
	xmlXPathFreeObject(value);

    return FALSE;
}

static xmlNodePtr
slaxFindVariable (xsltStylesheetPtr style UNUSED, xmlNodePtr inst,
		  const xmlChar *name, const xmlChar *uri)
{
    xmlNodePtr parent, child;
    xmlChar *vname, *local;

    for (parent = inst; parent; parent = parent->parent) {
	for (child = parent->children; child; child = child->next) {
	    if (child->type != XML_ELEMENT_NODE)
		continue;

	    slaxLog("findVariable: %s:%s -> %s:%s (%d)",
		      uri ?: null, name,
		      (child->ns && child->ns->prefix)
		      	? child->ns->prefix : null,
		      child->name, child->type);

	    if (!streq((const char *) child->name, ELT_VARIABLE))
		continue;

	    if (child->ns == NULL || child->ns->href == NULL)
		continue;

	    if (!streq((const char *) child->ns->href, XSL_NS))
		continue;

	    vname = xmlGetNsProp(child, (const xmlChar *) ATT_NAME, NULL);
	    if (vname == NULL)
		continue;

	    local = xmlStrchru(vname, ':');
	    if (local && uri) {
		*local++ = '\0';

		/* XXX: find namespace and compare it */

		slaxLog("var lname: %s %s", vname, name);
		if (xmlStrEqual(local, name)) {
		    xmlFree(vname);
		    return child;
		}

	    } else if (local == NULL && uri == NULL) {
		slaxLog("var name: %s %s", vname, name);
		if (xmlStrEqual(vname, name)) {
		    xmlFree(vname);
		    return child;
		}
	    }

	    xmlFree(vname);
	}
    }

    return NULL;
}

/**
 * Set a mutable variable
 *
 * @style the current stylesheet
 * @inst this instance
 * @function the transform function (opaquely passed to xsltInitElemPreComp)
 */
static xsltElemPreCompPtr
slaxMvarCompile (xsltStylesheetPtr style, xmlNodePtr inst,
		 xsltTransformFunction function, int append UNUSED)
{
    xmlChar *sel, *name;
    mvar_precomp_t *comp;
    xmlNodePtr var;

    comp = xmlMalloc(sizeof(*comp));
    if (comp == NULL) {
        xsltGenericError(xsltGenericErrorContext, "mvar: malloc failed\n");
        return NULL;
    }

    memset(comp, 0, sizeof(*comp));

    xsltInitElemPreComp((xsltElemPreCompPtr) comp, style, inst, function,
			 (xsltElemPreCompDeallocator) slaxMvarFreeComp);

    name = xmlGetNsProp(inst, (const xmlChar *) ATT_NAME, NULL);
    if (name)
	comp->mp_name = name;
    else {
	xsltTransformError(NULL, style, inst, "mvar: missing variable name\n");
	style->errors += 1;
    }

    /* Deal with setting mp_uri */
    comp->mp_localname = xmlStrchru(name, ':');
    if (comp->mp_localname) {
	*comp->mp_localname++ = '\0'; /* NUL terminate */
	/* XXX: find ns by uri */
    } else {
	comp->mp_localname = comp->mp_name; /* Skip '$' */
    }

    /* Look up the variable to report syntax errors */
    var = slaxFindVariable(style, inst, comp->mp_localname, NULL);
    if (var == NULL) {
	xsltTransformError(NULL, style, inst,
		"mvar: invalid variable '%s'.\n", comp->mp_localname);
	style->errors += 1;

    } else {
	xmlChar *mutable;

	mutable = xmlGetNsProp(var, (const xmlChar *) ATT_MUTABLE, NULL);
	if (mutable == NULL) {
	    xsltTransformError(NULL, style, inst,
		"mvar: immutable variable '%s'.\n", comp->mp_localname);
	    style->errors += 1;
	} else {
	    if (!streq((const char *) mutable, "yes")) {
		xsltTransformError(NULL, style, inst,
		    "mvar: immutable variable '%s'.\n", comp->mp_localname);
		style->errors += 1;
	    }

	    xmlFree(mutable);
	}
    }

    /* Precompile the select attribute */
    sel = xmlGetNsProp(inst, (const xmlChar *) ATT_SELECT, NULL);
    if (sel != NULL) {
	comp->mp_select = xmlXPathCompile(sel);
        if (comp->mp_select == NULL) {
            xsltTransformError(NULL, style, inst,
               "mvar: invalid XPath expression '%s'.\n", sel);
            style->errors += 1;
        }

        if (inst->children != NULL) {
            xsltTransformError(NULL, style, inst,
                "mvar: cannot have child nodes, since the "
                "attribute 'select' is used.\n");
            style->errors += 1;
        }

	xmlFree(sel);
    }

    /* Prebuild the namespace list */
    comp->mp_nslist = xmlGetNsList(inst->doc, inst);
    if (comp->mp_nslist != NULL) {
        int i = 0;
        while (comp->mp_nslist[i] != NULL)
	    i++;
	comp->mp_nscount = i;
    }

    return &comp->mp_comp;
}

static xmlXPathObjectPtr
slaxMvarEvalString (xsltTransformContextPtr ctxt, xmlNodePtr node,
		    xmlNsPtr *nslist, int nscount, xmlXPathCompExprPtr expr)
{
    xmlXPathObjectPtr value;

    /*
     * Adjust the context to allow the XPath expresion to
     * find the right stuff.  Save old info like namespace,
     * install fake ones, eval the expression, then restore
     * the saved values.
     */
    xmlNsPtr *save_nslist = ctxt->xpathCtxt->namespaces;
    int save_nscount = ctxt->xpathCtxt->nsNr;
    xmlNodePtr save_context = ctxt->xpathCtxt->node;

    ctxt->xpathCtxt->namespaces = nslist;
    ctxt->xpathCtxt->nsNr = nscount;
    ctxt->xpathCtxt->node = node;

    value = xmlXPathCompiledEval(expr, ctxt->xpathCtxt);

    ctxt->xpathCtxt->node = save_context;
    ctxt->xpathCtxt->nsNr = save_nscount;
    ctxt->xpathCtxt->namespaces = save_nslist;

    return value;
}

static xmlDocPtr
slaxMvarEvalBlock (xsltTransformContextPtr ctxt, xmlNodePtr node,
		   xmlNodePtr inst)
{
    /*
     * The content of the element is a template that generates
     * the value.
     */
    xmlNodePtr save_insert;
    xmlDocPtr container;

    /* Fake an RVT to hold the output of the template */
    container = xsltCreateRVT(ctxt);
    if (container == NULL) {
	xsltGenericError(xsltGenericErrorContext, "mvar: no memory\n");
	return NULL;
    }

    xsltRegisterLocalRVT(ctxt, container);

    save_insert = ctxt->insert;
    ctxt->insert = (xmlNodePtr) container;

    /* Apply the template code inside the element */
    xsltApplyOneTemplate(ctxt, node, inst->children, NULL, NULL);

    ctxt->insert = save_insert;

    return container;
}

/**
 * Set a mutable variable
 *
 * @ctxt transform context
 * @node current input node
 * @inst the <slax:*> element
 * @comp the precompiled info (a mvar_precomp_t)
 */
static void
slaxMvarElement (xsltTransformContextPtr ctxt,
		 xmlNodePtr node, xmlNodePtr inst,
		 mvar_precomp_t *comp, int append)
{
    xmlXPathObjectPtr value = NULL;
    xmlDocPtr tree = NULL;
    xsltStackElemPtr var;
    int local;

    if (comp->mp_select) {
	value = slaxMvarEvalString(ctxt, node,
				   comp->mp_nslist, comp->mp_nscount,
				   comp->mp_select);
	if (value == NULL)
	    return;

#if 0
	/*
	 * Mark it as a function result in order to avoid garbage
	 * collecting of tree fragments before the function exits.
	 */
	xsltExtensionInstructionResultRegister(ctxt, value);
#endif

    } else if (inst->children) {
	tree = slaxMvarEvalBlock(ctxt, node, inst);
	if (tree == NULL)
	    return;

    } else
	return;

    var = slaxMvarLookup (ctxt, comp->mp_name, comp->mp_uri, &local);
    if (var == NULL) {
        xsltGenericError(xsltGenericErrorContext,
			 "mvar: variable not found: %s\n", comp->mp_name);
	xmlXPathFreeObject(value);
	return;
    }

    /*
     * We don't need to free "tree" because it's registered
     * in the context and the normal garbage collection will
     * handle it.
     */
    if (append) {
	slaxMvarAppend(ctxt, var, value, tree);

    } else {
	if (tree) {
	    value = xmlXPathNewValueTree((xmlNodePtr) tree);
	    if (value == NULL)
		return;
	}

	slaxMvarSet(ctxt, comp->mp_localname, comp->mp_uri, var, value);
    }
}

static xsltElemPreCompPtr
slaxMvarSetCompile (xsltStylesheetPtr style, xmlNodePtr inst,
	       xsltTransformFunction function)
{
    return slaxMvarCompile(style, inst, function, FALSE);
}

static void
slaxMvarSetElement (xsltTransformContextPtr ctxt,
		  xmlNodePtr node, xmlNodePtr inst,
		  mvar_precomp_t *comp)
{
    slaxMvarElement(ctxt, node, inst, comp, FALSE);
}

static xsltElemPreCompPtr
slaxMvarAppendCompile (xsltStylesheetPtr style, xmlNodePtr inst,
	       xsltTransformFunction function)
{
    return slaxMvarCompile(style, inst, function, TRUE);
}

static void
slaxMvarAppendElement (xsltTransformContextPtr ctxt,
		  xmlNodePtr node, xmlNodePtr inst,
		  mvar_precomp_t *comp)
{
    slaxMvarElement(ctxt, node, inst, comp, TRUE);
}

/* ---------------------------------------------------------------------- */

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

    xsltRegisterExtModuleElement ((const xmlChar *) ELT_SET_VARIABLE,
				  (const xmlChar *) SLAX_URI,
			  (xsltPreComputeFunction) slaxMvarSetCompile,
			  (xsltTransformFunction) slaxMvarSetElement);

    xsltRegisterExtModuleElement ((const xmlChar *) ELT_APPEND_TO_VARIABLE,
				  (const xmlChar *) SLAX_URI,
			  (xsltPreComputeFunction) slaxMvarAppendCompile,
			  (xsltTransformFunction) slaxMvarAppendElement);

    xsltRegisterExtModuleFunction((const xmlChar *) FUNC_BUILD_SEQUENCE,
                                  (const xmlChar *) SLAX_URI,
                                  slaxExtBuildSequence);
}
