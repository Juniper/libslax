/*
 * templates.c: Implementation of the template processing
 *
 * Reference:
 *   http://www.w3.org/TR/1999/REC-xslt-19991116
 *
 * See Copyright for the status of this software.
 *
 * daniel@veillard.com
 */

#define IN_LIBXSLT
#include "libxslt.h"

#include <string.h>

#include <libxml/xmlmemory.h>
#include <libxml/globals.h>
#include <libxml/xmlerror.h>
#include <libxml/tree.h>
#include <libxml/dict.h>
#include <libxml/xpathInternals.h>
#include <libxml/parserInternals.h>
#include "xslt.h"
#include "xsltInternals.h"
#include "xsltutils.h"
#include "variables.h"
#include "functions.h"
#include "templates.h"
#include "transform.h"
#include "namespaces.h"
#include "attributes.h"

#ifdef WITH_XSLT_DEBUG
#define WITH_XSLT_DEBUG_TEMPLATES
#endif

/************************************************************************
 *									*
 *			Module interfaces				*
 *									*
 ************************************************************************/

/**
 * xsltEvalXPathPredicate:
 * @ctxt:  the XSLT transformation context
 * @comp:  the XPath compiled expression
 * @nsList:  the namespaces in scope
 * @nsNr:  the number of namespaces in scope
 *
 * Process the expression using XPath and evaluate the result as
 * an XPath predicate
 *
 * Returns 1 is the predicate was true, 0 otherwise
 */
int
xsltEvalXPathPredicate(xsltTransformContextPtr ctxt, xmlXPathCompExprPtr comp,
		       xmlNsPtr *nsList, int nsNr) {
    int ret;
    xmlXPathObjectPtr res;
    int oldNsNr;
    xmlNsPtr *oldNamespaces;
    xmlNodePtr oldInst;
    xmlNodePtr oldNode;
    int oldProximityPosition, oldContextSize;

    if ((ctxt == NULL) || (ctxt->inst == NULL)) {
        xsltTransformError(ctxt, NULL, NULL,
            "xsltEvalXPathPredicate: No context or instruction\n");
        return(0);
    }

    oldNode = ctxt->xpathCtxt->node;
    oldContextSize = ctxt->xpathCtxt->contextSize;
    oldProximityPosition = ctxt->xpathCtxt->proximityPosition;
    oldNsNr = ctxt->xpathCtxt->nsNr;
    oldNamespaces = ctxt->xpathCtxt->namespaces;
    oldInst = ctxt->inst;

    ctxt->xpathCtxt->node = ctxt->node;
    ctxt->xpathCtxt->namespaces = nsList;
    ctxt->xpathCtxt->nsNr = nsNr;

    res = xmlXPathCompiledEval(comp, ctxt->xpathCtxt);

    if (res != NULL) {
	ret = xmlXPathEvalPredicate(ctxt->xpathCtxt, res);
	xmlXPathFreeObject(res);
#ifdef WITH_XSLT_DEBUG_TEMPLATES
	XSLT_TRACE(ctxt,XSLT_TRACE_TEMPLATES,xsltGenericDebug(xsltGenericDebugContext,
	     "xsltEvalXPathPredicate: returns %d\n", ret));
#endif
    } else {
#ifdef WITH_XSLT_DEBUG_TEMPLATES
	XSLT_TRACE(ctxt,XSLT_TRACE_TEMPLATES,xsltGenericDebug(xsltGenericDebugContext,
	     "xsltEvalXPathPredicate: failed\n"));
#endif
	ctxt->state = XSLT_STATE_STOPPED;
	ret = 0;
    }

    ctxt->xpathCtxt->node = oldNode;
    ctxt->xpathCtxt->nsNr = oldNsNr;
    ctxt->xpathCtxt->namespaces = oldNamespaces;
    ctxt->inst = oldInst;
    ctxt->xpathCtxt->contextSize = oldContextSize;
    ctxt->xpathCtxt->proximityPosition = oldProximityPosition;

    return(ret);
}

/**
 * xsltEvalXPathStringNs:
 * @ctxt:  the XSLT transformation context
 * @comp:  the compiled XPath expression
 * @nsNr:  the number of namespaces in the list
 * @nsList:  the list of in-scope namespaces to use
 *
 * Process the expression using XPath, allowing to pass a namespace mapping
 * context and get a string
 *
 * Returns the computed string value or NULL, must be deallocated by the
 *    caller.
 */
xmlChar *
xsltEvalXPathStringNs(xsltTransformContextPtr ctxt, xmlXPathCompExprPtr comp,
	              int nsNr, xmlNsPtr *nsList) {
    xmlChar *ret = NULL;
    xmlXPathObjectPtr res;
    xmlNodePtr oldInst;
    xmlNodePtr oldNode;
    int	oldPos, oldSize;
    int oldNsNr;
    xmlNsPtr *oldNamespaces;

    if ((ctxt == NULL) || (ctxt->inst == NULL)) {
        xsltTransformError(ctxt, NULL, NULL,
            "xsltEvalXPathStringNs: No context or instruction\n");
        return(0);
    }

    oldInst = ctxt->inst;
    oldNode = ctxt->xpathCtxt->node;
    oldPos = ctxt->xpathCtxt->proximityPosition;
    oldSize = ctxt->xpathCtxt->contextSize;
    oldNsNr = ctxt->xpathCtxt->nsNr;
    oldNamespaces = ctxt->xpathCtxt->namespaces;

    ctxt->xpathCtxt->node = ctxt->node;
    /* TODO: do we need to propagate the namespaces here ? */
    ctxt->xpathCtxt->namespaces = nsList;
    ctxt->xpathCtxt->nsNr = nsNr;
    res = xmlXPathCompiledEval(comp, ctxt->xpathCtxt);
    if (res != NULL) {
	if (res->type != XPATH_STRING)
	    res = xmlXPathConvertString(res);
	if ((res != NULL) && (res->type == XPATH_STRING)) {
            ret = res->stringval;
	    res->stringval = NULL;
	} else {
	    xsltTransformError(ctxt, NULL, NULL,
		 "xpath : string() function didn't return a String\n");
	}
	xmlXPathFreeObject(res);
    } else {
	ctxt->state = XSLT_STATE_STOPPED;
    }
#ifdef WITH_XSLT_DEBUG_TEMPLATES
    XSLT_TRACE(ctxt,XSLT_TRACE_TEMPLATES,xsltGenericDebug(xsltGenericDebugContext,
	 "xsltEvalXPathString: returns %s\n", ret));
#endif
    ctxt->inst = oldInst;
    ctxt->xpathCtxt->node = oldNode;
    ctxt->xpathCtxt->contextSize = oldSize;
    ctxt->xpathCtxt->proximityPosition = oldPos;
    ctxt->xpathCtxt->nsNr = oldNsNr;
    ctxt->xpathCtxt->namespaces = oldNamespaces;
    return(ret);
}

/**
 * xsltEvalXPathString:
 * @ctxt:  the XSLT transformation context
 * @comp:  the compiled XPath expression
 *
 * Process the expression using XPath and get a string
 *
 * Returns the computed string value or NULL, must be deallocated by the
 *    caller.
 */
xmlChar *
xsltEvalXPathString(xsltTransformContextPtr ctxt, xmlXPathCompExprPtr comp) {
    return(xsltEvalXPathStringNs(ctxt, comp, 0, NULL));
}

/**
 * xsltEvalTemplateString:
 * @ctxt:  the XSLT transformation context
 * @contextNode:  the current node in the source tree
 * @inst:  the XSLT instruction (xsl:comment, xsl:processing-instruction)
 *
 * Processes the sequence constructor of the given instruction on
 * @contextNode and converts the resulting tree to a string.
 * This is needed by e.g. xsl:comment and xsl:processing-instruction.
 *
 * Returns the computed string value or NULL; it's up to the caller to
 *         free the result.
 */
xmlChar *
xsltEvalTemplateString(xsltTransformContextPtr ctxt,
		       xmlNodePtr contextNode,
	               xmlNodePtr inst)
{
    xmlNodePtr oldInsert, insert = NULL;
    xmlChar *ret;
    const xmlChar *oldLastText;
    int oldLastTextSize, oldLastTextUse;

    if ((ctxt == NULL) || (contextNode == NULL) || (inst == NULL) ||
        (xmlNodeGetType(inst) != XML_ELEMENT_NODE))
	return(NULL);

    if (xmlNodeGetChildren(inst) == NULL)
	return(NULL);

    /*
    * This creates a temporary element-node to add the resulting
    * text content to.
    * OPTIMIZE TODO: Keep such an element-node in the transformation
    *  context to avoid creating it every time.
    */
    insert = xmlNewDocNode(ctxt->output, NULL,
	                   (const xmlChar *)"fake", NULL);
    if (insert == NULL) {
	xsltTransformError(ctxt, NULL, inst,
		"Failed to create temporary node\n");
	return(NULL);
    }
    oldInsert = ctxt->insert;
    ctxt->insert = insert;
    oldLastText = ctxt->lasttext;
    oldLastTextSize = ctxt->lasttsize;
    oldLastTextUse = ctxt->lasttuse;
    /*
    * OPTIMIZE TODO: if inst->children consists only of text-nodes.
    */
    xsltApplyOneTemplate(ctxt, contextNode, xmlNodeGetChildren(inst), NULL, NULL);

    ctxt->insert = oldInsert;
    ctxt->lasttext = oldLastText;
    ctxt->lasttsize = oldLastTextSize;
    ctxt->lasttuse = oldLastTextUse;

    ret = xmlNodeGetContent(insert);
    if (insert != NULL)
	xmlFreeNode(insert);
    return(ret);
}

/**
 * xsltAttrTemplateValueProcessNode:
 * @ctxt:  the XSLT transformation context
 * @str:  the attribute template node value
 * @inst:  the instruction (or LRE) in the stylesheet holding the
 *         attribute with an AVT
 *
 * Process the given string, allowing to pass a namespace mapping
 * context and return the new string value.
 *
 * Called by:
 *  - xsltAttrTemplateValueProcess() (templates.c)
 *  - xsltEvalAttrValueTemplate() (templates.c)
 *
 * QUESTION: Why is this function public? It is not used outside
 *  of templates.c.
 *
 * Returns the computed string value or NULL, must be deallocated by the
 *    caller.
 */
xmlChar *
xsltAttrTemplateValueProcessNode(xsltTransformContextPtr ctxt,
	  const xmlChar *str, xmlNodePtr inst)
{
    xmlChar *ret = NULL;
    const xmlChar *cur;
    xmlChar *expr, *val;
    xmlNsPtr *nsList = NULL;
    int nsNr = 0;

    if (str == NULL) return(NULL);
    if (*str == 0)
	return(xmlStrndup((xmlChar *)"", 0));

    cur = str;
    while (*cur != 0) {
	if (*cur == '{') {
	    if (*(cur+1) == '{') {	/* escaped '{' */
	        cur++;
		ret = xmlStrncat(ret, str, cur - str);
		cur++;
		str = cur;
		continue;
	    }
	    ret = xmlStrncat(ret, str, cur - str);
	    str = cur;
	    cur++;
	    while ((*cur != 0) && (*cur != '}')) {
		/* Need to check for literal (bug539741) */
		if ((*cur == '\'') || (*cur == '"')) {
		    char delim = *(cur++);
		    while ((*cur != 0) && (*cur != delim))
			cur++;
		    if (*cur != 0)
			cur++;	/* skip the ending delimiter */
		} else
		    cur++;
            }
	    if (*cur == 0) {
	        xsltTransformError(ctxt, NULL, inst,
			"xsltAttrTemplateValueProcessNode: unmatched '{'\n");
		ret = xmlStrncat(ret, str, cur - str);
		goto exit;
	    }
	    str++;
	    expr = xmlStrndup(str, cur - str);
	    if (expr == NULL)
		goto exit;
	    else if (*expr == '{') {
		ret = xmlStrcat(ret, expr);
		xmlFree(expr);
	    } else {
		xmlXPathCompExprPtr comp;
		/*
		 * TODO: keep precompiled form around
		 */
		if ((nsList == NULL) && (inst != NULL)) {
		    int i = 0;

		    nsList = xmlGetNsList(xmlNodeGetDoc(inst), inst);
		    if (nsList != NULL) {
			while (nsList[i] != NULL)
			    i++;
			nsNr = i;
		    }
		}
		comp = xmlXPathCtxtCompile(ctxt->xpathCtxt, expr);
                val = xsltEvalXPathStringNs(ctxt, comp, nsNr, nsList);
		xmlXPathFreeCompExpr(comp);
		xmlFree(expr);
		if (val != NULL) {
		    ret = xmlStrcat(ret, val);
		    xmlFree(val);
		}
	    }
	    cur++;
	    str = cur;
	} else if (*cur == '}') {
	    cur++;
	    if (*cur == '}') {	/* escaped '}' */
		ret = xmlStrncat(ret, str, cur - str);
		cur++;
		str = cur;
		continue;
	    } else {
	        xsltTransformError(ctxt, NULL, inst,
		     "xsltAttrTemplateValueProcessNode: unmatched '}'\n");
	    }
	} else
	    cur++;
    }
    if (cur != str) {
	ret = xmlStrncat(ret, str, cur - str);
    }

exit:
    if (nsList != NULL)
	xmlFree(nsList);

    return(ret);
}

/**
 * xsltAttrTemplateValueProcess:
 * @ctxt:  the XSLT transformation context
 * @str:  the attribute template node value
 *
 * Process the given node and return the new string value.
 *
 * Returns the computed string value or NULL, must be deallocated by the
 *    caller.
 */
xmlChar *
xsltAttrTemplateValueProcess(xsltTransformContextPtr ctxt, const xmlChar *str) {
    return(xsltAttrTemplateValueProcessNode(ctxt, str, NULL));
}

/**
 * xsltEvalAttrValueTemplate:
 * @ctxt:  the XSLT transformation context
 * @inst:  the instruction (or LRE) in the stylesheet holding the
 *         attribute with an AVT
 * @name:  the attribute QName
 * @ns:  the attribute namespace URI
 *
 * Evaluate a attribute value template, i.e. the attribute value can
 * contain expressions contained in curly braces ({}) and those are
 * substituted by they computed value.
 *
 * Returns the computed string value or NULL, must be deallocated by the
 *    caller.
 */
xmlChar *
xsltEvalAttrValueTemplate(xsltTransformContextPtr ctxt, xmlNodePtr inst,
	                  const xmlChar *name, const xmlChar *ns)
{
    xmlChar *ret;
    xmlChar *expr;

    if ((ctxt == NULL) || (inst == NULL) || (name == NULL) ||
        (xmlNodeGetType(inst) != XML_ELEMENT_NODE))
	return(NULL);

    expr = xsltGetNsProp(inst, name, ns);
    if (expr == NULL)
	return(NULL);

    /*
     * TODO: though now {} is detected ahead, it would still be good to
     *       optimize both functions to keep the splitted value if the
     *       attribute content and the XPath precompiled expressions around
     */

    ret = xsltAttrTemplateValueProcessNode(ctxt, expr, inst);
#ifdef WITH_XSLT_DEBUG_TEMPLATES
    XSLT_TRACE(ctxt,XSLT_TRACE_TEMPLATES,xsltGenericDebug(xsltGenericDebugContext,
	 "xsltEvalAttrValueTemplate: %s returns %s\n", expr, ret));
#endif
    if (expr != NULL)
	xmlFree(expr);
    return(ret);
}

/**
 * xsltEvalStaticAttrValueTemplate:
 * @style:  the XSLT stylesheet
 * @inst:  the instruction (or LRE) in the stylesheet holding the
 *         attribute with an AVT
 * @name:  the attribute Name
 * @ns:  the attribute namespace URI
 * @found:  indicator whether the attribute is present
 *
 * Check if an attribute value template has a static value, i.e. the
 * attribute value does not contain expressions contained in curly braces ({})
 *
 * Returns the static string value or NULL, must be deallocated by the
 *    caller.
 */
const xmlChar *
xsltEvalStaticAttrValueTemplate(xsltStylesheetPtr style, xmlNodePtr inst,
			const xmlChar *name, const xmlChar *ns, int *found) {
    const xmlChar *ret;
    xmlChar *expr;

    if ((style == NULL) || (inst == NULL) || (name == NULL) ||
        (xmlNodeGetType(inst) != XML_ELEMENT_NODE))
	return(NULL);

    expr = xsltGetNsProp(inst, name, ns);
    if (expr == NULL) {
	*found = 0;
	return(NULL);
    }
    *found = 1;

    ret = xmlStrchr(expr, '{');
    if (ret != NULL) {
	xmlFree(expr);
	return(NULL);
    }
    ret = xmlDictLookup(style->dict, expr, -1);
    xmlFree(expr);
    return(ret);
}

/**
 * xsltAttrTemplateProcess:
 * @ctxt:  the XSLT transformation context
 * @target:  the element where the attribute will be grafted
 * @attr:  the attribute node of a literal result element
 *
 * Process one attribute of a Literal Result Element (in the stylesheet).
 * Evaluates Attribute Value Templates and copies the attribute over to
 * the result element.
 * This does *not* process attribute sets (xsl:use-attribute-set).
 *
 *
 * Returns the generated attribute node.
 */
xmlAttrPtr
xsltAttrTemplateProcess(xsltTransformContextPtr ctxt, xmlNodePtr target,
	                xmlAttrPtr attr)
{
    const xmlChar *value;
    xmlAttrPtr ret;

    if ((ctxt == NULL) || (attr == NULL) || (target == NULL) ||
        (xmlNodeGetType(target) != XML_ELEMENT_NODE))
	return(NULL);

    if (xmlAttrGetType(attr) != XML_ATTRIBUTE_NODE)
	return(NULL);

    /*
    * Skip all XSLT attributes.
    */
#ifdef XSLT_REFACTORED
    if (xmlAttrGetPsvi(attr) == xsltXSLTAttrMarker)
	return(NULL);
#else
    if ((xmlAttrGetNs(attr) != NULL) &&
        xmlStrEqual(xmlNsGetHref(xmlAttrGetNs(attr)), XSLT_NAMESPACE))
	return(NULL);
#endif
    /*
    * Get the value.
    */
    if (xmlAttrGetChildren(attr) != NULL) {
	if ((xmlNodeGetType(xmlAttrGetChildren(attr)) != XML_TEXT_NODE) ||
	    (xmlNodeGetNext(xmlAttrGetChildren(attr)) != NULL))
	{
	    xsltTransformError(ctxt, NULL, xmlAttrGetParent(attr),
		"Internal error: The children of an attribute node of a "
		"literal result element are not in the expected form.\n");
	    return(NULL);
	}
	value = xmlNodeGetContentRaw(xmlAttrGetChildren(attr));
	if (value == NULL)
	    value = xmlDictLookup(ctxt->dict, BAD_CAST "", 0);
    } else
	value = xmlDictLookup(ctxt->dict, BAD_CAST "", 0);
    /*
    * Overwrite duplicates.
    */
    ret = xmlNodeGetProperties(target);
    while (ret != NULL) {
        if (((xmlAttrGetNs(attr) != NULL) == (xmlAttrGetNs(ret) != NULL)) &&
	    xmlStrEqual(xmlAttrGetName(ret), xmlAttrGetName(attr)) &&
	    ((xmlAttrGetNs(attr) == NULL) ||
	     xmlStrEqual(xmlNsGetHref(xmlAttrGetNs(ret)),
	                 xmlNsGetHref(xmlAttrGetNs(attr)))))
	{
	    break;
	}
        ret = xmlAttrGetNext(ret);
    }
    if (ret != NULL) {
        /* free the existing value */
	xmlFreeNodeList(xmlAttrGetChildren(ret));
	xmlAttrSetChildren(ret, NULL);
	xmlAttrSetLast(ret, NULL);
	/*
	* Adjust ns-prefix if needed.
	*/
	if ((xmlAttrGetNs(ret) != NULL) &&
	    (! xmlStrEqual(xmlNsGetPrefix(xmlAttrGetNs(ret)),
	                   xmlNsGetPrefix(xmlAttrGetNs(attr)))))
	{
	    xmlAttrSetNs(ret, xsltGetNamespace(ctxt, xmlAttrGetParent(attr),
	        xmlAttrGetNs(attr), target));
	}
    } else {
        /* create a new attribute */
	if (xmlAttrGetNs(attr) != NULL)
	    ret = xmlNewNsProp(target,
		xsltGetNamespace(ctxt, xmlAttrGetParent(attr),
		    xmlAttrGetNs(attr), target),
		    xmlAttrGetName(attr), NULL);
	else
	    ret = xmlNewNsProp(target, NULL, xmlAttrGetName(attr), NULL);
    }
    /*
    * Set the value.
    */
    if (ret != NULL) {
        xmlNodePtr text;

        text = xmlNewText(NULL);
	if (text != NULL) {
	    xmlAttrSetLast(ret, text);
	    xmlAttrSetChildren(ret, text);
	    xmlNodeSetParent(text, (xmlNodePtr) ret);
	    xmlNodeSetDocRaw(text, xmlAttrGetDoc(ret));

	    if (xmlAttrGetPsvi(attr) != NULL) {
		/*
		* Evaluate the Attribute Value Template.
		*/
		xmlChar *val;
		val = xsltEvalAVT(ctxt, xmlAttrGetPsvi(attr),
		    xmlAttrGetParent(attr));
		if (val == NULL) {
		    /*
		    * TODO: Damn, we need an easy mechanism to report
		    * qualified names!
		    */
		    if (xmlAttrGetNs(attr)) {
			xsltTransformError(ctxt, NULL, xmlAttrGetParent(attr),
			    "Internal error: Failed to evaluate the AVT "
			    "of attribute '{%s}%s'.\n",
			    xmlNsGetHref(xmlAttrGetNs(attr)),
			    xmlAttrGetName(attr));
		    } else {
			xsltTransformError(ctxt, NULL, xmlAttrGetParent(attr),
			    "Internal error: Failed to evaluate the AVT "
			    "of attribute '%s'.\n",
			    xmlAttrGetName(attr));
		    }
		    xmlNodeSetContentRaw(text, xmlStrdup(BAD_CAST ""));
		} else {
		    xmlNodeSetContentRaw(text, val);
		}
	    } else if ((ctxt->internalized) && (target != NULL) &&
	               (xmlNodeGetDoc(target) != NULL) &&
		       (xmlNodeGetDoc(target)->dict == ctxt->dict) &&
		       xmlDictOwns(ctxt->dict, value)) {
		xmlNodeSetContentRaw(text, (xmlChar *) value);
	    } else {
		xmlNodeSetContentRaw(text, xmlStrdup(value));
	    }
	}
    } else {
	if (xmlAttrGetNs(attr)) {
	    xsltTransformError(ctxt, NULL, xmlAttrGetParent(attr),
		"Internal error: Failed to create attribute '{%s}%s'.\n",
		xmlNsGetHref(xmlAttrGetNs(attr)), xmlAttrGetName(attr));
	} else {
	    xsltTransformError(ctxt, NULL, xmlAttrGetParent(attr),
		"Internal error: Failed to create attribute '%s'.\n",
		xmlAttrGetName(attr));
	}
    }
    return(ret);
}


/**
 * xsltAttrListTemplateProcess:
 * @ctxt:  the XSLT transformation context
 * @target:  the element where the attributes will be grafted
 * @attrs:  the first attribute
 *
 * Processes all attributes of a Literal Result Element.
 * Attribute references are applied via xsl:use-attribute-set
 * attributes.
 * Copies all non XSLT-attributes over to the @target element
 * and evaluates Attribute Value Templates.
 *
 * Called by xsltApplySequenceConstructor() (transform.c).
 *
 * Returns a new list of attribute nodes, or NULL in case of error.
 *         (Don't assign the result to @target->properties; if
 *         the result is NULL, you'll get memory leaks, since the
 *         attributes will be disattached.)
 */
xmlAttrPtr
xsltAttrListTemplateProcess(xsltTransformContextPtr ctxt,
	                    xmlNodePtr target, xmlAttrPtr attrs)
{
    xmlAttrPtr attr, copy, last = NULL;
    xmlNodePtr oldInsert, text;
    xmlNsPtr origNs = NULL, copyNs = NULL;
    const xmlChar *value;
    xmlChar *valueAVT;
    int hasAttr = 0;

    if ((ctxt == NULL) || (target == NULL) || (attrs == NULL) ||
        (xmlNodeGetType(target) != XML_ELEMENT_NODE))
	return(NULL);

    oldInsert = ctxt->insert;
    ctxt->insert = target;

    /*
    * Apply attribute-sets.
    */
    attr = attrs;
    do {
#ifdef XSLT_REFACTORED
	if ((xmlAttrGetPsvi(attr) == xsltXSLTAttrMarker) &&
	    xmlStrEqual(xmlAttrGetName(attr), (const xmlChar *)"use-attribute-sets"))
	{
	    xsltApplyAttributeSet(ctxt, ctxt->node, (xmlNodePtr) attr, NULL);
	}
#else
	if ((xmlAttrGetNs(attr) != NULL) &&
	    xmlStrEqual(xmlAttrGetName(attr), (const xmlChar *)"use-attribute-sets") &&
	    xmlStrEqual(xmlNsGetHref(xmlAttrGetNs(attr)), XSLT_NAMESPACE))
	{
	    xsltApplyAttributeSet(ctxt, ctxt->node, (xmlNodePtr) attr, NULL);
	}
#endif
	attr = xmlAttrGetNext(attr);
    } while (attr != NULL);

    if (xmlNodeGetProperties(target) != NULL) {
        hasAttr = 1;
    }

    /*
    * Instantiate LRE-attributes.
    */
    attr = attrs;
    do {
	/*
	* Skip XSLT attributes.
	*/
#ifdef XSLT_REFACTORED
	if (xmlAttrGetPsvi(attr) == xsltXSLTAttrMarker) {
	    goto next_attribute;
	}
#else
	if ((xmlAttrGetNs(attr) != NULL) &&
	    xmlStrEqual(xmlNsGetHref(xmlAttrGetNs(attr)), XSLT_NAMESPACE))
	{
	    goto next_attribute;
	}
#endif
	/*
	* Get the value.
	*/
	if (xmlAttrGetChildren(attr) != NULL) {
	    if ((xmlNodeGetType(xmlAttrGetChildren(attr)) != XML_TEXT_NODE) ||
		(xmlNodeGetNext(xmlAttrGetChildren(attr)) != NULL))
	    {
		xsltTransformError(ctxt, NULL, xmlAttrGetParent(attr),
		    "Internal error: The children of an attribute node of a "
		    "literal result element are not in the expected form.\n");
		goto error;
	    }
	    value = xmlNodeGetContentRaw(xmlAttrGetChildren(attr));
	    if (value == NULL)
		value = xmlDictLookup(ctxt->dict, BAD_CAST "", 0);
	} else
	    value = xmlDictLookup(ctxt->dict, BAD_CAST "", 0);

	/*
	* Get the namespace. Avoid lookups of same namespaces.
	*/
	if (xmlAttrGetNs(attr) != origNs) {
	    origNs = xmlAttrGetNs(attr);
	    if (xmlAttrGetNs(attr) != NULL) {
#ifdef XSLT_REFACTORED
		copyNs = xsltGetSpecialNamespace(ctxt, xmlAttrGetParent(attr),
		    xmlNsGetHref(xmlAttrGetNs(attr)),
		    xmlNsGetPrefix(xmlAttrGetNs(attr)), target);
#else
		copyNs = xsltGetNamespace(ctxt, xmlAttrGetParent(attr),
		    xmlAttrGetNs(attr), target);
#endif
		if (copyNs == NULL)
		    goto error;
	    } else
		copyNs = NULL;
	}
	/*
	* Create a new attribute.
	*/
        if (hasAttr) {
	    copy = xmlSetNsProp(target, copyNs, xmlAttrGetName(attr), NULL);
        } else {
            /*
            * Avoid checking for duplicate attributes if there aren't
            * any attribute sets.
            */
	    copy = xmlNewDocProp(xmlNodeGetDoc(target), xmlAttrGetName(attr), NULL);

	    if (copy != NULL) {
                xmlAttrSetNs(copy, copyNs);

                /*
                * Attach it to the target element.
                */
                xmlAttrSetParent(copy, target);
                if (last == NULL) {
                    xmlNodeSetProperties(target, copy);
                    last = copy;
                } else {
                    xmlAttrSetNext(last, copy);
                    xmlAttrSetPrev(copy, last);
                    last = copy;
                }
            }
        }
	if (copy == NULL) {
	    if (xmlAttrGetNs(attr)) {
		xsltTransformError(ctxt, NULL, xmlAttrGetParent(attr),
		    "Internal error: Failed to create attribute '{%s}%s'.\n",
		    xmlNsGetHref(xmlAttrGetNs(attr)), xmlAttrGetName(attr));
	    } else {
		xsltTransformError(ctxt, NULL, xmlAttrGetParent(attr),
		    "Internal error: Failed to create attribute '%s'.\n",
		    xmlAttrGetName(attr));
	    }
	    goto error;
	}

	/*
	* Set the value.
	*/
	text = xmlNewText(NULL);
	if (text != NULL) {
	    xmlAttrSetLast(copy, text);
	    xmlAttrSetChildren(copy, text);
	    xmlNodeSetParent(text, (xmlNodePtr) copy);
	    xmlNodeSetDocRaw(text, xmlAttrGetDoc(copy));

	    if (xmlAttrGetPsvi(attr) != NULL) {
		/*
		* Evaluate the Attribute Value Template.
		*/
		valueAVT = xsltEvalAVT(ctxt, xmlAttrGetPsvi(attr),
		    xmlAttrGetParent(attr));
		if (valueAVT == NULL) {
		    /*
		    * TODO: Damn, we need an easy mechanism to report
		    * qualified names!
		    */
		    if (xmlAttrGetNs(attr)) {
			xsltTransformError(ctxt, NULL, xmlAttrGetParent(attr),
			    "Internal error: Failed to evaluate the AVT "
			    "of attribute '{%s}%s'.\n",
			    xmlNsGetHref(xmlAttrGetNs(attr)),
			    xmlAttrGetName(attr));
		    } else {
			xsltTransformError(ctxt, NULL, xmlAttrGetParent(attr),
			    "Internal error: Failed to evaluate the AVT "
			    "of attribute '%s'.\n",
			    xmlAttrGetName(attr));
		    }
		    xmlNodeSetContentRaw(text, xmlStrdup(BAD_CAST ""));
		    goto error;
		} else {
		    xmlNodeSetContentRaw(text, valueAVT);
		}
	    } else if ((ctxt->internalized) &&
		(xmlNodeGetDoc(target) != NULL) &&
		(xmlNodeGetDoc(target)->dict == ctxt->dict) &&
		xmlDictOwns(ctxt->dict, value))
	    {
		xmlNodeSetContentRaw(text, (xmlChar *) value);
	    } else {
		xmlNodeSetContentRaw(text, xmlStrdup(value));
	    }
            if ((copy != NULL) && (text != NULL) &&
                (xmlIsID(xmlAttrGetDoc(copy), xmlAttrGetParent(copy), copy)))
                xmlAddID(NULL, xmlAttrGetDoc(copy),
                    xmlNodeGetContentRaw(text), copy);
	}

next_attribute:
	attr = xmlAttrGetNext(attr);
    } while (attr != NULL);

    ctxt->insert = oldInsert;
    return(xmlNodeGetProperties(target));

error:
    ctxt->insert = oldInsert;
    return(NULL);
}


/**
 * xsltTemplateProcess:
 * @ctxt:  the XSLT transformation context
 * @node:  the attribute template node
 *
 * Obsolete. Don't use it.
 *
 * Returns NULL.
 */
xmlNodePtr *
xsltTemplateProcess(xsltTransformContextPtr ctxt UNUSED, xmlNodePtr node) {
    if (node == NULL)
	return(NULL);

    return(0);
}


