/*
 * dynamic.c: Implementation of the EXSLT -- Dynamic module
 *
 * References:
 *   http://www.exslt.org/dyn/dyn.html
 *
 * See Copyright for the status of this software.
 *
 * Authors:
 *   Mark Vakoc <mark_vakoc@jdedwards.com>
 *   Thomas Broyer <tbroyer@ltgt.net>
 *
 * TODO:
 * elements:
 * functions:
 *    min
 *    max
 *    sum
 *    map
 *    closure
 */

#define IN_LIBEXSLT
#include "libexslt/libexslt.h"

#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include <libxslt/xsltutils.h>
#include <libxslt/xsltInternals.h>
#include <libxslt/extensions.h>

#include "exslt.h"

/**
 * exsltDynEvaluateFunction:
 * @ctxt:  an XPath parser context
 * @nargs:  the number of arguments
 *
 * Evaluates the string as an XPath expression and returns the result
 * value, which may be a boolean, number, string, node set, result tree
 * fragment or external object.
 */

static void
exsltDynEvaluateFunction(xmlXPathParserContextPtr ctxt, int nargs) {
	xmlChar *str = NULL;
	xmlXPathObjectPtr ret = NULL;

	if (ctxt == NULL)
		return;
	if (nargs != 1) {
		xsltPrintErrorContext(xsltXPathGetTransformContext(ctxt), NULL, NULL);
        xsltGenericError(xsltGenericErrorContext,
			"dyn:evalute() : invalid number of args %d\n", nargs);
		xmlXPathParserContextSetError(ctxt, XPATH_INVALID_ARITY);
		return;
	}
	str = xmlXPathPopString(ctxt);
	/* return an empty node-set if an empty string is passed in */
	if (!str||!xmlStrlen(str)) {
		if (str) xmlFree(str);
		valuePush(ctxt,xmlXPathNewNodeSet(NULL));
		return;
	}
#if LIBXML_VERSION >= 20911
        /*
         * Recursive evaluation can grow the call stack quickly.
         */
        xmlXPathContextSetDepth(xmlXPathParserContextGetContext(ctxt), xmlXPathContextGetDepth(xmlXPathParserContextGetContext(ctxt)) + 5);
#endif
	ret = xmlXPathEval(str,xmlXPathParserContextGetContext(ctxt));
#if LIBXML_VERSION >= 20911
        xmlXPathContextSetDepth(xmlXPathParserContextGetContext(ctxt), xmlXPathContextGetDepth(xmlXPathParserContextGetContext(ctxt)) - 5);
#endif
	if (ret)
		valuePush(ctxt,ret);
	else {
		xsltGenericError(xsltGenericErrorContext,
			"dyn:evaluate() : unable to evaluate expression '%s'\n",str);
		valuePush(ctxt,xmlXPathNewNodeSet(NULL));
	}
	xmlFree(str);
	return;
}

/**
 * exsltDynMapFunction:
 * @ctxt:  an XPath parser context
 * @nargs:  the number of arguments
 *
 * Evaluates the string as an XPath expression and returns the result
 * value, which may be a boolean, number, string, node set, result tree
 * fragment or external object.
 */

static void
exsltDynMapFunction(xmlXPathParserContextPtr ctxt, int nargs)
{
    xmlChar *str = NULL;
    xmlNodeSetPtr nodeset = NULL;
    xsltTransformContextPtr tctxt;
    xmlXPathCompExprPtr comp = NULL;
    xmlXPathObjectPtr ret = NULL;
    xmlDocPtr oldDoc, container = NULL;
    xmlNodePtr oldNode;
    int oldContextSize;
    int oldProximityPosition;
    int i, j;


    if (nargs != 2) {
        xmlXPathSetArityError(ctxt);
        return;
    }
    str = xmlXPathPopString(ctxt);
    if (xmlXPathCheckError(ctxt))
        goto cleanup;

    nodeset = xmlXPathPopNodeSet(ctxt);
    if (xmlXPathCheckError(ctxt))
        goto cleanup;

    ret = xmlXPathNewNodeSet(NULL);
    if (ret == NULL) {
        xsltGenericError(xsltGenericErrorContext,
                         "exsltDynMapFunction: ret == NULL\n");
        goto cleanup;
    }

    tctxt = xsltXPathGetTransformContext(ctxt);
    if (tctxt == NULL) {
	xsltTransformError(xsltXPathGetTransformContext(ctxt), NULL, NULL,
	      "dyn:map : internal error tctxt == NULL\n");
	goto cleanup;
    }

    if (str == NULL || !xmlStrlen(str) ||
        !(comp = xmlXPathCtxtCompile(tctxt->xpathCtxt, str)))
        goto cleanup;

    oldDoc = xmlXPathContextGetDoc(xmlXPathParserContextGetContext(ctxt));
    oldNode = xmlXPathContextGetNode(xmlXPathParserContextGetContext(ctxt));
    oldContextSize = xmlXPathContextGetContextSize(xmlXPathParserContextGetContext(ctxt));
    oldProximityPosition = xmlXPathContextGetProximityPosition(xmlXPathParserContextGetContext(ctxt));

        /**
	 * since we really don't know we're going to be adding node(s)
	 * down the road we create the RVT regardless
	 */
    container = xsltCreateRVT(tctxt);
    if (container == NULL) {
	xsltTransformError(tctxt, NULL, NULL,
	      "dyn:map : internal error container == NULL\n");
	goto cleanup;
    }
    xsltRegisterLocalRVT(tctxt, container);
    if (nodeset && xmlNodeSetGetNodeNr(nodeset) > 0) {
        xmlXPathNodeSetSort(nodeset);
        xmlXPathContextSetContextSize(xmlXPathParserContextGetContext(ctxt), xmlNodeSetGetNodeNr(nodeset));
        xmlXPathContextSetProximityPosition(xmlXPathParserContextGetContext(ctxt), 0);
        for (i = 0; i < xmlNodeSetGetNodeNr(nodeset); i++) {
            xmlXPathObjectPtr subResult = NULL;
            xmlNodePtr cur = xmlNodeSetGetNodeEntry(nodeset, i);

            xmlXPathContextSetProximityPosition(xmlXPathParserContextGetContext(ctxt),
                xmlXPathContextGetProximityPosition(xmlXPathParserContextGetContext(ctxt)) + 1);
            xmlXPathContextSetNode(xmlXPathParserContextGetContext(ctxt), cur);

            if (xmlNodeGetType(cur) == XML_NAMESPACE_DECL) {
                /*
                * The XPath module sets the owner element of a ns-node on
                * the ns->next field.
                */
                cur = (xmlNodePtr) xmlNsGetNext((xmlNsPtr) cur);
                if ((cur == NULL) || (xmlNodeGetType(cur) != XML_ELEMENT_NODE)) {
                    xsltGenericError(xsltGenericErrorContext,
                        "Internal error in exsltDynMapFunction: "
                        "Cannot retrieve the doc of a namespace node.\n");
                    continue;
                }
                xmlXPathContextSetDoc(xmlXPathParserContextGetContext(ctxt), xmlNodeGetDoc(cur));
            } else {
                xmlXPathContextSetDoc(xmlXPathParserContextGetContext(ctxt), xmlNodeGetDoc(cur));
            }

            subResult = xmlXPathCompiledEval(comp, xmlXPathParserContextGetContext(ctxt));
            if (subResult != NULL) {
                switch (xmlXPathObjectGetType(subResult)) {
                    case XPATH_NODESET:
                        if (xmlXPathObjectGetNodesetval(subResult) != NULL)
                            for (j = 0;
                                 j < xmlNodeSetGetNodeNr(xmlXPathObjectGetNodesetval(subResult));
                                 j++)
                                xmlXPathNodeSetAdd(xmlXPathObjectGetNodesetval(ret),
                                                   xmlNodeSetGetNodeEntry(
                                                   xmlXPathObjectGetNodesetval(subResult), j));
                        break;
                    case XPATH_BOOLEAN:
                        if (container != NULL) {
                            xmlNodePtr newChildNode =
                                xmlNewTextChild((xmlNodePtr) container, NULL,
                                                BAD_CAST "boolean",
                                                BAD_CAST (xmlXPathObjectGetBoolval(subResult)
                                                ? "true" : ""));
                            if (newChildNode != NULL) {
                                xmlNodeSetNs(newChildNode,
                                    xmlNewNs(newChildNode,
                                             BAD_CAST
                                             "http://exslt.org/common",
                                             BAD_CAST "exsl"));
                                xmlXPathNodeSetAddUnique(xmlXPathObjectGetNodesetval(ret),
                                                         newChildNode);
                            }
                        }
                        break;
                    case XPATH_NUMBER:
                        if (container != NULL) {
                            xmlChar *val =
                                xmlXPathCastNumberToString(
                                    xmlXPathObjectGetFloatval(subResult));
                            xmlNodePtr newChildNode =
                                xmlNewTextChild((xmlNodePtr) container, NULL,
                                                BAD_CAST "number", val);
                            if (val != NULL)
                                xmlFree(val);

                            if (newChildNode != NULL) {
                                xmlNodeSetNs(newChildNode,
                                    xmlNewNs(newChildNode,
                                             BAD_CAST
                                             "http://exslt.org/common",
                                             BAD_CAST "exsl"));
                                xmlXPathNodeSetAddUnique(xmlXPathObjectGetNodesetval(ret),
                                                         newChildNode);
                            }
                        }
                        break;
                    case XPATH_STRING:
                        if (container != NULL) {
                            xmlNodePtr newChildNode =
                                xmlNewTextChild((xmlNodePtr) container, NULL,
                                                BAD_CAST "string",
                                                xmlXPathObjectGetStringval(subResult));
                            if (newChildNode != NULL) {
                                xmlNodeSetNs(newChildNode,
                                    xmlNewNs(newChildNode,
                                             BAD_CAST
                                             "http://exslt.org/common",
                                             BAD_CAST "exsl"));
                                xmlXPathNodeSetAddUnique(xmlXPathObjectGetNodesetval(ret),
                                                         newChildNode);
                            }
                        }
                        break;
		    default:
                        break;
                }
                xmlXPathFreeObject(subResult);
            }
        }
    }
    xmlXPathContextSetDoc(xmlXPathParserContextGetContext(ctxt), oldDoc);
    xmlXPathContextSetNode(xmlXPathParserContextGetContext(ctxt), oldNode);
    xmlXPathContextSetContextSize(xmlXPathParserContextGetContext(ctxt), oldContextSize);
    xmlXPathContextSetProximityPosition(xmlXPathParserContextGetContext(ctxt), oldProximityPosition);


  cleanup:
    /* restore the xpath context */
    if (comp != NULL)
        xmlXPathFreeCompExpr(comp);
    if (nodeset != NULL)
        xmlXPathFreeNodeSet(nodeset);
    if (str != NULL)
        xmlFree(str);
    valuePush(ctxt, ret);
    return;
}


/**
 * exsltDynRegister:
 *
 * Registers the EXSLT - Dynamic module
 */

void
exsltDynRegister (void) {
    xsltRegisterExtModuleFunction ((const xmlChar *) "evaluate",
				   EXSLT_DYNAMIC_NAMESPACE,
				   exsltDynEvaluateFunction);
  xsltRegisterExtModuleFunction ((const xmlChar *) "map",
				   EXSLT_DYNAMIC_NAMESPACE,
				   exsltDynMapFunction);

}
