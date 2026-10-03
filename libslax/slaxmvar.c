/*
 * Copyright (c) 2010-2025, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
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
 *
 * mvars are backed by ordinary RVT (Result Tree Fragment) documents,
 * the same kind xsl:variable uses, except that xmlDoc now carries a
 * small refcount (see mvars-redo.md, Section 6) that keeps a value's
 * docs alive for as long as anything still references them -- a plain
 * variable that captured an mvar's value, or the mvar itself before a
 * reassignment finishes. That refcounting (xsltStackElemReplaceValue()
 * and friends, in libbxslt/libxslt/variables.c) is what makes a
 * reassignment safe without first copying the old value somewhere else
 * "just in case": there's no second variable to find or maintain, and
 * no explicit "retire" call needed. This file used to also maintain a
 * compiler-synthesized "shadow" variable as a history mechanism for
 * exactly that purpose; it's gone now, along with any ability to look
 * at an mvar's past values (there isn't one -- $book only ever yields
 * $book's current value).
 */

#include "slaxinternals.h"
#include <libslax/slax.h>
#include "slaxparser.h"
#include <ctype.h>
#include <errno.h>

#include <libxslt/extensions.h>
#include <libxslt/xsltutils.h>
#include <libxslt/transform.h>
#include <libxslt/variables.h>
#include <libxml/xpathInternals.h>

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
 * Called from the parser grammar for a "mvar $foo ...;" declaration to
 * mark the resulting <xsl:variable> as mutable. See the file header for
 * why there's no shadow variable to synthesize any more.
 *
 * @sdp: main slax parsing data structure
 */
void
slaxMvarMarkMutable (slax_data_t *sdp)
{
    slaxAttribAddLiteral(sdp, ATT_MUTABLE, "yes");
}

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
 * Search in the variable array of the context for the given
 * variable value.
 *
 * @ctxt:  the XSLT transformation context
 * @name:  the variable name
 * @ns_uri:  the variable namespace URI
 * @returns the variable or NULL if not found
 */
static xsltStackElemPtr
slaxMvarGlobalLookup (xsltTransformContextPtr ctxt,
		      const xmlChar *name, const xmlChar *uri)
{
    xsltStackElemPtr elem;

    /*
     * Lookup the global variables in XPath global variable hash table
     */
    if (xsltTransformContextGetXpathCtxt(ctxt) == NULL
	|| xsltTransformContextGetGlobalVars(ctxt) == NULL)
	return NULL;

    elem = (xsltStackElemPtr)
	xmlHashLookup2(xsltTransformContextGetGlobalVars(ctxt), name, uri);
    return elem;
}

/**
 * Search the stack for a local variable of the given name.
 *
 * @ctxt:  the XSLT transformation context
 * @name:  the variable name
 * @ns_uri:  the variable namespace URI
 * @returns the variable or NULL if not found
 */
static xsltStackElemPtr
slaxMvarLocalLookup (xsltTransformContextPtr ctxt,
		     const xmlChar *name, const xmlChar *uri)
{
    xsltStackElemPtr cur;
    int i;

    if (ctxt == NULL || name == NULL
	|| xsltTransformContextGetVarsNr(ctxt) == 0)
	return NULL;

    /*
     * Do the lookup from the top of the stack, but don't use params
     * being computed in a call-param The lookup expects the variable
     * name and URI strings to come from the dictionary and hence
     * pointer comparison.
     */
    slaxLog("local lookup: ctxt %p %d..%d", ctxt,
	      xsltTransformContextGetVarsNr(ctxt),
	      xsltTransformContextGetVarsBase(ctxt));
    for (i = xsltTransformContextGetVarsNr(ctxt);
	 i > xsltTransformContextGetVarsBase(ctxt); i--) {
	for (cur = xsltTransformContextGetVarsEntry(ctxt, i - 1); cur != NULL;
	     cur = xsltStackElemGetNext(cur)) {
	    if (xsltStackElemGetName(cur) == name
		&& xsltStackElemGetNameURI(cur) == uri)
		return cur;
	}
    }

    return NULL;
}

/**
 * Search for a local variable of the given name, either local or global.
 *
 * @ctxt:  the XSLT transformation context
 * @name:  the variable name
 * @ns_uri:  the variable namespace URI
 * @localp: (set on return) indicate if the variable is local
 * @returns the variable or NULL if not found
 */
static xsltStackElemPtr
slaxMvarLookup (xsltTransformContextPtr ctxt, const xmlChar *name,
		const xmlChar *uri, int *localp)
{
    const xmlChar *dname = xmlDictLookup(xsltTransformContextGetDict(ctxt),
					  name, -1);
    const xmlChar *duri = uri
	? xmlDictLookup(xsltTransformContextGetDict(ctxt), uri, -1) : NULL;
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

xsltStackElemPtr
slaxMvarLookupQname (xsltTransformContextPtr tctxt, const xmlChar *svarname,
		     int *localp)
{
    const xmlChar *lname = xmlStrchr(svarname, ':');
    xmlChar *uri;

    if (lname) {
	/* Make a copy of the uri so we can nul terminate it */
	int ulen = lname - svarname;
	uri = alloca(ulen + 1);
	memcpy(uri, svarname, ulen);
	uri[ulen] = '\0';
	lname += 1;		/* Move over ':' */
    } else {
	lname = svarname;
	uri = NULL;
    }

    return slaxMvarLookup(tctxt, lname, uri, localp);
}

/**
 * Decide if a value is scalar, meaning not a node set.
 *
 * @value: value to test for scalar-ness
 * @returns TRUE if the value is scalar (boolean, number, string)
 */
static int
slaxValueIsScalar (xmlXPathObjectPtr value)
{
    if (value == NULL)
	return TRUE;		/* Odd, but.... */

    if (xmlXPathObjectGetType(value) == XPATH_NODESET
	    || xmlXPathObjectGetType(value) == XPATH_XSLT_TREE)
	return FALSE;		/* Node sets are not scalar */

    return TRUE;
}

/*
 * Set a mutable variable to the given value. @value becomes @var's new
 * value outright -- xsltStackElemReplaceMvarValue() takes care of
 * promoting any brand-new RTF @value introduces to mvar-owned, and of
 * releasing whatever @var held before (down to zero and freed, unless
 * something else -- e.g. a plain variable that captured $foo's old
 * value -- still references it).
 */
static int
slaxMvarSet (const xmlChar *name, xsltStackElemPtr var,
	     xmlXPathObjectPtr value)
{
    slaxLog("mvar: set: %s --> %p (%p)", name, value,
	    xsltStackElemGetValue(var));

    xsltStackElemReplaceMvarValue(var, value);

    return FALSE;
}

/*
 * Deep-copies @cur into @container and appends the copy as a new child.
 */
static void
slaxMvarAddChild (xmlDocPtr container, xmlNodePtr cur)
{
    xmlNodePtr newp = xmlDocCopyNode(cur, container, 1);

    if (newp)
	xmlAddChild((xmlNodePtr) container, newp);
}

/*
 * Deep-copies every node in @nset as new children of @container. Used
 * only when slaxMvarAppendContainer() must start a fresh container but
 * needs to carry forward content from a value it can no longer safely
 * grow in place (see there for why). RTF entries are copied by their
 * children, since RTFs use "next" as a free list.
 */
static void
slaxMvarCopyInto (xmlDocPtr container, xmlNodeSetPtr nset)
{
    int i;

    for (i = 0; nset && i < xmlNodeSetGetNodeNr(nset); i++) {
	xmlNodePtr cur = xmlNodeSetGetNodeEntry(nset, i);

	if (cur == NULL)
	    continue;

	if (xsltIsResultTreeFragment(cur)) {
	    for (cur = xmlNodeGetChildren(cur); cur; cur = xmlNodeGetNext(cur))
		slaxMvarAddChild(container, cur);
	} else {
	    slaxMvarAddChild(container, cur);
	}
    }
}

/*
 * Returns the RTF container that new content should be appended into
 * for @var's current (non-scalar) value, creating one if needed.
 *
 * If that value's nodeset already ends in an RTF we exclusively own
 * (mvarRefcount == 1, meaning nothing else -- e.g. a plain variable
 * that captured $foo's value -- references it), we grow it in place:
 * new content becomes new children, with nothing new to refcount, since
 * the container itself isn't changing.
 *
 * Otherwise -- this is the first append, or @var's value is shared
 * (refcount > 1) or isn't an owned RTF at all (e.g. fresh off
 * "set $foo = some/xpath;") -- growing it in place would risk
 * corrupting whatever else references it, or isn't even ours to grow.
 * A fresh, exclusively-owned container is created instead, any existing
 * content is carried forward into it, and it's installed as @var's new
 * value via xsltStackElemReplaceMvarValue().
 */
static xmlDocPtr
slaxMvarAppendContainer (xsltTransformContextPtr ctxt, xsltStackElemPtr var)
{
    xmlXPathObjectPtr val = xsltStackElemGetValue(var);
    xmlNodeSetPtr nset = val ? xmlXPathObjectGetNodesetval(val) : NULL;
    int nr = nset ? xmlNodeSetGetNodeNr(nset) : 0;
    xmlDocPtr container;
    xmlNodeSetPtr res;

    if (nr > 0) {
	xmlNodePtr last = xmlNodeSetGetNodeEntry(nset, nr - 1);

	if (xsltIsResultTreeFragment(last)
		&& xmlDocGetMvarRefcount((xmlDocPtr) last) == 1)
	    return (xmlDocPtr) last;
    }

    container = xsltCreateRVT(ctxt);
    if (container == NULL)
	return NULL;

    /*
     * Deliberately not registered on ctxt's persist-RVT list: this
     * container's lifetime is governed entirely by mvar refcounting from
     * the moment xsltStackElemReplaceMvarValue() below attaches it, and
     * that machinery frees a doc early (via xsltReleaseRVT(), which
     * reuses its "next" field for an unrelated free list) as soon as its
     * refcount reaches zero. A doc can't safely be on both lists at
     * once.
     */
    if (nset != NULL)
	slaxMvarCopyInto(container, nset);

    res = xmlXPathNodeSetCreate((xmlNodePtr) container);
    if (res == NULL)
	return NULL;

    xsltStackElemReplaceMvarValue(var, xmlXPathWrapNodeSet(res));

    return container;
}

/*
 * Append a value to a variable.  There are four possibilities here:
 *
 * case #1: [ scalar var / scalar value ] -> string concatenation
 * case #2: [ scalar var / non-scalar value ] -> discard var
 * case #3: [ non-scalar var / scalar value ] -> use <text> for value
 * case #4: [ non-scalar var / non-scalar value ] -> append to node set
 *
 * In case 2, the scalar value will be placed inside a <text> element
 * and that node will be used as a node set.
 */
static int
slaxMvarAppend (xsltTransformContextPtr ctxt, const xmlChar *name,
		xsltStackElemPtr var, xmlXPathObjectPtr value)
{
    xmlNodePtr newp = NULL, cur;
    xmlDocPtr container;
    xmlNodeSetPtr nset = NULL;
    int i;

    if (value == NULL)
	return TRUE;

    slaxLog("mvar: append: %s, old %p --> new %p",
	    name, xsltStackElemGetValue(var), value);

    if (slaxValueIsScalar(xsltStackElemGetValue(var))) {
	if (value && slaxValueIsScalar(value)) {
	    /*
	     * case #1: [ scalar var / scalar value ] -> string concatenation
	     */
	    xmlChar *old_str = xmlXPathCastToString(xsltStackElemGetValue(var));
	    xmlChar *new_str = xmlXPathCastToString(value);
	    int old_len = old_str ? xmlStrlen(old_str) : 0;
	    int new_len = new_str ? xmlStrlen(new_str) : 0;
	    xmlChar *buf = xmlMalloc(old_len + new_len + 1);

	    if (buf) {
		memcpy(buf, old_str, old_len);
		memcpy(buf + old_len, new_str, new_len);
		buf[old_len + new_len] = '\0';

		xsltStackElemReplaceValue(var, xmlXPathWrapString(buf));
	    }

	    /* Free the values if we allocated them */
	    xmlFreeAndEasy(old_str);
	    xmlFreeAndEasy(new_str);
	    xmlXPathFreeObject(value);

	    return FALSE;

	} else {
	    /*
	     * case #2: [ scalar var / non-scalar value ] -> discard var;
	     * @value's own RTF(s) become $var's new value outright, no
	     * copying needed.
	     */
	    xsltStackElemReplaceMvarValue(var, value);
	    return FALSE;
	}

    } else {
	if (slaxValueIsScalar(value)) {
	    /*
	     * case #3: [ non-scalar var / scalar value ] -> use
	     * <text> for value
	     */
	    xmlChar *new_str = xmlXPathCastToString(value);

	    if (new_str && *new_str)
		newp = xmlNewText(new_str);

	    xmlFreeAndEasy(new_str);
	    xmlXPathFreeObject(value);
	    value = NULL;

	} else {
	    /*
	     * case #4: [ non-scalar var / non-scalar value ] ->
	     * append to node set
	     */
	    nset = xmlXPathObjectGetNodesetval(value);
	}
    }

    container = slaxMvarAppendContainer(ctxt, var);
    if (container == NULL) {
	slaxTransformError2(ctxt, "could not find append container for %s",
			    name);
	if (newp)
	    xmlFreeNode(newp);
	if (value)
	    xmlXPathFreeObject(value);
	return TRUE;
    }

    if (newp) {
	cur = xmlNewDocNode(container, NULL, (const xmlChar *) ELT_TEXT, NULL);
	if (cur) {
	    xmlAddChild(cur, newp);
	    slaxMvarAddChild(container, cur);
	    xmlFreeNode(cur);
	} else
	    xmlFreeNode(newp); /* Clean up on error */

    } else if (nset) {
	/* Add everything in the node set to the container */
	for (i = 0; i < xmlNodeSetGetNodeNr(nset); i++) {
	    cur = xmlNodeSetGetNodeEntry(nset, i);
	    if (cur == NULL)
		continue;

	    if (xsltIsResultTreeFragment(cur)) {
		for (cur = xmlNodeGetChildren(cur); cur;
		     cur = xmlNodeGetNext(cur))
		    slaxMvarAddChild(container, cur);

	    } else {
		slaxMvarAddChild(container, cur);
	    }
	}

	xmlXPathFreeObject(value);
    }

    return FALSE;
}

static xmlNodePtr
slaxFindVariable (xsltStylesheetPtr style UNUSED, xmlNodePtr inst,
		  const xmlChar *name, const xmlChar *uri)
{
    xmlNodePtr parent, child;
    xmlChar *vname, *local;

    for (parent = inst; parent; parent = xmlNodeGetParent(parent)) {
	for (child = xmlNodeGetChildren(parent); child;
	     child = xmlNodeGetNext(child)) {
	    if (xmlNodeGetType(child) != XML_ELEMENT_NODE)
		continue;

	    xmlNsPtr child_ns = xmlNodeGetNs(child);

	    slaxLog("findVariable: %s:%s -> %s:%s (%d)",
		      uri ?: slaxNull, name,
		      (child_ns && xmlNsGetPrefix(child_ns))
		      	? xmlNsGetPrefix(child_ns) : slaxNull,
		      xmlNodeGetName(child), xmlNodeGetType(child));

	    if (!streq((const char *) xmlNodeGetName(child), ELT_VARIABLE))
		continue;

	    if (child_ns == NULL || xmlNsGetHref(child_ns) == NULL)
		continue;

	    if (!streq((const char *) xmlNsGetHref(child_ns), XSL_URI))
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
	xsltStylesheetIncrementErrors(style);
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
	slaxLog("mvar: variable not found '%s'.\n", comp->mp_localname);

    } else {
	xmlChar *mutable;

	mutable = xmlGetNsProp(var, (const xmlChar *) ATT_MUTABLE, NULL);
	if (mutable == NULL) {
	    xsltTransformError(NULL, style, inst,
			 "immutable variable cannot be changed (var): '%s'.\n",
			       comp->mp_localname);
	    xsltStylesheetIncrementErrors(style);
	} else {
	    if (!streq((const char *) mutable, "yes")) {
		xsltTransformError(NULL, style, inst,
		    "immutable variable cannot be changed: '%s'.\n", name);
		xsltStylesheetIncrementErrors(style);
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
	       "invalid XPath expression for mvar '%s': '%s'.\n", name, sel);
	    xsltStylesheetIncrementErrors(style);
	}

	if (xmlNodeGetChildren(inst) != NULL) {
	    xsltTransformError(NULL, style, inst,
		"mvar cannot have child nodes when the "
		"attribute 'select' is used.\n");
	    xsltStylesheetIncrementErrors(style);
	}

	xmlFree(sel);
    }

    /* Prebuild the namespace list */
    comp->mp_nslist = xmlGetNsList(xmlNodeGetDoc(inst), inst);
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
    xmlXPathContextPtr xpathCtxt = xsltTransformContextGetXpathCtxt(ctxt);
    xmlNsPtr *save_nslist = xmlXPathContextGetNamespaces(xpathCtxt);
    int save_nscount = xmlXPathContextGetNsNr(xpathCtxt);
    xmlNodePtr save_context = xmlXPathContextGetNode(xpathCtxt);

    xmlXPathContextSetNamespaces(xpathCtxt, nslist);
    xmlXPathContextSetNsNr(xpathCtxt, nscount);
    xmlXPathContextSetNode(xpathCtxt, node);

    value = xmlXPathCompiledEval(expr, xpathCtxt);

    xmlXPathContextSetNode(xpathCtxt, save_context);
    xmlXPathContextSetNsNr(xpathCtxt, save_nscount);
    xmlXPathContextSetNamespaces(xpathCtxt, save_nslist);

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

    save_insert = xsltTransformContextGetInsert(ctxt);
    xsltTransformContextSetInsert(ctxt, (xmlNodePtr) container);

    /* Apply the template code inside the element */
    xsltApplyOneTemplate(ctxt, node, xmlNodeGetChildren(inst), NULL, NULL);

    xsltTransformContextSetInsert(ctxt, save_insert);

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

    } else if (xmlNodeGetChildren(inst)) {
	tree = slaxMvarEvalBlock(ctxt, node, inst);
	if (tree == NULL)
	    return;

	/*
	 * Wrap the freshly rendered RTF as a value, same as any other
	 * mvar content -- its lifetime from here on is governed purely
	 * by mvar refcounting (xsltStackElemReplaceMvarValue() and
	 * friends), not by any persist-RVT registration, so there's
	 * nothing further to do with the raw doc pointer itself.
	 */
	value = xmlXPathNewValueTree((xmlNodePtr) tree);
	if (value == NULL) {
	    xmlFreeDoc(tree);
	    return;
	}

    } else
	return;

    /*
     * mvars must support stepping through their content directly (e.g.
     * "$book/child::*"), which XPath's CHECK_TYPE0() refuses for
     * XPATH_XSLT_TREE -- that type is only transparent to node-set
     * -accepting functions/operators, not to location-step evaluation
     * (see xmlXPathNodeCollectAndTest()). A value can come out tagged
     * XPATH_XSLT_TREE either from slaxMvarEvalBlock()'s RTF above, or
     * from a "select" expression that itself evaluates to one (e.g.
     * calling a SLAX function that returns a result tree fragment); in
     * either case, retag it in place as a plain node-set, exactly like
     * exsl:node-set() does for the same type (see xsltFunctionNodeSet()
     * in extra.c) -- the two types share the same internal layout.
     */
    if (xmlXPathObjectGetType(value) == XPATH_XSLT_TREE)
	xmlXPathObjectSetType(value, XPATH_NODESET);

    var = slaxMvarLookup (ctxt, comp->mp_name, comp->mp_uri, &local);
    if (var == NULL) {
	xsltGenericError(xsltGenericErrorContext,
			 "mvar variable not found: %s\n", comp->mp_name);
	xmlXPathFreeObject(value);
	return;
    }

    /* slaxMvarSet()/slaxMvarAppend() consume value, so don't free it */
    if (append)
	slaxMvarAppend(ctxt, comp->mp_localname, var, value);
    else
	slaxMvarSet(comp->mp_localname, var, value);
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

void
slaxMvarRegister (void)
{
    xsltRegisterExtModuleElement ((const xmlChar *) ELT_SET_VARIABLE,
				  (const xmlChar *) SLAX_URI,
			  (xsltPreComputeFunction) slaxMvarSetCompile,
			  (xsltTransformFunction) slaxMvarSetElement);

    xsltRegisterExtModuleElement ((const xmlChar *) ELT_APPEND_TO_VARIABLE,
				  (const xmlChar *) SLAX_URI,
			  (xsltPreComputeFunction) slaxMvarAppendCompile,
			  (xsltTransformFunction) slaxMvarAppendElement);
}
