/*
 * preproc.c: Preprocessing of style operations
 *
 * References:
 *   http://www.w3.org/TR/1999/REC-xslt-19991116
 *
 *   Michael Kay "XSLT Programmer's Reference" pp 637-643
 *   Writing Multiple Output Files
 *
 *   XSLT-1.1 Working Draft
 *   http://www.w3.org/TR/xslt11#multiple-output
 *
 * See Copyright for the status of this software.
 *
 * daniel@veillard.com
 */

#define IN_LIBXSLT
#include "libxslt.h"

#include <string.h>

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/valid.h>
#include <libxml/hash.h>
#include <libxml/uri.h>
#include <libxml/encoding.h>
#include <libxml/xmlerror.h>
#include "xslt.h"
#include "xsltutils.h"
#include "xsltInternals.h"
#include "transform.h"
#include "templates.h"
#include "variables.h"
#include "numbersInternals.h"
#include "preproc.h"
#include "extra.h"
#include "imports.h"
#include "extensions.h"
#include "pattern.h"

#ifdef WITH_XSLT_VERBOSE
#define WITH_XSLT_VERBOSE_PREPROC 1
#endif

#include "xsltverbose.h"

const xmlChar *xsltExtMarker = (const xmlChar *) "Extension Element";

/************************************************************************
 *									*
 *			Grammar checks					*
 *									*
 ************************************************************************/

/**
 * xsltCheckTopLevelElement:
 * @style: the XSLT stylesheet
 * @inst: the XSLT instruction
 * @err: raise an error or not
 *
 * Check that the instruction is instanciated as a top level element.
 *
 * Returns -1 in case of error, 0 if failed and 1 in case of success
 */
static int
xsltCheckTopLevelElement (xsltStylesheetPtr style, xmlNodePtr inst, int err)
{
    xmlNodePtr parent;
    if (style == NULL || inst == NULL || xmlNodeGetNs(inst) == NULL)
        return -1;

    parent = xmlNodeGetParent(inst);
    if (parent == NULL) {
        if (err) {
            xsltTransformError(NULL, style, inst,
                               "internal problem: element has no parent\n");
            style->errors++;
        }
        return 0;
    }
    if (xmlNodeGetNs(parent) == NULL
        || xmlNodeGetType(parent) != XML_ELEMENT_NODE
        || xmlNodeGetNs(parent) != xmlNodeGetNs(inst)
            && !xmlStrEqual(xmlNsGetHref(xmlNodeGetNs(parent)),
                             xmlNsGetHref(xmlNodeGetNs(inst)))
        || !xmlStrEqual(xmlNodeGetName(parent), BAD_CAST "stylesheet")
            && !xmlStrEqual(xmlNodeGetName(parent), BAD_CAST "transform")) {
        if (err) {
            xsltTransformError(
                NULL, style, inst,
                "element %s only allowed as child of stylesheet\n",
                xmlNodeGetName(inst));
            style->errors++;
        }
        return 0;
    }
    return 1;
}

/**
 * xsltCheckInstructionElement:
 * @style: the XSLT stylesheet
 * @inst: the XSLT instruction
 *
 * Check that the instruction is instanciated as an instruction element.
 */
static void
xsltCheckInstructionElement (xsltStylesheetPtr style, xmlNodePtr inst)
{
    xmlNodePtr parent;
    int has_ext;

    if (style == NULL || inst == NULL || xmlNodeGetNs(inst) == NULL
        || style->literal_result)
        return;

    has_ext = (style->extInfos != NULL);

    parent = xmlNodeGetParent(inst);
    if (parent == NULL) {
        xsltTransformError(NULL, style, inst,
                           "internal problem: element has no parent\n");
        style->errors++;
        return;
    }
    while (parent != NULL && xmlNodeGetType(parent) != XML_DOCUMENT_NODE) {
        if (((xmlNodeGetNs(parent) == xmlNodeGetNs(inst))
             || ((xmlNodeGetNs(parent) != NULL)
                 && (xmlStrEqual(xmlNsGetHref(xmlNodeGetNs(parent)),
                                 xmlNsGetHref(xmlNodeGetNs(inst))))))
            && ((xmlStrEqual(xmlNodeGetName(parent), BAD_CAST "template"))
                || (xmlStrEqual(xmlNodeGetName(parent), BAD_CAST "param"))
                || (xmlStrEqual(xmlNodeGetName(parent), BAD_CAST "attribute"))
                || (xmlStrEqual(xmlNodeGetName(parent),
                                BAD_CAST "variable")))) {
            return;
        }

        /*
	 * if we are within an extension element all bets are off
	 * about the semantic there e.g. xsl:param within func:function
	 */
        if (has_ext && xmlNodeGetNs(parent) != NULL
            && xmlHashLookup(style->extInfos,
                              xmlNsGetHref(xmlNodeGetNs(parent)))
                != NULL)
            return;

        parent = xmlNodeGetParent(parent);
    }
    xsltTransformError(
        NULL, style, inst,
        "element %s only allowed within a template, variable or param\n",
        xmlNodeGetName(inst));
    style->errors++;
}

/**
 * xsltCheckParentElement:
 * @style: the XSLT stylesheet
 * @inst: the XSLT instruction
 * @allow1: allowed parent1
 * @allow2: allowed parent2
 *
 * Check that the instruction is instanciated as the childre of one of the
 * possible parents.
 */
static void
xsltCheckParentElement (xsltStylesheetPtr style, xmlNodePtr inst,
                        const xmlChar *allow1, const xmlChar *allow2)
{
    xmlNodePtr parent;

    if (style == NULL || inst == NULL || xmlNodeGetNs(inst) == NULL
        || style->literal_result)
        return;

    parent = xmlNodeGetParent(inst);
    if (parent == NULL) {
        xsltTransformError(NULL, style, inst,
                           "internal problem: element has no parent\n");
        style->errors++;
        return;
    }
    if (((xmlNodeGetNs(parent) == xmlNodeGetNs(inst))
         || ((xmlNodeGetNs(parent) != NULL)
             && (xmlStrEqual(xmlNsGetHref(xmlNodeGetNs(parent)),
                             xmlNsGetHref(xmlNodeGetNs(inst))))))
        && ((xmlStrEqual(xmlNodeGetName(parent), allow1))
            || (xmlStrEqual(xmlNodeGetName(parent), allow2)))) {
        return;
    }

    if (style->extInfos != NULL) {
        while (parent != NULL
               && xmlNodeGetType(parent) != XML_DOCUMENT_NODE) {
            /*
	     * if we are within an extension element all bets are off
	     * about the semantic there e.g. xsl:param within func:function
	     */
            if (xmlNodeGetNs(parent) != NULL
                && xmlHashLookup(style->extInfos,
                                  xmlNsGetHref(xmlNodeGetNs(parent)))
                    != NULL)
                return;

            parent = xmlNodeGetParent(parent);
        }
    }
    xsltTransformError(NULL, style, inst,
                       "element %s is not allowed within that context\n",
                       xmlNodeGetName(inst));
    style->errors++;
}

/************************************************************************
 *									*
 *			handling of precomputed data			*
 *									*
 ************************************************************************/

/**
 * xsltNewStylePreComp:
 * @style:  the XSLT stylesheet
 * @type:  the construct type
 *
 * Create a new XSLT Style precomputed block
 *
 * Returns the newly allocated specialized structure
 *         or NULL in case of error
 */
static xsltStylePreCompPtr
xsltNewStylePreComp (xsltStylesheetPtr style, xsltStyleType type)
{
    xsltStylePreCompPtr cur;

    if (style == NULL)
        return NULL;

    /*
    * Old behaviour.
    */
    cur = (xsltStylePreCompPtr) xmlMalloc(sizeof(xsltStylePreComp));
    if (cur == NULL) {
        xsltTransformError(NULL, style, NULL,
                           "xsltNewStylePreComp : malloc failed\n");
        style->errors++;
        return NULL;
    }
    memset(cur, 0, sizeof(xsltStylePreComp));

    /*
    * URGENT TODO: Better to move this to spezialized factory functions.
    */
    cur->type = type;
    switch (cur->type) {
    case XSLT_FUNC_COPY:
        cur->func = xsltCopy;
        break;
    case XSLT_FUNC_SORT:
        cur->func = xsltSort;
        break;
    case XSLT_FUNC_TEXT:
        cur->func = xsltText;
        break;
    case XSLT_FUNC_ELEMENT:
        cur->func = xsltElement;
        break;
    case XSLT_FUNC_ATTRIBUTE:
        cur->func = xsltAttribute;
        break;
    case XSLT_FUNC_COMMENT:
        cur->func = xsltComment;
        break;
    case XSLT_FUNC_PI:
        cur->func = xsltProcessingInstruction;
        break;
    case XSLT_FUNC_COPYOF:
        cur->func = xsltCopyOf;
        break;
    case XSLT_FUNC_VALUEOF:
        cur->func = xsltValueOf;
        break;
    case XSLT_FUNC_NUMBER:
        cur->func = xsltNumber;
        break;
    case XSLT_FUNC_APPLYIMPORTS:
        cur->func = xsltApplyImports;
        break;
    case XSLT_FUNC_CALLTEMPLATE:
        cur->func = xsltCallTemplate;
        break;
    case XSLT_FUNC_APPLYTEMPLATES:
        cur->func = xsltApplyTemplates;
        break;
    case XSLT_FUNC_CHOOSE:
        cur->func = xsltChoose;
        break;
    case XSLT_FUNC_IF:
        cur->func = xsltIf;
        break;
    case XSLT_FUNC_FOREACH:
        cur->func = xsltForEach;
        break;
    case XSLT_FUNC_DOCUMENT:
        cur->func = xsltDocumentElem;
        break;
    case XSLT_FUNC_WITHPARAM:
    case XSLT_FUNC_PARAM:
    case XSLT_FUNC_VARIABLE:
    case XSLT_FUNC_WHEN:
        break;
    default:
        if (cur->func == NULL) {
            xsltTransformError(
                NULL, style, NULL,
                "xsltNewStylePreComp : no function for type %d\n", type);
            style->errors++;
        }
    }
    cur->next = style->preComps;
    style->preComps = (xsltElemPreCompPtr) cur;

    return cur;
}

/**
 * xsltFreeStylePreComp:
 * @comp:  an XSLT Style precomputed block
 *
 * Free up the memory allocated by @comp
 */
static void
xsltFreeStylePreComp (xsltStylePreCompPtr comp)
{
    if (comp == NULL)
        return;
    if (comp->comp != NULL)
        xmlXPathFreeCompExpr(comp->comp);
    if (comp->numdata.countPat != NULL)
        xsltFreeCompMatchList(comp->numdata.countPat);
    if (comp->numdata.fromPat != NULL)
        xsltFreeCompMatchList(comp->numdata.fromPat);
    if (comp->nsList != NULL)
        xmlFree(comp->nsList);

    xmlFree(comp);
}

/************************************************************************
 *									*
 *		    XSLT-1.1 extensions					*
 *									*
 ************************************************************************/

/**
 * xsltDocumentComp:
 * @style:  the XSLT stylesheet
 * @inst:  the instruction in the stylesheet
 * @function:  unused
 *
 * Pre process an XSLT-1.1 document element
 *
 * Returns a precompiled data structure for the element
 */
xsltElemPreCompPtr
xsltDocumentComp (xsltStylesheetPtr style, xmlNodePtr inst,
                  xsltTransformFunction function UNUSED)
{
    xsltStylePreCompPtr comp;
    const xmlChar *filename = NULL;

    /*
    * As of 2006-03-30, this function is currently defined in Libxslt
    * to be used for:
    * (in libxslt/extra.c)
    * "output" in XSLT_SAXON_NAMESPACE
    * "write" XSLT_XALAN_NAMESPACE
    * "document" XSLT_XT_NAMESPACE
    * "document" XSLT_NAMESPACE (from the abandoned old working
    *                            draft of XSLT 1.1)
    * (in libexslt/common.c)
    * "document" in EXSLT_COMMON_NAMESPACE
    */
    comp = xsltNewStylePreComp(style, XSLT_FUNC_DOCUMENT);

    if (comp == NULL)
        return NULL;
    comp->inst = inst;
    comp->ver11 = 0;

    if (xmlStrEqual(xmlNodeGetName(inst), (const xmlChar *) "output")) {
        xsltVerbose(XSLT_VB_EXTRA, xsltGenericDebugContext,
                    "Found saxon:output extension\n");
        /*
	* The element "output" is in the namespace XSLT_SAXON_NAMESPACE
	*   (http://icl.com/saxon)
	* The @file is in no namespace; it is an AVT.
	*   (http://www.computerwizards.com/saxon/doc/extensions.html#saxon:output)
	*
	* TODO: Do we need not to check the namespace here?
	*/
        filename = xsltEvalStaticAttrValueTemplate(
            style, inst, (const xmlChar *) "file", NULL, &comp->has_filename);
    } else if (xmlStrEqual(xmlNodeGetName(inst), (const xmlChar *) "write")) {
        xsltVerbose(XSLT_VB_EXTRA, xsltGenericDebugContext,
                    "Found xalan:write extension\n");
        /* the filename need to be interpreted */
        /*
	* TODO: Is "filename need to be interpreted" meant to be a todo?
	*   Where will be the filename of xalan:write be processed?
	*
	* TODO: Do we need not to check the namespace here?
	*   The extension ns is "http://xml.apache.org/xalan/redirect".
	*   See http://xml.apache.org/xalan-j/extensionslib.html.
	*/
    } else if (xmlStrEqual(xmlNodeGetName(inst),
                           (const xmlChar *) "document")) {
        if (xmlNodeGetNs(inst) != NULL) {
            if (xmlStrEqual(xmlNsGetHref(xmlNodeGetNs(inst)), XSLT_NAMESPACE)) {
                /*
		* Mark the instruction as being of
		* XSLT version 1.1 (abandoned).
		*/
                comp->ver11 = 1;
                xsltVerbose(XSLT_VB_EXTRA, xsltGenericDebugContext,
                            "Found xslt11:document construct\n");
            } else {
                if (xmlStrEqual(xmlNsGetHref(xmlNodeGetNs(inst)),
                                (const xmlChar *) "http://exslt.org/common")) {
                    /* EXSLT. */
                    xsltVerbose(XSLT_VB_EXTRA, xsltGenericDebugContext,
                                "Found exslt:document extension\n");
                } else if (xmlStrEqual(xmlNsGetHref(xmlNodeGetNs(inst)),
                                       XSLT_XT_NAMESPACE)) {
                    /* James Clark's XT. */
                    xsltVerbose(XSLT_VB_EXTRA, xsltGenericDebugContext,
                                "Found xt:document extension\n");
                }
            }
        }
        /*
	* The element "document" is used in conjunction with the
	* following namespaces:
	*
	* 1) XSLT_NAMESPACE (http://www.w3.org/1999/XSL/Transform version 1.1)
	*    <!ELEMENT xsl:document %template;>
	*    <!ATTLIST xsl:document
	*       href %avt; #REQUIRED
	*    @href is an AVT
	*    IMPORTANT: xsl:document was in the abandoned XSLT 1.1 draft,
	*    it was removed and isn't available in XSLT 1.1 anymore.
	*    In XSLT 2.0 it was renamed to xsl:result-document.
	*
	*   All other attributes are identical to the attributes
	*   on xsl:output
	*
	* 2) EXSLT_COMMON_NAMESPACE (http://exslt.org/common)
	*    <exsl:document
	*       href = { uri-reference }
	*    TODO: is @href is an AVT?
	*
	* 3) XSLT_XT_NAMESPACE (http://www.jclark.com/xt)
	*     Example: <xt:document method="xml" href="myFile.xml">
	*    TODO: is @href is an AVT?
	*
	* In all cases @href is in no namespace.
	*/
        filename = xsltEvalStaticAttrValueTemplate(
            style, inst, (const xmlChar *) "href", NULL, &comp->has_filename);
    }
    if (!comp->has_filename) {
        goto error;
    }
    comp->filename = filename;

error:
    return (xsltElemPreCompPtr) comp;
}

/************************************************************************
 *									*
 *		Most of the XSLT-1.0 transformations			*
 *									*
 ************************************************************************/

/**
 * xsltSortComp:
 * @style:  the XSLT stylesheet
 * @inst:  the xslt sort node
 *
 * Process the xslt sort node on the source node
 */
static void
xsltSortComp (xsltStylesheetPtr style, xmlNodePtr inst)
{
    xsltStylePreCompPtr comp;
    if (style == NULL || inst == NULL
        || xmlNodeGetType(inst) != XML_ELEMENT_NODE)
        return;

    comp = xsltNewStylePreComp(style, XSLT_FUNC_SORT);

    if (comp == NULL)
        return;
    xmlNodeSetPsvi(inst, comp);
    comp->inst = inst;

    comp->stype = xsltEvalStaticAttrValueTemplate(
        style, inst, (const xmlChar *) "data-type", NULL, &comp->has_stype);
    if (comp->stype != NULL) {
        if (xmlStrEqual(comp->stype, (const xmlChar *) "text"))
            comp->number = 0;
        else if (xmlStrEqual(comp->stype, (const xmlChar *) "number"))
            comp->number = 1;
        else {
            xsltTransformError(NULL, style, inst,
                               "xsltSortComp: no support for data-type = %s\n",
                               comp->stype);
            comp->number = 0; /* use default */
            if (style != NULL)
                style->warnings++;
        }
    }
    comp->order = xsltEvalStaticAttrValueTemplate(
        style, inst, (const xmlChar *) "order", NULL, &comp->has_order);
    if (comp->order != NULL) {
        if (xmlStrEqual(comp->order, (const xmlChar *) "ascending"))
            comp->descending = 0;
        else if (xmlStrEqual(comp->order, (const xmlChar *) "descending"))
            comp->descending = 1;
        else {
            xsltTransformError(NULL, style, inst,
                               "xsltSortComp: invalid value %s for order\n",
                               comp->order);
            comp->descending = 0; /* use default */
            if (style != NULL)
                style->warnings++;
        }
    }
    comp->case_order = xsltEvalStaticAttrValueTemplate(
        style, inst, (const xmlChar *) "case-order", NULL, &comp->has_use);
    if (comp->case_order != NULL) {
        if (xmlStrEqual(comp->case_order, (const xmlChar *) "upper-first"))
            comp->lower_first = 0;
        else if (xmlStrEqual(comp->case_order, (const xmlChar *) "lower-first"))
            comp->lower_first = 1;
        else {
            xsltTransformError(NULL, style, inst,
                               "xsltSortComp: invalid value %s for order\n",
                               comp->order);
            comp->lower_first = 0; /* use default */
            if (style != NULL)
                style->warnings++;
        }
    }

    comp->lang = xsltEvalStaticAttrValueTemplate(
        style, inst, (const xmlChar *) "lang", NULL, &comp->has_lang);

    comp->select = xsltGetCNsProp(style, inst, (const xmlChar *) "select",
                                  XSLT_NAMESPACE);
    if (comp->select == NULL) {
        /*
	 * The default value of the select attribute is ., which will
	 * cause the string-value of the current node to be used as
	 * the sort key.
	 */
        comp->select = xmlDictLookup(style->dict, BAD_CAST ".", 1);
    }
    comp->comp = xsltXPathCompile(style, comp->select, inst);
    if (comp->comp == NULL) {
        if (style != NULL)
            style->errors++;
    }
    if (xmlNodeGetChildren(inst) != NULL) {
        xsltTransformError(NULL, style, inst, "xsl:sort : is not empty\n");
        if (style != NULL)
            style->errors++;
    }
}

/**
 * xsltCopyComp:
 * @style:  the XSLT stylesheet
 * @inst:  the xslt copy node
 *
 * Process the xslt copy node on the source node
 */
static void
xsltCopyComp (xsltStylesheetPtr style, xmlNodePtr inst)
{
    xsltStylePreCompPtr comp;

    if (style == NULL || inst == NULL
        || xmlNodeGetType(inst) != XML_ELEMENT_NODE)
        return;
    comp = xsltNewStylePreComp(style, XSLT_FUNC_COPY);

    if (comp == NULL)
        return;
    xmlNodeSetPsvi(inst, comp);
    comp->inst = inst;

    comp->use = xsltGetCNsProp(
        style, inst, (const xmlChar *) "use-attribute-sets", XSLT_NAMESPACE);
    if (comp->use == NULL)
        comp->has_use = 0;
    else
        comp->has_use = 1;
}

/**
 * xsltTextComp:
 * @style: an XSLT compiled stylesheet
 * @inst:  the xslt text node
 *
 * TODO: This function is obsolete, since xsl:text won't
 *  be compiled, but removed from the tree.
 *
 * Process the xslt text node on the source node
 */
static void
xsltTextComp (xsltStylesheetPtr style, xmlNodePtr inst)
{
    xsltStylePreCompPtr comp;
    const xmlChar *prop;

    if (style == NULL || inst == NULL
        || xmlNodeGetType(inst) != XML_ELEMENT_NODE)
        return;

    comp = xsltNewStylePreComp(style, XSLT_FUNC_TEXT);
    if (comp == NULL)
        return;
    xmlNodeSetPsvi(inst, comp);
    comp->inst = inst;
    comp->noescape = 0;

    prop = xsltGetCNsProp(style, inst,
                          (const xmlChar *) "disable-output-escaping",
                          XSLT_NAMESPACE);
    if (prop != NULL) {
        if (xmlStrEqual(prop, (const xmlChar *) "yes")) {
            comp->noescape = 1;
        } else if (!xmlStrEqual(prop, (const xmlChar *) "no")) {
            xsltTransformError(
                NULL, style, inst,
                "xsl:text: disable-output-escaping allows only yes or no\n");
            if (style != NULL)
                style->warnings++;
        }
    }
}

/**
 * xsltElementComp:
 * @style: an XSLT compiled stylesheet
 * @inst:  the xslt element node
 *
 * Process the xslt element node on the source node
 */
static void
xsltElementComp (xsltStylesheetPtr style, xmlNodePtr inst)
{
    xsltStylePreCompPtr comp;

    /*
    * <xsl:element
    *   name = { qname }
    *   namespace = { uri-reference }
    *   use-attribute-sets = qnames>
    *   <!-- Content: template -->
    * </xsl:element>
    */
    if (style == NULL || inst == NULL
        || xmlNodeGetType(inst) != XML_ELEMENT_NODE)
        return;

    comp = xsltNewStylePreComp(style, XSLT_FUNC_ELEMENT);

    if (comp == NULL)
        return;
    xmlNodeSetPsvi(inst, comp);
    comp->inst = inst;

    /*
    * Attribute "name".
    */
    /*
    * TODO: Precompile the AVT. See bug #344894.
    */
    comp->name = xsltEvalStaticAttrValueTemplate(
        style, inst, (const xmlChar *) "name", NULL, &comp->has_name);
    if (!comp->has_name) {
        xsltTransformError(NULL, style, inst,
                           "xsl:element: The attribute 'name' is missing.\n");
        style->errors++;
        goto error;
    }
    /*
    * Attribute "namespace".
    */
    /*
    * TODO: Precompile the AVT. See bug #344894.
    */
    comp->ns = xsltEvalStaticAttrValueTemplate(
        style, inst, (const xmlChar *) "namespace", NULL, &comp->has_ns);

    if (comp->name != NULL) {
        if (xmlValidateQName(comp->name, 0)) {
            xsltTransformError(
                NULL, style, inst,
                "xsl:element: The value '%s' of the attribute 'name' is "
                "not a valid QName.\n",
                comp->name);
            style->errors++;
        } else {
            const xmlChar *prefix = NULL, *name;

            name = xsltSplitQName(style->dict, comp->name, &prefix);
            if (comp->has_ns == 0) {
                xmlNsPtr ns;

                /*
		* SPEC XSLT 1.0:
		*  "If the namespace attribute is not present, then the QName is
		*  expanded into an expanded-name using the namespace declarations
		*  in effect for the xsl:element element, including any default
		*  namespace declaration.
		*/
                ns = xmlSearchNs(xmlNodeGetDoc(inst), inst, prefix);
                if (ns != NULL) {
                    comp->ns = xmlDictLookup(style->dict, xmlNsGetHref(ns), -1);
                    comp->has_ns = 1;
                    (void) name; /* Suppress unused variable warning. */
                } else if (prefix != NULL) {
                    xsltTransformError(
                        NULL, style, inst,
                        "xsl:element: The prefixed QName '%s' "
                        "has no namespace binding in scope in the "
                        "stylesheet; this is an error, since the namespace was "
                        "not specified by the instruction itself.\n",
                        comp->name);
                    style->errors++;
                }
            }
            if (prefix != NULL
                && !xmlStrncasecmp(prefix, (xmlChar *) "xml", 3)) {
                /*
		* Mark is to be skipped.
		*/
                comp->has_name = 0;
            }
        }
    }
    /*
    * Attribute "use-attribute-sets",
    */
    comp->use = xsltEvalStaticAttrValueTemplate(
        style, inst, (const xmlChar *) "use-attribute-sets", NULL,
        &comp->has_use);

error:
    return;
}

/**
 * xsltAttributeComp:
 * @style: an XSLT compiled stylesheet
 * @inst:  the xslt attribute node
 *
 * Process the xslt attribute node on the source node
 */
static void
xsltAttributeComp (xsltStylesheetPtr style, xmlNodePtr inst)
{
    xsltStylePreCompPtr comp;

    /*
    * <xsl:attribute
    *   name = { qname }
    *   namespace = { uri-reference }>
    *   <!-- Content: template -->
    * </xsl:attribute>
    */
    if (style == NULL || inst == NULL
        || xmlNodeGetType(inst) != XML_ELEMENT_NODE)
        return;

    comp = xsltNewStylePreComp(style, XSLT_FUNC_ATTRIBUTE);

    if (comp == NULL)
        return;
    xmlNodeSetPsvi(inst, comp);
    comp->inst = inst;

    /*
    * Attribute "name".
    */
    /*
    * TODO: Precompile the AVT. See bug #344894.
    */
    comp->name = xsltEvalStaticAttrValueTemplate(
        style, inst, (const xmlChar *) "name", NULL, &comp->has_name);
    if (!comp->has_name) {
        xsltTransformError(
            NULL, style, inst,
            "XSLT-attribute: The attribute 'name' is missing.\n");
        style->errors++;
        return;
    }
    /*
    * Attribute "namespace".
    */
    /*
    * TODO: Precompile the AVT. See bug #344894.
    */
    comp->ns = xsltEvalStaticAttrValueTemplate(
        style, inst, (const xmlChar *) "namespace", NULL, &comp->has_ns);

    if (comp->name != NULL) {
        if (xmlValidateQName(comp->name, 0)) {
            xsltTransformError(
                NULL, style, inst,
                "xsl:attribute: The value '%s' of the attribute 'name' is "
                "not a valid QName.\n",
                comp->name);
            style->errors++;
        } else if (xmlStrEqual(comp->name, BAD_CAST "xmlns")) {
            xsltTransformError(
                NULL, style, inst,
                "xsl:attribute: The attribute name 'xmlns' is not allowed.\n");
            style->errors++;
        } else {
            const xmlChar *prefix = NULL, *name;

            name = xsltSplitQName(style->dict, comp->name, &prefix);
            if (prefix != NULL) {
                if (comp->has_ns == 0) {
                    xmlNsPtr ns;

                    /*
		    * SPEC XSLT 1.0:
		    *  "If the namespace attribute is not present, then the
		    *  QName is expanded into an expanded-name using the
		    *  namespace declarations in effect for the xsl:element
		    *  element, including any default namespace declaration.
		    */
                    ns = xmlSearchNs(xmlNodeGetDoc(inst), inst, prefix);
                    if (ns != NULL) {
                        comp->ns
                            = xmlDictLookup(style->dict, xmlNsGetHref(ns), -1);
                        comp->has_ns = 1;
                        (void) name; /* Suppress unused variable warning. */
                    } else {
                        xsltTransformError(
                            NULL, style, inst,
                            "xsl:attribute: The prefixed QName '%s' "
                            "has no namespace binding in scope in the "
                            "stylesheet; this is an error, since the "
                            "namespace was not specified by the instruction "
                            "itself.\n",
                            comp->name);
                        style->errors++;
                    }
                }
            }
        }
    }
}

/**
 * xsltCommentComp:
 * @style: an XSLT compiled stylesheet
 * @inst:  the xslt comment node
 *
 * Process the xslt comment node on the source node
 */
static void
xsltCommentComp (xsltStylesheetPtr style, xmlNodePtr inst)
{
    xsltStylePreCompPtr comp;

    if (style == NULL || inst == NULL
        || xmlNodeGetType(inst) != XML_ELEMENT_NODE)
        return;

    comp = xsltNewStylePreComp(style, XSLT_FUNC_COMMENT);

    if (comp == NULL)
        return;
    xmlNodeSetPsvi(inst, comp);
    comp->inst = inst;
}

/**
 * xsltProcessingInstructionComp:
 * @style: an XSLT compiled stylesheet
 * @inst:  the xslt processing-instruction node
 *
 * Process the xslt processing-instruction node on the source node
 */
static void
xsltProcessingInstructionComp (xsltStylesheetPtr style, xmlNodePtr inst)
{
    xsltStylePreCompPtr comp;

    if (style == NULL || inst == NULL
        || xmlNodeGetType(inst) != XML_ELEMENT_NODE)
        return;

    comp = xsltNewStylePreComp(style, XSLT_FUNC_PI);

    if (comp == NULL)
        return;
    xmlNodeSetPsvi(inst, comp);
    comp->inst = inst;

    comp->name = xsltEvalStaticAttrValueTemplate(
        style, inst, (const xmlChar *) "name", XSLT_NAMESPACE, &comp->has_name);
}

/**
 * xsltCopyOfComp:
 * @style: an XSLT compiled stylesheet
 * @inst:  the xslt copy-of node
 *
 * Process the xslt copy-of node on the source node
 */
static void
xsltCopyOfComp (xsltStylesheetPtr style, xmlNodePtr inst)
{
    xsltStylePreCompPtr comp;

    if (style == NULL || inst == NULL
        || xmlNodeGetType(inst) != XML_ELEMENT_NODE)
        return;

    comp = xsltNewStylePreComp(style, XSLT_FUNC_COPYOF);

    if (comp == NULL)
        return;
    xmlNodeSetPsvi(inst, comp);
    comp->inst = inst;

    comp->select = xsltGetCNsProp(style, inst, (const xmlChar *) "select",
                                  XSLT_NAMESPACE);
    if (comp->select == NULL) {
        xsltTransformError(NULL, style, inst,
                           "xsl:copy-of : select is missing\n");
        if (style != NULL)
            style->errors++;
        return;
    }
    comp->comp = xsltXPathCompile(style, comp->select, inst);
    if (comp->comp == NULL) {
        if (style != NULL)
            style->errors++;
    }
}

/**
 * xsltValueOfComp:
 * @style: an XSLT compiled stylesheet
 * @inst:  the xslt value-of node
 *
 * Process the xslt value-of node on the source node
 */
static void
xsltValueOfComp (xsltStylesheetPtr style, xmlNodePtr inst)
{
    xsltStylePreCompPtr comp;
    const xmlChar *prop;

    if (style == NULL || inst == NULL
        || xmlNodeGetType(inst) != XML_ELEMENT_NODE)
        return;

    comp = xsltNewStylePreComp(style, XSLT_FUNC_VALUEOF);

    if (comp == NULL)
        return;
    xmlNodeSetPsvi(inst, comp);
    comp->inst = inst;

    prop = xsltGetCNsProp(style, inst,
                          (const xmlChar *) "disable-output-escaping",
                          XSLT_NAMESPACE);
    if (prop != NULL) {
        if (xmlStrEqual(prop, (const xmlChar *) "yes")) {
            comp->noescape = 1;
        } else if (!xmlStrEqual(prop, (const xmlChar *) "no")) {
            xsltTransformError(NULL, style, inst,
                               "xsl:value-of : disable-output-escaping allows "
                               "only yes or no\n");
            if (style != NULL)
                style->warnings++;
        }
    }
    comp->select = xsltGetCNsProp(style, inst, (const xmlChar *) "select",
                                  XSLT_NAMESPACE);
    if (comp->select == NULL) {
        xsltTransformError(NULL, style, inst,
                           "xsl:value-of : select is missing\n");
        if (style != NULL)
            style->errors++;
        return;
    }
    comp->comp = xsltXPathCompile(style, comp->select, inst);
    if (comp->comp == NULL) {
        if (style != NULL)
            style->errors++;
    }
}

static void
xsltGetQNameProperty (xsltStylesheetPtr style, xmlNodePtr inst,
                      const xmlChar *propName, int mandatory, int *hasProp,
                      const xmlChar **nsName, const xmlChar **localName)
{
    const xmlChar *prop;

    if (nsName)
        *nsName = NULL;
    if (localName)
        *localName = NULL;
    if (hasProp)
        *hasProp = 0;

    prop = xsltGetCNsProp(style, inst, propName, XSLT_NAMESPACE);
    if (prop == NULL) {
        if (mandatory) {
            xsltTransformError(NULL, style, inst,
                               "The attribute '%s' is missing.\n", propName);
            style->errors++;
            return;
        }
    } else {
        const xmlChar *URI;

        if (xmlValidateQName(prop, 0)) {
            xsltTransformError(NULL, style, inst,
                               "The value '%s' of the attribute "
                               "'%s' is not a valid QName.\n",
                               prop, propName);
            style->errors++;
            return;
        } else {
            /*
	    * @prop will be in the string dict afterwards, @URI not.
	    */
            URI = xsltGetQNameURI2(style, inst, &prop);
            if (prop == NULL) {
                style->errors++;
            } else {
                if (localName)
                    *localName = prop;
                if (hasProp)
                    *hasProp = 1;
                if (URI != NULL) {
                    /*
		    * Fixes bug #308441: Put the ns-name in the dict
		    * in order to pointer compare names during XPath's
		    * variable lookup.
		    */
                    if (nsName)
                        *nsName = xmlDictLookup(style->dict, URI, -1);
                    /* comp->has_ns = 1; */
                }
            }
        }
    }
    return;
}

/**
 * xsltWithParamComp:
 * @style: an XSLT compiled stylesheet
 * @inst:  the xslt with-param node
 *
 * Process the xslt with-param node on the source node
 * Allowed parents: xsl:call-template, xsl:apply-templates.
 * <xsl:with-param
 *  name = qname
 *  select = expression>
 *  <!-- Content: template -->
 * </xsl:with-param>
 */
static void
xsltWithParamComp (xsltStylesheetPtr style, xmlNodePtr inst)
{
    xsltStylePreCompPtr comp;

    if (style == NULL || inst == NULL
        || xmlNodeGetType(inst) != XML_ELEMENT_NODE)
        return;

    comp = xsltNewStylePreComp(style, XSLT_FUNC_WITHPARAM);

    if (comp == NULL)
        return;
    xmlNodeSetPsvi(inst, comp);
    comp->inst = inst;

    /*
    * Attribute "name".
    */
    xsltGetQNameProperty(style, inst, BAD_CAST "name", 1, &(comp->has_name),
                         &(comp->ns), &(comp->name));
    if (comp->ns)
        comp->has_ns = 1;
    /*
    * Attribute "select".
    */
    comp->select = xsltGetCNsProp(style, inst, (const xmlChar *) "select",
                                  XSLT_NAMESPACE);
    if (comp->select != NULL) {
        comp->comp = xsltXPathCompile(style, comp->select, inst);
        if (comp->comp == NULL) {
            style->errors++;
        }
        if (xmlNodeGetChildren(inst) != NULL) {
            xsltTransformError(
                NULL, style, inst,
                "XSLT-with-param: The content should be empty since "
                "the attribute select is present.\n");
            style->warnings++;
        }
    }
}

/**
 * xsltNumberComp:
 * @style: an XSLT compiled stylesheet
 * @cur:   the xslt number node
 *
 * Process the xslt number node on the source node
 */
static void
xsltNumberComp (xsltStylesheetPtr style, xmlNodePtr cur)
{
    xsltStylePreCompPtr comp;
    const xmlChar *prop;

    if (style == NULL || cur == NULL
        || xmlNodeGetType(cur) != XML_ELEMENT_NODE)
        return;

    comp = xsltNewStylePreComp(style, XSLT_FUNC_NUMBER);

    if (comp == NULL)
        return;
    xmlNodeSetPsvi(cur, comp);

    comp->numdata.doc = xmlNodeGetDoc(cur);
    comp->numdata.node = cur;
    comp->numdata.value
        = xsltGetCNsProp(style, cur, (const xmlChar *) "value", XSLT_NAMESPACE);

    prop = xsltEvalStaticAttrValueTemplate(
        style, cur, (const xmlChar *) "format", XSLT_NAMESPACE,
        &comp->numdata.has_format);
    if (comp->numdata.has_format == 0) {
        comp->numdata.format = xmlDictLookup(style->dict, BAD_CAST "", 0);
    } else {
        comp->numdata.format = prop;
    }

    comp->numdata.count
        = xsltGetCNsProp(style, cur, (const xmlChar *) "count", XSLT_NAMESPACE);
    comp->numdata.from
        = xsltGetCNsProp(style, cur, (const xmlChar *) "from", XSLT_NAMESPACE);

    prop
        = xsltGetCNsProp(style, cur, (const xmlChar *) "count", XSLT_NAMESPACE);
    if (prop != NULL) {
        comp->numdata.countPat
            = xsltCompilePattern(prop, xmlNodeGetDoc(cur), cur, style, NULL);
    }

    prop = xsltGetCNsProp(style, cur, (const xmlChar *) "from", XSLT_NAMESPACE);
    if (prop != NULL) {
        comp->numdata.fromPat
            = xsltCompilePattern(prop, xmlNodeGetDoc(cur), cur, style, NULL);
    }

    prop
        = xsltGetCNsProp(style, cur, (const xmlChar *) "level", XSLT_NAMESPACE);
    if (prop != NULL) {
        if (xmlStrEqual(prop, BAD_CAST("single"))
            || xmlStrEqual(prop, BAD_CAST("multiple"))
            || xmlStrEqual(prop, BAD_CAST("any"))) {
            comp->numdata.level = prop;
        } else {
            xsltTransformError(NULL, style, cur,
                               "xsl:number : invalid value %s for level\n",
                               prop);
            if (style != NULL)
                style->warnings++;
        }
    }

    prop = xsltGetCNsProp(style, cur, (const xmlChar *) "lang", XSLT_NAMESPACE);
    if (prop != NULL) {
        xsltTransformError(NULL, style, cur,
                           "xsl:number : lang attribute not implemented\n");
        XSLT_TODO; /* xsl:number lang attribute */
    }

    prop = xsltGetCNsProp(style, cur, (const xmlChar *) "letter-value",
                          XSLT_NAMESPACE);
    if (prop != NULL) {
        if (xmlStrEqual(prop, BAD_CAST("alphabetic"))) {
            xsltTransformError(
                NULL, style, cur,
                "xsl:number : letter-value 'alphabetic' not implemented\n");
            if (style != NULL)
                style->warnings++;
            XSLT_TODO; /* xsl:number letter-value attribute alphabetic */
        } else if (xmlStrEqual(prop, BAD_CAST("traditional"))) {
            xsltTransformError(
                NULL, style, cur,
                "xsl:number : letter-value 'traditional' not implemented\n");
            if (style != NULL)
                style->warnings++;
            XSLT_TODO; /* xsl:number letter-value attribute traditional */
        } else {
            xsltTransformError(
                NULL, style, cur,
                "xsl:number : invalid value %s for letter-value\n", prop);
            if (style != NULL)
                style->warnings++;
        }
    }

    prop = xsltGetCNsProp(style, cur, (const xmlChar *) "grouping-separator",
                          XSLT_NAMESPACE);
    if (prop != NULL) {
        comp->numdata.groupingCharacterLen = xmlStrlen(prop);
        comp->numdata.groupingCharacter
            = xsltGetUTF8Char(prop, &(comp->numdata.groupingCharacterLen));
        if (comp->numdata.groupingCharacter < 0)
            comp->numdata.groupingCharacter = 0;
    }

    prop = xsltGetCNsProp(style, cur, (const xmlChar *) "grouping-size",
                          XSLT_NAMESPACE);
    if (prop != NULL) {
        sscanf((char *) prop, "%d", &comp->numdata.digitsPerGroup);
    } else {
        comp->numdata.groupingCharacter = 0;
    }

    /* Set default values */
    if (comp->numdata.value == NULL) {
        if (comp->numdata.level == NULL) {
            comp->numdata.level
                = xmlDictLookup(style->dict, BAD_CAST "single", 6);
        }
    }
}

/**
 * xsltApplyImportsComp:
 * @style: an XSLT compiled stylesheet
 * @inst:  the xslt apply-imports node
 *
 * Process the xslt apply-imports node on the source node
 */
static void
xsltApplyImportsComp (xsltStylesheetPtr style, xmlNodePtr inst)
{
    xsltStylePreCompPtr comp;

    if (style == NULL || inst == NULL
        || xmlNodeGetType(inst) != XML_ELEMENT_NODE)
        return;

    comp = xsltNewStylePreComp(style, XSLT_FUNC_APPLYIMPORTS);

    if (comp == NULL)
        return;
    xmlNodeSetPsvi(inst, comp);
    comp->inst = inst;
}

/**
 * xsltCallTemplateComp:
 * @style: an XSLT compiled stylesheet
 * @inst:  the xslt call-template node
 *
 * Process the xslt call-template node on the source node
 */
static void
xsltCallTemplateComp (xsltStylesheetPtr style, xmlNodePtr inst)
{
    xsltStylePreCompPtr comp;

    if (style == NULL || inst == NULL
        || xmlNodeGetType(inst) != XML_ELEMENT_NODE)
        return;

    comp = xsltNewStylePreComp(style, XSLT_FUNC_CALLTEMPLATE);

    if (comp == NULL)
        return;
    xmlNodeSetPsvi(inst, comp);
    comp->inst = inst;

    /*
     * Attribute "name".
     */
    xsltGetQNameProperty(style, inst, BAD_CAST "name", 1, &(comp->has_name),
                         &(comp->ns), &(comp->name));
    if (comp->ns)
        comp->has_ns = 1;
}

/**
 * xsltApplyTemplatesComp:
 * @style: an XSLT compiled stylesheet
 * @inst:  the apply-templates node
 *
 * Process the apply-templates node on the source node
 */
static void
xsltApplyTemplatesComp (xsltStylesheetPtr style, xmlNodePtr inst)
{
    xsltStylePreCompPtr comp;

    if (style == NULL || inst == NULL
        || xmlNodeGetType(inst) != XML_ELEMENT_NODE)
        return;

    comp = xsltNewStylePreComp(style, XSLT_FUNC_APPLYTEMPLATES);

    if (comp == NULL)
        return;
    xmlNodeSetPsvi(inst, comp);
    comp->inst = inst;

    /*
     * Attribute "mode".
     */
    xsltGetQNameProperty(style, inst, BAD_CAST "mode", 0, NULL,
                         &(comp->modeURI), &(comp->mode));
    /*
    * Attribute "select".
    */
    comp->select
        = xsltGetCNsProp(style, inst, BAD_CAST "select", XSLT_NAMESPACE);
    if (comp->select != NULL) {
        comp->comp = xsltXPathCompile(style, comp->select, inst);
        if (comp->comp == NULL) {
            style->errors++;
        }
    }
    /* TODO: handle (or skip) the xsl:sort and xsl:with-param */
}

/**
 * xsltChooseComp:
 * @style: an XSLT compiled stylesheet
 * @inst:  the xslt choose node
 *
 * Process the xslt choose node on the source node
 */
static void
xsltChooseComp (xsltStylesheetPtr style, xmlNodePtr inst)
{
    xsltStylePreCompPtr comp;

    if (style == NULL || inst == NULL
        || xmlNodeGetType(inst) != XML_ELEMENT_NODE)
        return;

    comp = xsltNewStylePreComp(style, XSLT_FUNC_CHOOSE);

    if (comp == NULL)
        return;
    xmlNodeSetPsvi(inst, comp);
    comp->inst = inst;
}

/**
 * xsltIfComp:
 * @style: an XSLT compiled stylesheet
 * @inst:  the xslt if node
 *
 * Process the xslt if node on the source node
 */
static void
xsltIfComp (xsltStylesheetPtr style, xmlNodePtr inst)
{
    xsltStylePreCompPtr comp;

    if (style == NULL || inst == NULL
        || xmlNodeGetType(inst) != XML_ELEMENT_NODE)
        return;

    comp = xsltNewStylePreComp(style, XSLT_FUNC_IF);

    if (comp == NULL)
        return;
    xmlNodeSetPsvi(inst, comp);
    comp->inst = inst;

    comp->test
        = xsltGetCNsProp(style, inst, (const xmlChar *) "test", XSLT_NAMESPACE);
    if (comp->test == NULL) {
        xsltTransformError(NULL, style, inst, "xsl:if : test is not defined\n");
        if (style != NULL)
            style->errors++;
        return;
    }
    comp->comp = xsltXPathCompile(style, comp->test, inst);
    if (comp->comp == NULL) {
        if (style != NULL)
            style->errors++;
    }
}

/**
 * xsltWhenComp:
 * @style: an XSLT compiled stylesheet
 * @inst:  the xslt if node
 *
 * Process the xslt if node on the source node
 */
static void
xsltWhenComp (xsltStylesheetPtr style, xmlNodePtr inst)
{
    xsltStylePreCompPtr comp;

    if (style == NULL || inst == NULL
        || xmlNodeGetType(inst) != XML_ELEMENT_NODE)
        return;

    comp = xsltNewStylePreComp(style, XSLT_FUNC_WHEN);

    if (comp == NULL)
        return;
    xmlNodeSetPsvi(inst, comp);
    comp->inst = inst;

    comp->test
        = xsltGetCNsProp(style, inst, (const xmlChar *) "test", XSLT_NAMESPACE);
    if (comp->test == NULL) {
        xsltTransformError(NULL, style, inst,
                           "xsl:when : test is not defined\n");
        if (style != NULL)
            style->errors++;
        return;
    }
    comp->comp = xsltXPathCompile(style, comp->test, inst);
    if (comp->comp == NULL) {
        if (style != NULL)
            style->errors++;
    }
}

/**
 * xsltForEachComp:
 * @style: an XSLT compiled stylesheet
 * @inst:  the xslt for-each node
 *
 * Process the xslt for-each node on the source node
 */
static void
xsltForEachComp (xsltStylesheetPtr style, xmlNodePtr inst)
{
    xsltStylePreCompPtr comp;

    if (style == NULL || inst == NULL
        || xmlNodeGetType(inst) != XML_ELEMENT_NODE)
        return;

    comp = xsltNewStylePreComp(style, XSLT_FUNC_FOREACH);

    if (comp == NULL)
        return;
    xmlNodeSetPsvi(inst, comp);
    comp->inst = inst;

    comp->select = xsltGetCNsProp(style, inst, (const xmlChar *) "select",
                                  XSLT_NAMESPACE);
    if (comp->select == NULL) {
        xsltTransformError(NULL, style, inst,
                           "xsl:for-each : select is missing\n");
        if (style != NULL)
            style->errors++;
    } else {
        comp->comp = xsltXPathCompile(style, comp->select, inst);
        if (comp->comp == NULL) {
            if (style != NULL)
                style->errors++;
        }
    }
    /* TODO: handle and skip the xsl:sort */
}

/**
 * xsltVariableComp:
 * @style: an XSLT compiled stylesheet
 * @inst:  the xslt variable node
 *
 * Process the xslt variable node on the source node
 */
static void
xsltVariableComp (xsltStylesheetPtr style, xmlNodePtr inst)
{
    xsltStylePreCompPtr comp;

    if (style == NULL || inst == NULL
        || xmlNodeGetType(inst) != XML_ELEMENT_NODE)
        return;

    comp = xsltNewStylePreComp(style, XSLT_FUNC_VARIABLE);

    if (comp == NULL)
        return;

    xmlNodeSetPsvi(inst, comp);
    comp->inst = inst;
    /*
     * The full template resolution can be done statically
     */

    /*
    * Attribute "name".
    */
    xsltGetQNameProperty(style, inst, BAD_CAST "name", 1, &(comp->has_name),
                         &(comp->ns), &(comp->name));
    if (comp->ns)
        comp->has_ns = 1;
    /*
    * Attribute "select".
    */
    comp->select = xsltGetCNsProp(style, inst, (const xmlChar *) "select",
                                  XSLT_NAMESPACE);
    if (comp->select != NULL) {
        xmlNodePtr cur;
        comp->comp = xsltXPathCompile(style, comp->select, inst);
        if (comp->comp == NULL) {
            style->errors++;
        }
        for (cur = xmlNodeGetChildren(inst); cur != NULL;
             cur = xmlNodeGetNext(cur)) {
            if (xmlNodeGetType(cur) != XML_COMMENT_NODE
                && (xmlNodeGetType(cur) != XML_TEXT_NODE
                    || !xsltIsBlank(xmlNodeGetContentRaw(cur)))) {
                xsltTransformError(
                    NULL, style, inst,
                    "XSLT-variable: There must be no child nodes, since the "
                    "attribute 'select' was specified.\n");
                style->errors++;
            }
        }
    }
}

/**
 * xsltParamComp:
 * @style: an XSLT compiled stylesheet
 * @inst:  the xslt param node
 *
 * Process the xslt param node on the source node
 */
static void
xsltParamComp (xsltStylesheetPtr style, xmlNodePtr inst)
{
    xsltStylePreCompPtr comp;

    if (style == NULL || inst == NULL
        || xmlNodeGetType(inst) != XML_ELEMENT_NODE)
        return;

    comp = xsltNewStylePreComp(style, XSLT_FUNC_PARAM);

    if (comp == NULL)
        return;
    xmlNodeSetPsvi(inst, comp);
    comp->inst = inst;

    /*
     * Attribute "name".
     */
    xsltGetQNameProperty(style, inst, BAD_CAST "name", 1, &(comp->has_name),
                         &(comp->ns), &(comp->name));
    if (comp->ns)
        comp->has_ns = 1;
    /*
    * Attribute "select".
    */
    comp->select = xsltGetCNsProp(style, inst, (const xmlChar *) "select",
                                  XSLT_NAMESPACE);
    if (comp->select != NULL) {
        comp->comp = xsltXPathCompile(style, comp->select, inst);
        if (comp->comp == NULL) {
            style->errors++;
        }
        if (xmlNodeGetChildren(inst) != NULL) {
            xsltTransformError(
                NULL, style, inst,
                "XSLT-param: The content should be empty since the "
                "attribute 'select' is present.\n");
            style->warnings++;
        }
    }
}

/************************************************************************
 *									*
 *		    Generic interface					*
 *									*
 ************************************************************************/

/**
 * xsltFreeStylePreComps:
 * @style:  an XSLT transformation context
 *
 * Free up the memory allocated by all precomputed blocks
 */
void
xsltFreeStylePreComps (xsltStylesheetPtr style)
{
    xsltElemPreCompPtr cur, next;

    if (style == NULL)
        return;

    cur = style->preComps;
    while (cur != NULL) {
        next = cur->next;
        if (cur->type == XSLT_FUNC_EXTENSION)
            cur->free(cur);
        else
            xsltFreeStylePreComp((xsltStylePreCompPtr) cur);
        cur = next;
    }
}


/**
 * xsltStylePreCompute:
 * @style:  the XSLT stylesheet
 * @inst:  the instruction in the stylesheet
 *
 * Precompute an XSLT stylesheet element
 */
void
xsltStylePreCompute (xsltStylesheetPtr style, xmlNodePtr inst)
{
    /*
    * URGENT TODO: Normally inst->psvi Should never be reserved here,
    *   BUT: since if we include the same stylesheet from
    *   multiple imports, then the stylesheet will be parsed
    *   again. We simply must not try to compute the stylesheet again.
    * TODO: Get to the point where we don't need to query the
    *   namespace- and local-name of the node, but can evaluate this
    *   using cctxt->style->inode->category;
    */
    if (inst == NULL || xmlNodeGetType(inst) != XML_ELEMENT_NODE
        || xmlNodeGetPsvi(inst) != NULL)
        return;

    if (IS_XSLT_ELEM(inst)) {
        xsltStylePreCompPtr cur;

        if (IS_XSLT_NAME(inst, "apply-templates")) {
            xsltCheckInstructionElement(style, inst);
            xsltApplyTemplatesComp(style, inst);
        } else if (IS_XSLT_NAME(inst, "with-param")) {
            xsltCheckParentElement(style, inst, BAD_CAST "apply-templates",
                                   BAD_CAST "call-template");
            xsltWithParamComp(style, inst);
        } else if (IS_XSLT_NAME(inst, "value-of")) {
            xsltCheckInstructionElement(style, inst);
            xsltValueOfComp(style, inst);
        } else if (IS_XSLT_NAME(inst, "copy")) {
            xsltCheckInstructionElement(style, inst);
            xsltCopyComp(style, inst);
        } else if (IS_XSLT_NAME(inst, "copy-of")) {
            xsltCheckInstructionElement(style, inst);
            xsltCopyOfComp(style, inst);
        } else if (IS_XSLT_NAME(inst, "if")) {
            xsltCheckInstructionElement(style, inst);
            xsltIfComp(style, inst);
        } else if (IS_XSLT_NAME(inst, "when")) {
            xsltCheckParentElement(style, inst, BAD_CAST "choose", NULL);
            xsltWhenComp(style, inst);
        } else if (IS_XSLT_NAME(inst, "choose")) {
            xsltCheckInstructionElement(style, inst);
            xsltChooseComp(style, inst);
        } else if (IS_XSLT_NAME(inst, "for-each")) {
            xsltCheckInstructionElement(style, inst);
            xsltForEachComp(style, inst);
        } else if (IS_XSLT_NAME(inst, "apply-imports")) {
            xsltCheckInstructionElement(style, inst);
            xsltApplyImportsComp(style, inst);
        } else if (IS_XSLT_NAME(inst, "attribute")) {
            xmlNodePtr parent = xmlNodeGetParent(inst);

            if (parent == NULL || xmlNodeGetType(parent) != XML_ELEMENT_NODE
                || xmlNodeGetNs(parent) == NULL
                || xmlNodeGetNs(parent) != xmlNodeGetNs(inst)
                    && !xmlStrEqual(xmlNsGetHref(xmlNodeGetNs(parent)),
                                     xmlNsGetHref(xmlNodeGetNs(inst)))
                || !xmlStrEqual(xmlNodeGetName(parent),
                                 BAD_CAST "attribute-set")) {
                xsltCheckInstructionElement(style, inst);
            }
            xsltAttributeComp(style, inst);
        } else if (IS_XSLT_NAME(inst, "element")) {
            xsltCheckInstructionElement(style, inst);
            xsltElementComp(style, inst);
        } else if (IS_XSLT_NAME(inst, "text")) {
            xsltCheckInstructionElement(style, inst);
            xsltTextComp(style, inst);
        } else if (IS_XSLT_NAME(inst, "sort")) {
            xsltCheckParentElement(style, inst, BAD_CAST "apply-templates",
                                   BAD_CAST "for-each");
            xsltSortComp(style, inst);
        } else if (IS_XSLT_NAME(inst, "comment")) {
            xsltCheckInstructionElement(style, inst);
            xsltCommentComp(style, inst);
        } else if (IS_XSLT_NAME(inst, "number")) {
            xsltCheckInstructionElement(style, inst);
            xsltNumberComp(style, inst);
        } else if (IS_XSLT_NAME(inst, "processing-instruction")) {
            xsltCheckInstructionElement(style, inst);
            xsltProcessingInstructionComp(style, inst);
        } else if (IS_XSLT_NAME(inst, "call-template")) {
            xsltCheckInstructionElement(style, inst);
            xsltCallTemplateComp(style, inst);
        } else if (IS_XSLT_NAME(inst, "param")) {
            if (xsltCheckTopLevelElement(style, inst, 0) == 0)
                xsltCheckInstructionElement(style, inst);
            xsltParamComp(style, inst);
        } else if (IS_XSLT_NAME(inst, "variable")) {
            if (xsltCheckTopLevelElement(style, inst, 0) == 0)
                xsltCheckInstructionElement(style, inst);
            xsltVariableComp(style, inst);
        } else if (IS_XSLT_NAME(inst, "otherwise")) {
            xsltCheckParentElement(style, inst, BAD_CAST "choose", NULL);
            xsltCheckInstructionElement(style, inst);
            return;
        } else if (IS_XSLT_NAME(inst, "template")) {
            xsltCheckTopLevelElement(style, inst, 1);
            return;
        } else if (IS_XSLT_NAME(inst, "output")) {
            xsltCheckTopLevelElement(style, inst, 1);
            return;
        } else if (IS_XSLT_NAME(inst, "preserve-space")) {
            xsltCheckTopLevelElement(style, inst, 1);
            return;
        } else if (IS_XSLT_NAME(inst, "strip-space")) {
            xsltCheckTopLevelElement(style, inst, 1);
            return;
        } else if (IS_XSLT_NAME(inst, "stylesheet")
                   || IS_XSLT_NAME(inst, "transform")) {
            xmlNodePtr parent = xmlNodeGetParent(inst);

            if (parent == NULL
                || xmlNodeGetType(parent) != XML_DOCUMENT_NODE) {
                xsltTransformError(
                    NULL, style, inst,
                    "element %s only allowed only as root element\n",
                    xmlNodeGetName(inst));
                style->errors++;
            }
            return;
        } else if (IS_XSLT_NAME(inst, "key")) {
            xsltCheckTopLevelElement(style, inst, 1);
            return;
        } else if (IS_XSLT_NAME(inst, "message")) {
            xsltCheckInstructionElement(style, inst);
            return;
        } else if (IS_XSLT_NAME(inst, "attribute-set")) {
            xsltCheckTopLevelElement(style, inst, 1);
            return;
        } else if (IS_XSLT_NAME(inst, "namespace-alias")) {
            xsltCheckTopLevelElement(style, inst, 1);
            return;
        } else if (IS_XSLT_NAME(inst, "include")) {
            xsltCheckTopLevelElement(style, inst, 1);
            return;
        } else if (IS_XSLT_NAME(inst, "import")) {
            xsltCheckTopLevelElement(style, inst, 1);
            return;
        } else if (IS_XSLT_NAME(inst, "decimal-format")) {
            xsltCheckTopLevelElement(style, inst, 1);
            return;
        } else if (IS_XSLT_NAME(inst, "fallback")) {
            xsltCheckInstructionElement(style, inst);
            return;
        } else if (IS_XSLT_NAME(inst, "document")) {
            xsltCheckInstructionElement(style, inst);
            xmlNodeSetPsvi(
                inst, (void *) xsltDocumentComp(style, inst, xsltDocumentElem));
        } else if (style == NULL || style->forwards_compatible == 0) {
            xsltTransformError(NULL, style, inst,
                               "xsltStylePreCompute: unknown xsl:%s\n",
                               xmlNodeGetName(inst));
            if (style != NULL)
                style->warnings++;
        }

        cur = (xsltStylePreCompPtr) xmlNodeGetPsvi(inst);
        /*
	* A ns-list is build for every XSLT item in the
	* node-tree. This is needed for XPath expressions.
	*/
        if (cur != NULL) {
            int i = 0;

            cur->nsList = xmlGetNsList(xmlNodeGetDoc(inst), inst);
            if (cur->nsList != NULL) {
                while (cur->nsList[i] != NULL)
                    i++;
            }
            cur->nsNr = i;
        }
    } else {
        xmlNodeSetPsvi(inst,
                       (void *) xsltPreComputeExtModuleElement(style, inst));

        /*
	 * Unknown element, maybe registered at the context
	 * level. Mark it for later recognition.
	 */
        if (xmlNodeGetPsvi(inst) == NULL)
            xmlNodeSetPsvi(inst, (void *) xsltExtMarker);
    }
}
