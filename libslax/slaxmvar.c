/*
 * $Id$
 *
 * Copyright (c) 2010, Juniper Networks, Inc.
 * All rights reserved.
 * See ../Copyright for the status of this software
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

typedef struct mvar_precomp_s {
    xsltElemPreComp mp_comp;	/* Standard precomp header */
    xmlXPathCompExprPtr mp_select; /* Compiled select expression */
    xmlNsPtr *mp_nslist;	   /* Prebuilt namespace list */
    int mp_nscount;		   /* Number of namespaces in mp_nslist */
    xmlChar *mp_name;		   /* Name of the variable */
    xmlChar *mp_localname;	   /* Pointer to localname _in_ mp_name */
    xmlChar *mp_uri;		   /* Namespace of the variable */
    xmlChar *mp_svarname;	   /* Name of the shadow variable */
} mvar_precomp_t;

/*
 * Make the variable name for the shadow variable.
 */
static xmlChar *
slaxMvarSvarName (const char *varname)
{
    char buf[BUFSIZ], *cp;

    cp = strchr(varname, ':');
    if (cp)
	snprintf(buf, sizeof(buf), "%*s:slax-%s", (int) (cp - varname),
		 varname, cp);
    else
	snprintf(buf, sizeof(buf), "slax-%s", varname);

    return xmlStrdup((const xmlChar *) buf);
}

/**
 * Called from the parser grammar to rewrite a variable definition into
 * a mutable variable (mvar) and its shadow variable (svar).  The shadow
 * variable is used as a "history" mechanism, keeping previous values
 * of the mvar so references to those contents don't dangle when the
 * mvar value changes.
 *
 * @sdp: main slax parsing data structure
 * @varname: variable name (mvar)
 */
void
slaxMvarCreateSvar (slax_data_t *sdp, const char *mvarname)
{
    xmlNodePtr mvar = sdp->sd_ctxt->node, svar;
    xmlChar *svarname;

    svarname = slaxMvarSvarName(mvarname);
    if (svarname == NULL)
	return;

    /* Create the shadow variable (svar) as a copy of the mvar*/
    if (mvar->children) {
	svar = xmlDocCopyNode(mvar, mvar->doc, 1);
	if (svar == NULL)
	    goto fail;
    } else {
	svar = xmlNewNode(sdp->sd_xsl_ns, (const xmlChar *) ELT_VARIABLE);
	if (svar == NULL)
	    goto fail;
    }

    /* Set the line number so the debugger can find us */
    svar->line = mvar->line;

    /* Set the name of the shadow variable */
    xmlSetNsProp(svar, NULL, (const xmlChar *) ATT_NAME,
		 (const xmlChar *) svarname);
    xmlSetNsProp(svar, NULL, (const xmlChar *) ATT_MVARNAME,
	       (const xmlChar *) mvarname);

    /*
     * Put some finishing touches on the mvar:
     * - Mark it as mutable
     * - Add select attribute
     * - Clear children
     */
    slaxAttribAddLiteral(sdp, ATT_MUTABLE, "yes");
    if (mvar->children) {
	char buf[BUFSIZ];

	/* Set the 'select' attribute to our init function */
	snprintf(buf, sizeof(buf),
		 SLAX_PREFIX ":" FUNC_MVAR_INIT "(\"%s\")", svarname);
	xmlSetNsProp(mvar, NULL,
		     (const xmlChar *) ATT_SELECT, (const xmlChar *) buf);
	slaxSetSlaxNs(sdp, mvar, FALSE);

	/* Remove the children (copies are under the svar) */
	xmlFreeNodeList(mvar->children);
	mvar->children = NULL;
    }

    /* Leave a pointer to our svar's name */
    xmlSetNsProp(mvar, NULL, (const xmlChar *) ATT_SVARNAME,
		 (const xmlChar *) svarname);

    /* Add the svar to the tree */
    xmlAddPrevSibling(mvar, svar);

 fail:
    xmlFreeAndEasy(svarname);
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

    xmlFreeAndEasy(comp->mp_svarname);
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
    if (ctxt->xpathCtxt == NULL || ctxt->globalVars == NULL)
	return NULL;

    elem = (xsltStackElemPtr) xmlHashLookup2(ctxt->globalVars, name, uri);
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

static xsltStackElemPtr
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

    if (value->type == XPATH_NODESET || value->type == XPATH_XSLT_TREE)
	return FALSE;		/* Node sets are not scalar */

    return TRUE;
}

/*
 * Returns an allocated strings _OR_ stringval.
 */
static xmlChar *
slaxCastValueToString (xmlXPathObjectPtr value, int *freep)
{
    if (value->type == XPATH_NUMBER) {
	if (freep)
	    *freep = TRUE;
	return xmlXPathCastNumberToString(value->floatval);
    }

    if (value->type == XPATH_BOOLEAN) {
	if (freep)
	    *freep = TRUE;
	return xmlXPathCastBooleanToString(value->boolval);
    }

    return value->stringval;
}

/*
 * Return the root document for a shadow variable (svar)
 */
static xsltStackElemPtr
slaxMvarGetSvar (xsltTransformContextPtr ctxt,
		 xsltStackElemPtr var UNUSED,
		 const xmlChar *mvarname UNUSED,
		 const xmlChar *svarname)
{
    return slaxMvarLookupQname(ctxt, svarname, NULL);
}

/*
 * Return the root document of the shadow variable's value.  If
 * there isn't one, make it by hand.
 */
static xmlDocPtr
slaxMvarGetSvarRoot (xsltTransformContextPtr ctxt, xsltStackElemPtr svar)
{
    xmlXPathObjectPtr value = svar->value;
    xmlDocPtr container;

    if (value->nodesetval && value->nodesetval->nodeNr > 0
	    && value->nodesetval->nodeTab)
	return (xmlDocPtr) value->nodesetval->nodeTab[0];

    container = xsltCreateRVT(ctxt);
    if (container == NULL)
	return NULL;

    /*
     * We build value as a nodeset containing the RTF/RVT.  It's
     * not pretty, but building it by hand is unavoidable.
     */
    xmlFreeAndEasy(value->stringval); /* Discard stringval if any */
    memset(value, 0, sizeof(*value));
    value->type = XPATH_NODESET;
    value->boolval = 1;		/* Free container when value is freed */
    value->nodesetval = xmlXPathNodeSetCreate((xmlNodePtr) container);

    if (value->nodesetval && value->nodesetval->nodeNr > 0
	    && value->nodesetval->nodeTab)
	return (xmlDocPtr) value->nodesetval->nodeTab[0];

    return NULL;
}

static void
slaxMvarAdd (xmlDocPtr container, xmlNodeSetPtr res, xmlNodePtr cur)
{
    xmlNodePtr newp;

    newp = xmlDocCopyNode(cur, container, 1);
    if (newp) {
	xmlAddChild((xmlNodePtr) container, newp);
	xmlXPathNodeSetAdd(res, newp);
    }
}

/*
 * Set a mutable variable to the given value
 */
static int
slaxMvarSet (xsltTransformContextPtr ctxt, const xmlChar *name,
	     const xmlChar *svarname, const xmlChar *uri UNUSED,
	     xsltStackElemPtr var, xmlXPathObjectPtr value)
{
    xmlXPathObjectPtr old_value;
    int i;

    slaxLog("mvar: set: %s --> %p (%p)", name, value, var->value);

    if (!slaxValueIsScalar(value)) {
	/*
	 * Value is not a scalar; copy it to our shadow variable's RTF
	 * and build a node set for the real value.
	 */
	xmlDocPtr container;
	xsltStackElemPtr svar;
	xmlNodeSetPtr res;
	xmlNodeSetPtr nset = value->nodesetval;

	svar = slaxMvarGetSvar(ctxt, var, name, svarname);
	if (svar == NULL) {
	    slaxTransformError2(ctxt,
				"found not find shadow variable for %s (%s)",
				name, svarname);
	    return TRUE;
	}

	container = slaxMvarGetSvarRoot(ctxt, svar);
	if (container == NULL) {
	    slaxTransformError2(ctxt,
				"found not find shadow container for %s (%s)",
				name, svarname);
	    return TRUE;
	}

	res = xmlXPathNodeSetCreate(NULL);
	if (res == NULL) {
	    slaxTransformError2(ctxt,
				"found not make node set for %s (%s)",
				name, svarname);
	    return TRUE;
	}

	for (i = 0; i < nset->nodeNr; i++) {
	    xmlNodePtr cur = nset->nodeTab[i];
	    if (cur == NULL)
		continue;

	   if (XSLT_IS_RES_TREE_FRAG(cur)) {
	       for (cur = cur->children; cur; cur = cur->next)
		   slaxMvarAdd(container, res, cur);

	   } else {
	       slaxMvarAdd(container, res, cur);
	   }
	}

	/* We need to free the new value, since we've copied its contents */
	xmlXPathFreeObject(value);

	value = xmlXPathWrapNodeSet(res);
    }

    /* Substitute our new value into the variable */
    old_value = var->value;
    var->value = value;
    var->computed = 1;

    /*
     * The old value is never an RTF, so we can free it without worrying
     * leaving someone with dangling references to our value.  The real
     * value lies in the shadow variable.
     */
    xmlXPathFreeObject(old_value);

    return FALSE;
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
		const xmlChar *svarname UNUSED, const xmlChar *uri UNUSED,
		xsltStackElemPtr var, xmlXPathObjectPtr value, xmlDocPtr tree)
{
    xmlNodePtr newp = NULL, cur;
    xmlDocPtr container;
    xsltStackElemPtr svar;
    xmlNodeSetPtr res = var->value->nodesetval;
    xmlNodeSetPtr nset = NULL;
    int i;

    if (value == NULL && tree == NULL) /* Must have one or the other */
	return TRUE;

    slaxLog("mvar: append: %s --> %p (%p)", name, value, var->value);

    if (slaxValueIsScalar(var->value)) {
	if (value && slaxValueIsScalar(value)) {
	    /*
	     * case #1: [ scalar var / scalar value ] -> string concatenation
	     */
	    int old_free = FALSE, new_free = FALSE;
	    xmlChar *old_str = slaxCastValueToString(var->value, &old_free);
	    xmlChar *new_str = slaxCastValueToString(value, &new_free);
	    int old_len = old_str ? xmlStrlen(old_str) : 0;
	    int new_len = new_str ? xmlStrlen(new_str) : 0;
	    xmlChar *buf = xmlRealloc(old_str, old_len + new_len + 1);

	    if (buf) {
		if (!old_free && buf != old_str)
		    old_free = TRUE;
		memcpy(buf + old_len, new_str, new_len);
		buf[old_len + new_len] = '\0';

		/* Are we lucky enough have to realloc'd the buffer? */
		old_free = (buf != old_str) ? TRUE : FALSE;

		if (var->value->stringval && buf != var->value->stringval) {
		    xmlFreeAndEasy(var->value->stringval);
		    old_free = FALSE;
		}

		var->value->stringval = buf;
		var->value->type = XPATH_STRING; /* Force as string */
	    }

	    /* Free the values if we allocated them */
	    if (old_free)
		xmlFreeAndEasy(old_str);
	    if (new_free)
		xmlFreeAndEasy(new_str);

	    return FALSE;

	} else {
	    /*
	     * case #2: [ scalar var / non-scalar value ] -> discard var
	     */

	    /* Turns out, we don't need to do anything */
	}

    } else {
	if (value && slaxValueIsScalar(value)) {
	    /*
	     * case #3: [ non-scalar var / scalar value ] -> use
	     * <text> for value
	     */
	    int new_free = FALSE;
	    xmlChar *new_str = slaxCastValueToString(value, &new_free);

	    if (new_str && *new_str)
		newp = xmlNewText(new_str);

	    if (new_free)
		xmlFreeAndEasy(new_str);

	} else {
	    /*
	     * case #4: [ non-scalar var / non-scalar value ] ->
	     * append to node set
	     */
	    nset = value ? value->nodesetval : NULL;
	}
    }

    svar = slaxMvarGetSvar(ctxt, var, name, svarname);
    if (svar == NULL) {
	slaxTransformError2(ctxt,
			    "found not find shadow variable for %s (%s)",
			    name, svarname);
	return TRUE;
    }

    container = slaxMvarGetSvarRoot(ctxt, svar);
    if (container == NULL) {
	slaxTransformError2(ctxt,
			    "found not find shadow container for %s (%s)",
			    name, svarname);
	return TRUE;
    }

    /* Make a node set if we need one */
    if (res == NULL) {
	res = xmlXPathNodeSetCreate(NULL);
	if (res == NULL) {
	    slaxTransformError2(ctxt,
				"found not make node set for %s (%s)",
				name, svarname);
	    return TRUE;
	}
	var->value->nodesetval = res;
	var->value->type = XPATH_NODESET;
	if (var->value->stringval) {
	    xmlFree(var->value->stringval);
	    var->value->stringval = NULL;
	}
    }

    if (newp) {
	cur = xmlNewNode(NULL, (const xmlChar *) ELT_TEXT);
	if (cur) {
	    xmlAddChild(cur, newp);

	    /* Add one node to the variable */
	    slaxMvarAdd(container, res, cur);

	    /*
	     * Since slaxMvarAdd has copied cur into the container tree,
	     * we need to release cur.
	     */
	    xmlFreeNode(cur);
	} else
	    xmlFreeNode(newp); /* Clean up on error */

    } else if (tree) {
	/* Add all the nodes in a tree to the variable */
	for (cur = tree->children; cur; cur = cur->next)
	    slaxMvarAdd(container, res, cur);

    } else if (nset) {
	/* Add everything in the node set to the variable */
	for (i = 0; i < nset->nodeNr; i++) {
	    cur = nset->nodeTab[i];
	    if (cur == NULL)
		continue;

	    if (XSLT_IS_RES_TREE_FRAG(cur)) {
		for (cur = cur->children; cur; cur = cur->next)
		    slaxMvarAdd(container, res, cur);
	    } else {
		slaxMvarAdd(container, res, cur);
	    }
	}
    }

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

	    if (!streq((const char *) child->ns->href, XSL_URI))
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

/*
 * Add the "svarname" attribute to a node
 */
void
slaxMvarAddSvarName (slax_data_t *sdp UNUSED, xmlNodePtr nodep)
{
    char *mvarname = (char *) xmlGetProp(nodep, (const xmlChar *) ATT_NAME);

    if (mvarname) {
	xmlChar *svarname = slaxMvarSvarName(mvarname ?: "bad-svarname");
	if (svarname) {
	    xmlSetNsProp(nodep, NULL,
		 (const xmlChar *) ATT_SVARNAME, (const xmlChar *) svarname);
	    xmlFree(svarname);
	}

	xmlFree(mvarname);
    }
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

    comp->mp_svarname = xmlGetNsProp(inst, (const xmlChar *) ATT_SVARNAME,
				     NULL);
    if (comp->mp_svarname == NULL) {
	xsltTransformError(NULL, style, inst,
	   "mvar: missing svarname attribute for mvar: %s\n", name);
	style->errors += 1;
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
	if (value)
	    xmlXPathFreeObject(value);
	return;
    }

    if (append) {
	slaxMvarAppend(ctxt, comp->mp_localname, comp->mp_svarname,
		       comp->mp_uri, var, value, tree);

	if (tree)
	    xmlFreeDoc(tree);
	if (value)
	    xmlXPathFreeObject(value);

    } else {
	if (tree) {
	    value = xmlXPathNewValueTree((xmlNodePtr) tree);
	    if (value == NULL) {
		xmlFreeDoc(tree);
		return;
	    }
	}

	/* slaxMvarSet() consumed value and/or table, so don't free them */
	slaxMvarSet(ctxt, comp->mp_localname, comp->mp_svarname,
		    comp->mp_uri, var, value);
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

/*
 * Initialize a variable from a shadow variable's contents.  We are
 * passed the name of shadow variable, 
 */
static void
slaxMvarInit (xmlXPathParserContextPtr ctxt, int nargs)
{
    xsltTransformContextPtr tctxt;
    xmlChar *svarname = NULL;
    xsltStackElemPtr svar;
    xmlXPathObjectPtr ret = NULL;
    xmlNodePtr nodep;

    if (nargs != 1) {
	xmlXPathSetArityError(ctxt);
	return;
    }

    tctxt = xsltXPathGetTransformContext(ctxt);
    if (tctxt == NULL) {
	slaxTransformError(ctxt, "slax:mvar-init: tctxt is NULL");
	goto fail;
    }

    svarname = xmlXPathPopString(ctxt);
    if (svarname == NULL) {
	slaxTransformError(ctxt, "slax:mvar-init: svarname is NULL");
	goto fail;
    }

    svar = slaxMvarLookupQname(tctxt, svarname, NULL);
    if (svar == NULL) {
	slaxTransformError(ctxt, "slax:mvar-init: svarname not found: %s",
			   svarname);
	goto fail;
    }

    if (svar->value == NULL || svar->value->nodesetval == NULL
	|| svar->value->nodesetval->nodeTab == NULL
	|| svar->value->nodesetval->nodeTab[0] == NULL
	|| svar->value->nodesetval->nodeTab[0]->children == NULL)
	goto fail;

    /*
     * Now we have the shadow variable and just need to build a nodeset
     * containing its nodes.
     */
    ret = xmlXPathNewNodeSet(NULL);
    if (ret == NULL)
	goto fail;

    nodep = svar->value->nodesetval->nodeTab[0]->children;
    for ( ; nodep; nodep = nodep->next) {
	xmlXPathNodeSetAdd(ret->nodesetval, nodep);
    }


fail:
    xmlFreeAndEasy(svarname);

    if (ret)
	valuePush(ctxt, ret);
    else 
	valuePush(ctxt, xmlXPathNewNodeSet(NULL));
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

    xsltRegisterExtModuleFunction((const xmlChar *) FUNC_MVAR_INIT,
				  (const xmlChar *) SLAX_URI,
				  slaxMvarInit);
}
