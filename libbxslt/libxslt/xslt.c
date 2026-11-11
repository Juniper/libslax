/*
 * xslt.c: Implemetation of an XSL Transformation 1.0 engine
 *
 * Reference:
 *   XSLT specification
 *   http://www.w3.org/TR/1999/REC-xslt-19991116
 *
 *   Associating Style Sheets with XML documents
 *   http://www.w3.org/1999/06/REC-xml-stylesheet-19990629
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
#include <libxml/xmlerror.h>
#include <libxml/parserInternals.h>
#include <libxml/xpathInternals.h>
#include <libxml/xpath.h>
#include "xslt.h"
#include "xsltInternals.h"
#include "pattern.h"
#include "variables.h"
#include "namespaces.h"
#include "attributes.h"
#include "xsltutils.h"
#include "imports.h"
#include "keys.h"
#include "documents.h"
#include "extensions.h"
#include "preproc.h"
#include "extra.h"
#include "security.h"
#include "xsltlocale.h"

#ifdef WITH_XSLT_VERBOSE
#define WITH_XSLT_VERBOSE_PARSING 1
/* #define WITH_XSLT_VERBOSE_BLANKS */
#endif

/*
 * xsltverbose.h must be included after the local override above, since
 * it reads WITH_XSLT_VERBOSE_PARSING/WITH_XSLT_VERBOSE_BLANKS to compute
 * XSLT_VB_PARSING/XSLT_VB_BLANKS.
 */
#include "xsltverbose.h"

const char *xsltEngineVersion = LIBXSLT_VERSION_STRING LIBXSLT_VERSION_EXTRA;
const int xsltLibxsltVersion = LIBXSLT_VERSION;
const int xsltLibxmlVersion = LIBXML_VERSION;


#ifdef XSLT_LOCALE_WINAPI
extern xmlRMutexPtr xsltLocaleMutex;
#endif

/*
 * Useful macros
 */

#ifdef IS_BLANK
#undef IS_BLANK
#endif
#define IS_BLANK(c) \
    (((c) == 0x20) || ((c) == 0x09) || ((c) == 0xA) || ((c) == 0x0D))

#ifdef IS_BLANK_NODE
#undef IS_BLANK_NODE
#endif
#define IS_BLANK_NODE(n)                  \
    ((xmlNodeGetType(n) == XML_TEXT_NODE) \
     && (xsltIsBlank(xmlNodeGetContentRaw(n))))

/**
 * xsltParseContentError:
 *
 * @style: the stylesheet
 * @node: the node where the error occured
 *
 * Compile-time error function.
 */
static void
xsltParseContentError (xsltStylesheetPtr style, xmlNodePtr node)
{
    if (style == NULL || node == NULL)
        return;

    if (IS_XSLT_ELEM(node))
        xsltTransformError(
            NULL, style, node,
            "The XSLT-element '%s' is not allowed at this position.\n",
            xmlNodeGetName(node));
    else
        xsltTransformError(
            NULL, style, node,
            "The element '%s' is not allowed at this position.\n",
            xmlNodeGetName(node));
    style->errors++;
}

/**
 * exclPrefixPush:
 * @style: the transformation stylesheet
 * @value:  the excluded namespace name to push on the stack
 *
 * Push an excluded namespace name on the stack
 *
 * Returns the new index in the stack or -1 if already present or
 * in case of error
 */
static int
exclPrefixPush (xsltStylesheetPtr style, xmlChar *orig)
{
    xmlChar *value;
    int i;

    /*
     * orig can come from a namespace definition on a node which
     * could be deleted later, for example in xsltParseTemplateContent.
     * Store the string in stylesheet's dict to avoid use after free.
     */
    value = (xmlChar *) xmlDictLookup(style->dict, orig, -1);
    if (value == NULL)
        return -1;

    /* do not push duplicates */
    for (i = 0; i < style->exclPrefixNr; i++) {
        if (xmlStrEqual(style->exclPrefixTab[i], value))
            return -1;
    }
    if (style->exclPrefixNr >= style->exclPrefixMax) {
        xmlChar **tmp;
        size_t max = style->exclPrefixMax ? style->exclPrefixMax * 2 : 4;

        tmp = xmlRealloc(style->exclPrefixTab,
                         max * sizeof(style->exclPrefixTab[0]));
        if (tmp == NULL) {
            xmlGenericError(xmlGenericErrorContext, "realloc failed !\n");
            return -1;
        }
        style->exclPrefixTab = tmp;
        style->exclPrefixMax = max;
    }
    style->exclPrefixTab[style->exclPrefixNr] = value;
    style->exclPrefix = value;
    return style->exclPrefixNr++;
}
/**
 * exclPrefixPop:
 * @style: the transformation stylesheet
 *
 * Pop an excluded prefix value from the stack
 *
 * Returns the stored excluded prefix value
 */
static xmlChar *
exclPrefixPop (xsltStylesheetPtr style)
{
    xmlChar *ret;

    if (style->exclPrefixNr <= 0)
        return 0;
    style->exclPrefixNr--;
    if (style->exclPrefixNr > 0)
        style->exclPrefix = style->exclPrefixTab[style->exclPrefixNr - 1];
    else
        style->exclPrefix = NULL;
    ret = style->exclPrefixTab[style->exclPrefixNr];
    style->exclPrefixTab[style->exclPrefixNr] = 0;
    return ret;
}

/************************************************************************
 *									*
 *			Helper functions				*
 *									*
 ************************************************************************/

static int initialized = 0;
/**
 * xsltInit:
 *
 * Initializes the processor (e.g. registers built-in extensions,
 * etc.)
 */
void
xsltInit (void)
{
    if (initialized == 0) {
        initialized = 1;
#ifdef XSLT_LOCALE_WINAPI
        xsltLocaleMutex = xmlNewRMutex();
#endif
        xsltRegisterAllExtras();
    }
}

/**
 * xsltUninit:
 *
 * Uninitializes the processor.
 */
void
xsltUninit (void)
{
#ifdef XSLT_LOCALE_WINAPI
    xmlFreeRMutex(xsltLocaleMutex);
    xsltLocaleMutex = NULL;
#endif
    initialized = 0;
}

/**
 * xsltIsBlank:
 * @str:  a string
 *
 * Check if a string is ignorable
 *
 * Returns 1 if the string is NULL or made of blanks chars, 0 otherwise
 */
int
xsltIsBlank (xmlChar *str)
{
    if (str == NULL)
        return 1;
    while (*str != 0) {
        if (!(IS_BLANK(*str)))
            return 0;
        str++;
    }
    return 1;
}

/************************************************************************
 *									*
 *		Routines to handle XSLT data structures			*
 *									*
 ************************************************************************/
static xsltDecimalFormatPtr
xsltNewDecimalFormat (const xmlChar *nsUri, xmlChar *name)
{
    xsltDecimalFormatPtr self;
    /* UTF-8 for 0x2030 */
    static const xmlChar permille[4] = {0xe2, 0x80, 0xb0, 0};

    self = xmlMalloc(sizeof(xsltDecimalFormat));
    if (self != NULL) {
        self->next = NULL;
        self->nsUri = nsUri;
        self->name = name;

        /* Default values */
        self->digit = xmlStrdup(BAD_CAST("#"));
        self->patternSeparator = xmlStrdup(BAD_CAST(";"));
        self->decimalPoint = xmlStrdup(BAD_CAST("."));
        self->grouping = xmlStrdup(BAD_CAST(","));
        self->percent = xmlStrdup(BAD_CAST("%"));
        self->permille = xmlStrdup(BAD_CAST(permille));
        self->zeroDigit = xmlStrdup(BAD_CAST("0"));
        self->minusSign = xmlStrdup(BAD_CAST("-"));
        self->infinity = xmlStrdup(BAD_CAST("Infinity"));
        self->noNumber = xmlStrdup(BAD_CAST("NaN"));
    }
    return self;
}

static void
xsltFreeDecimalFormat (xsltDecimalFormatPtr self)
{
    if (self != NULL) {
        if (self->digit)
            xmlFree(self->digit);
        if (self->patternSeparator)
            xmlFree(self->patternSeparator);
        if (self->decimalPoint)
            xmlFree(self->decimalPoint);
        if (self->grouping)
            xmlFree(self->grouping);
        if (self->percent)
            xmlFree(self->percent);
        if (self->permille)
            xmlFree(self->permille);
        if (self->zeroDigit)
            xmlFree(self->zeroDigit);
        if (self->minusSign)
            xmlFree(self->minusSign);
        if (self->infinity)
            xmlFree(self->infinity);
        if (self->noNumber)
            xmlFree(self->noNumber);
        if (self->name)
            xmlFree(self->name);
        xmlFree(self);
    }
}

static void
xsltFreeDecimalFormatList (xsltStylesheetPtr self)
{
    xsltDecimalFormatPtr iter;
    xsltDecimalFormatPtr tmp;

    if (self == NULL)
        return;

    iter = self->decimalFormat;
    while (iter != NULL) {
        tmp = iter->next;
        xsltFreeDecimalFormat(iter);
        iter = tmp;
    }
}

/**
 * xsltDecimalFormatGetByName:
 * @style: the XSLT stylesheet
 * @name: the decimal-format name to find
 *
 * Find decimal-format by name
 *
 * Returns the xsltDecimalFormatPtr
 */
xsltDecimalFormatPtr
xsltDecimalFormatGetByName (xsltStylesheetPtr style, xmlChar *name)
{
    xsltDecimalFormatPtr result = NULL;

    if (name == NULL)
        return style->decimalFormat;

    while (style != NULL) {
        for (result = style->decimalFormat->next; result != NULL;
             result = result->next) {
            if (result->nsUri == NULL && xmlStrEqual(name, result->name))
                return result;
        }
        style = xsltNextImport(style);
    }
    return result;
}

/**
 * xsltDecimalFormatGetByQName:
 * @style: the XSLT stylesheet
 * @nsUri: the namespace URI of the QName
 * @name: the local part of the QName
 *
 * Find decimal-format by QName
 *
 * Returns the xsltDecimalFormatPtr
 */
xsltDecimalFormatPtr
xsltDecimalFormatGetByQName (xsltStylesheetPtr style, const xmlChar *nsUri,
                             const xmlChar *name)
{
    xsltDecimalFormatPtr result = NULL;

    if (name == NULL)
        return style->decimalFormat;

    while (style != NULL) {
        for (result = style->decimalFormat->next; result != NULL;
             result = result->next) {
            if (xmlStrEqual(nsUri, result->nsUri)
                && xmlStrEqual(name, result->name))
                return result;
        }
        style = xsltNextImport(style);
    }
    return result;
}

/**
 * xsltNewTemplate:
 *
 * Create a new XSLT Template
 *
 * Returns the newly allocated xsltTemplatePtr or NULL in case of error
 */
static xsltTemplatePtr
xsltNewTemplate (void)
{
    xsltTemplatePtr cur;

    cur = (xsltTemplatePtr) xmlMalloc(sizeof(xsltTemplate));
    if (cur == NULL) {
        xsltTransformError(NULL, NULL, NULL,
                           "xsltNewTemplate : malloc failed\n");
        return NULL;
    }
    memset(cur, 0, sizeof(xsltTemplate));
    cur->priority = XSLT_PAT_NO_PRIORITY;
    return cur;
}

/**
 * xsltFreeTemplate:
 * @template:  an XSLT template
 *
 * Free up the memory allocated by @template
 */
static void
xsltFreeTemplate (xsltTemplatePtr template)
{
    if (template == NULL)
        return;
    if (template->match)
        xmlFree(template->match);
    /*
*   NOTE: @name and @nameURI are put into the string dict now.
*   if (template->name) xmlFree(template->name);
*   if (template->nameURI) xmlFree(template->nameURI);
*/
    /*
    if (template->mode) xmlFree(template->mode);
    if (template->modeURI) xmlFree(template->modeURI);
 */
    if (template->inheritedNs)
        xmlFree(template->inheritedNs);

    /* free profiling data */
    if (template->templCalledTab)
        xmlFree(template->templCalledTab);
    if (template->templCountTab)
        xmlFree(template->templCountTab);

    memset(template, -1, sizeof(xsltTemplate));
    xmlFree(template);
}

/**
 * xsltFreeTemplateList:
 * @template:  an XSLT template list
 *
 * Free up the memory allocated by all the elements of @template
 */
static void
xsltFreeTemplateList (xsltTemplatePtr template)
{
    xsltTemplatePtr cur;

    while (template != NULL) {
        cur = template;
        template = template->next;
        xsltFreeTemplate(cur);
    }
}


/**
 * xsltNewStylesheetInternal:
 * @parent:  the parent stylesheet or NULL
 *
 * Create a new XSLT Stylesheet
 *
 * Returns the newly allocated xsltStylesheetPtr or NULL in case of error
 */
static xsltStylesheetPtr
xsltNewStylesheetInternal (xsltStylesheetPtr parent)
{
    xsltStylesheetPtr ret = NULL;

    ret = (xsltStylesheetPtr) xmlMalloc(sizeof(xsltStylesheet));
    if (ret == NULL) {
        xsltTransformError(NULL, NULL, NULL,
                           "xsltNewStylesheet : malloc failed\n");
        goto internal_err;
    }
    memset(ret, 0, sizeof(xsltStylesheet));

    ret->parent = parent;
    ret->omitXmlDeclaration = -1;
    ret->standalone = -1;
    ret->decimalFormat = xsltNewDecimalFormat(NULL, NULL);
    ret->indent = -1;
    ret->errors = 0;
    ret->warnings = 0;
    ret->exclPrefixNr = 0;
    ret->exclPrefixMax = 0;
    ret->exclPrefixTab = NULL;
    ret->extInfos = NULL;
    ret->extrasNr = 0;
    ret->internalized = 1;
    ret->literal_result = 0;
    ret->forwards_compatible = 0;
    ret->dict = xmlDictCreate();
    xsltVerbose(XSLT_VB_GENERAL, xsltGenericDebugContext,
                "creating dictionary for stylesheet\n");

    if (parent == NULL) {
        ret->principal = ret;

        ret->xpathCtxt = xmlXPathNewContext(NULL);
        if (ret->xpathCtxt == NULL) {
            xsltTransformError(
                NULL, NULL, NULL,
                "xsltNewStylesheet: xmlXPathNewContext failed\n");
            goto internal_err;
        }
        if (xmlXPathContextSetCache(ret->xpathCtxt, 1, -1, 0) == -1)
            goto internal_err;
    } else {
        ret->principal = parent->principal;
    }

    xsltInit();

    return ret;

internal_err:
    if (ret != NULL)
        xsltFreeStylesheet(ret);
    return NULL;
}

/**
 * xsltNewStylesheet:
 *
 * Create a new XSLT Stylesheet
 *
 * Returns the newly allocated xsltStylesheetPtr or NULL in case of error
 */
xsltStylesheetPtr
xsltNewStylesheet (void)
{
    return xsltNewStylesheetInternal(NULL);
}

/**
 * xsltAllocateExtra:
 * @style:  an XSLT stylesheet
 *
 * Allocate an extra runtime information slot statically while compiling
 * the stylesheet and return its number
 *
 * Returns the number of the slot
 */
int
xsltAllocateExtra (xsltStylesheetPtr style)
{
    return style->extrasNr++;
}

/**
 * xsltAllocateExtraCtxt:
 * @ctxt:  an XSLT transformation context
 *
 * Allocate an extra runtime information slot at run-time
 * and return its number
 * This make sure there is a slot ready in the transformation context
 *
 * Returns the number of the slot
 */
int
xsltAllocateExtraCtxt (xsltTransformContextPtr ctxt)
{
    if (ctxt->extrasNr >= ctxt->extrasMax) {
        int i;
        if (ctxt->extrasNr == 0) {
            ctxt->extrasMax = 20;
            ctxt->extras = (xsltRuntimeExtraPtr) xmlMalloc(
                ctxt->extrasMax * sizeof(xsltRuntimeExtra));
            if (ctxt->extras == NULL) {
                xsltTransformError(ctxt, NULL, NULL,
                                   "xsltAllocateExtraCtxt: out of memory\n");
                return 0;
            }
            for (i = 0; i < ctxt->extrasMax; i++) {
                ctxt->extras[i].info = NULL;
                ctxt->extras[i].deallocate = NULL;
                ctxt->extras[i].val.ptr = NULL;
            }

        } else {
            xsltRuntimeExtraPtr tmp;

            ctxt->extrasMax += 100;
            tmp = (xsltRuntimeExtraPtr) xmlRealloc(
                ctxt->extras, ctxt->extrasMax * sizeof(xsltRuntimeExtra));
            if (tmp == NULL) {
                xsltTransformError(ctxt, NULL, NULL,
                                   "xsltAllocateExtraCtxt: out of memory\n");
                return 0;
            }
            ctxt->extras = tmp;
            for (i = ctxt->extrasNr; i < ctxt->extrasMax; i++) {
                ctxt->extras[i].info = NULL;
                ctxt->extras[i].deallocate = NULL;
                ctxt->extras[i].val.ptr = NULL;
            }
        }
    }
    return ctxt->extrasNr++;
}

/**
 * xsltFreeStylesheetList:
 * @style:  an XSLT stylesheet list
 *
 * Free up the memory allocated by the list @style
 */
static void
xsltFreeStylesheetList (xsltStylesheetPtr style)
{
    xsltStylesheetPtr next;

    while (style != NULL) {
        next = style->next;
        xsltFreeStylesheet(style);
        style = next;
    }
}

/**
 * xsltCleanupStylesheetTree:
 *
 * @doc: the document-node
 * @node: the element where the stylesheet is rooted at
 *
 * Actually @node need not be the document-element, but
 * currently Libxslt does not support embedded stylesheets.
 *
 * Returns 0 if OK, -1 on API or internal errors.
 */
static int
xsltCleanupStylesheetTree (xmlDocPtr doc UNUSED, xmlNodePtr rootElem UNUSED)
{
    return 0;
}

/**
 * xsltFreeStylesheet:
 * @style:  an XSLT stylesheet
 *
 * Free up the memory allocated by @style
 */
void
xsltFreeStylesheet (xsltStylesheetPtr style)
{
    if (style == NULL)
        return;

    /*
    * Start with a cleanup of the main stylesheet's doc.
    */
    if (style->parent == NULL && style->doc)
        xsltCleanupStylesheetTree(style->doc, xmlDocGetRootElement(style->doc));

    xsltFreeKeys(style);
    xsltFreeExts(style);
    xsltFreeTemplateHashes(style);
    xsltFreeDecimalFormatList(style);
    xsltFreeTemplateList(style->templates);
    xsltFreeAttributeSetsHashes(style);
    xsltFreeNamespaceAliasHashes(style);
    xsltFreeStylePreComps(style);
    /*
    * Free documents of all included stylsheet modules of this
    * stylesheet level.
    */
    xsltFreeStyleDocuments(style);
    /*
    * TODO: Best time to shutdown extension stuff?
    */
    xsltShutdownExts(style);

    if (style->variables != NULL)
        xsltFreeStackElemList(style->variables);
    if (style->cdataSection != NULL)
        xmlHashFree(style->cdataSection, NULL);
    if (style->stripSpaces != NULL)
        xmlHashFree(style->stripSpaces, NULL);
    if (style->nsHash != NULL)
        xmlHashFree(style->nsHash, NULL);
    if (style->exclPrefixTab != NULL)
        xmlFree(style->exclPrefixTab);
    if (style->method != NULL)
        xmlFree(style->method);
    if (style->methodURI != NULL)
        xmlFree(style->methodURI);
    if (style->version != NULL)
        xmlFree(style->version);
    if (style->encoding != NULL)
        xmlFree(style->encoding);
    if (style->doctypePublic != NULL)
        xmlFree(style->doctypePublic);
    if (style->doctypeSystem != NULL)
        xmlFree(style->doctypeSystem);
    if (style->mediaType != NULL)
        xmlFree(style->mediaType);
    if (style->attVTs)
        xsltFreeAVTList(style->attVTs);
    if (style->imports != NULL)
        xsltFreeStylesheetList(style->imports);

    /*
    * Better to free the main document of this stylesheet level
    * at the end - so here.
    */
    if (style->doc != NULL) {
        xmlFreeDoc(style->doc);
    }

    xsltVerbose(XSLT_VB_GENERAL, xsltGenericDebugContext,
                "freeing dictionary from stylesheet\n");
    xmlDictFree(style->dict);

    if (style->xpathCtxt != NULL)
        xmlXPathFreeContext(style->xpathCtxt);

    memset(style, -1, sizeof(xsltStylesheet));
    xmlFree(style);
}

/************************************************************************
 *									*
 *		Parsing of an XSLT Stylesheet				*
 *									*
 ************************************************************************/

/**
 * xsltGetInheritedNsList:
 * @style:  the stylesheet
 * @template: the template
 * @node:  the current node
 *
 * Search all the namespace applying to a given element except the ones
 * from excluded output prefixes currently in scope. Initialize the
 * template inheritedNs list with it.
 *
 * Returns the number of entries found
 */
static int
xsltGetInheritedNsList (xsltStylesheetPtr style, xsltTemplatePtr template,
                        xmlNodePtr node)
{
    xmlNsPtr cur;
    xmlNsPtr *ret = NULL, *tmp;
    int nbns = 0;
    int maxns = 0;
    int i;

    if (style == NULL || template == NULL || node == NULL
        || template->inheritedNsNr != 0 || template->inheritedNs != NULL)
        return 0;
    while (node != NULL) {
        if (xmlNodeGetType(node) == XML_ELEMENT_NODE) {
            cur = xmlNodeGetNsDef(node);
            while (cur != NULL) {
                if (xmlStrEqual(xmlNsGetHref(cur), XSLT_NAMESPACE))
                    goto skip_ns;

                if (xmlNsGetPrefix(cur) != NULL
                    && xsltCheckExtPrefix(style, xmlNsGetPrefix(cur)))
                    goto skip_ns;
                /*
		* Check if this namespace was excluded.
		* Note that at this point only the exclusions defined
		* on the topmost stylesheet element are in the exclusion-list.
		*/
                for (i = 0; i < style->exclPrefixNr; i++) {
                    if (xmlStrEqual(xmlNsGetHref(cur), style->exclPrefixTab[i]))
                        goto skip_ns;
                }
                /*
		* Skip shadowed namespace bindings.
		*/
                for (i = 0; i < nbns; i++) {
                    if (xmlNsGetPrefix(cur) == xmlNsGetPrefix(ret[i])
                        || xmlStrEqual(xmlNsGetPrefix(cur),
                                        xmlNsGetPrefix(ret[i])))
                        break;
                }
                if (i >= nbns) {
                    if (nbns >= maxns) {
                        maxns = (maxns == 0) ? 10 : 2 * maxns;
                        tmp = (xmlNsPtr *) xmlRealloc(
                            ret, (maxns + 1) * sizeof(xmlNsPtr));
                        if (tmp == NULL) {
                            xmlGenericError(
                                xmlGenericErrorContext,
                                "xsltGetInheritedNsList : realloc failed!\n");
                            xmlFree(ret);
                            return 0;
                        }
                        ret = tmp;
                    }
                    ret[nbns++] = cur;
                    ret[nbns] = NULL;
                }
            skip_ns:
                cur = xmlNsGetNext(cur);
            }
        }
        node = xmlNodeGetParent(node);
    }
    if (nbns != 0) {
        xsltVerbose(XSLT_VB_PARSING, xsltGenericDebugContext,
                    "template has %d inherited namespaces\n", nbns);
        template->inheritedNsNr = nbns;
        template->inheritedNs = ret;
    }
    return nbns;
}

/**
 * xsltParseStylesheetOutput:
 * @style:  the XSLT stylesheet
 * @cur:  the "output" element
 *
 * parse an XSLT stylesheet output element and record
 * information related to the stylesheet output
 */

void
xsltParseStylesheetOutput (xsltStylesheetPtr style, xmlNodePtr cur)
{
    xmlChar *elements, *prop;
    xmlChar *element, *end;

    if (cur == NULL || style == NULL
        || xmlNodeGetType(cur) != XML_ELEMENT_NODE)
        return;

    prop = xmlGetNsProp(cur, (const xmlChar *) "version", NULL);
    if (prop != NULL) {
        if (style->version != NULL)
            xmlFree(style->version);
        style->version = prop;
    }

    prop = xmlGetNsProp(cur, (const xmlChar *) "encoding", NULL);
    if (prop != NULL) {
        if (style->encoding != NULL)
            xmlFree(style->encoding);
        style->encoding = prop;
    }

    /* relaxed to support xt:document
    * TODO KB: What does "relaxed to support xt:document" mean?
    */
    prop = xmlGetNsProp(cur, (const xmlChar *) "method", NULL);
    if (prop != NULL) {
        const xmlChar *URI;

        if (style->method != NULL)
            xmlFree(style->method);
        style->method = NULL;
        if (style->methodURI != NULL)
            xmlFree(style->methodURI);
        style->methodURI = NULL;

        /*
	* TODO: Don't use xsltGetQNameURI().
	*/
        URI = xsltGetQNameURI(cur, &prop);
        if (prop == NULL) {
            if (style != NULL)
                style->errors++;
        } else if (URI == NULL) {
            if (xmlStrEqual(prop, (const xmlChar *) "xml")
                || xmlStrEqual(prop, (const xmlChar *) "html")
                || xmlStrEqual(prop, (const xmlChar *) "text")) {
                style->method = prop;
            } else {
                xsltTransformError(NULL, style, cur,
                                   "invalid value for method: %s\n", prop);
                if (style != NULL)
                    style->warnings++;
                xmlFree(prop);
            }
        } else {
            style->method = prop;
            style->methodURI = xmlStrdup(URI);
        }
    }

    prop = xmlGetNsProp(cur, (const xmlChar *) "doctype-system", NULL);
    if (prop != NULL) {
        if (style->doctypeSystem != NULL)
            xmlFree(style->doctypeSystem);
        style->doctypeSystem = prop;
    }

    prop = xmlGetNsProp(cur, (const xmlChar *) "doctype-public", NULL);
    if (prop != NULL) {
        if (style->doctypePublic != NULL)
            xmlFree(style->doctypePublic);
        style->doctypePublic = prop;
    }

    prop = xmlGetNsProp(cur, (const xmlChar *) "standalone", NULL);
    if (prop != NULL) {
        if (xmlStrEqual(prop, (const xmlChar *) "yes")) {
            style->standalone = 1;
        } else if (xmlStrEqual(prop, (const xmlChar *) "no")) {
            style->standalone = 0;
        } else {
            xsltTransformError(NULL, style, cur,
                               "invalid value for standalone: %s\n", prop);
            style->errors++;
        }
        xmlFree(prop);
    }

    prop = xmlGetNsProp(cur, (const xmlChar *) "indent", NULL);
    if (prop != NULL) {
        if (xmlStrEqual(prop, (const xmlChar *) "yes")) {
            style->indent = 1;
        } else if (xmlStrEqual(prop, (const xmlChar *) "no")) {
            style->indent = 0;
        } else {
            xsltTransformError(NULL, style, cur,
                               "invalid value for indent: %s\n", prop);
            style->errors++;
        }
        xmlFree(prop);
    }

    prop = xmlGetNsProp(cur, (const xmlChar *) "omit-xml-declaration", NULL);
    if (prop != NULL) {
        if (xmlStrEqual(prop, (const xmlChar *) "yes")) {
            style->omitXmlDeclaration = 1;
        } else if (xmlStrEqual(prop, (const xmlChar *) "no")) {
            style->omitXmlDeclaration = 0;
        } else {
            xsltTransformError(NULL, style, cur,
                               "invalid value for omit-xml-declaration: %s\n",
                               prop);
            style->errors++;
        }
        xmlFree(prop);
    }

    elements
        = xmlGetNsProp(cur, (const xmlChar *) "cdata-section-elements", NULL);
    if (elements != NULL) {
        if (style->cdataSection == NULL)
            style->cdataSection = xmlHashCreate(10);
        if (style->cdataSection == NULL) {
            xmlFree(elements);
            return;
        }

        element = elements;
        while (*element != 0) {
            while (IS_BLANK(*element))
                element++;
            if (*element == 0)
                break;
            end = element;
            while (*end != 0 && !IS_BLANK(*end))
                end++;
            element = xmlStrndup(element, end - element);
            if (element) {
                xsltVerbose(XSLT_VB_PARSING, xsltGenericDebugContext,
                            "add cdata section output element %s\n",
                            element);
                if (xmlValidateQName(BAD_CAST element, 0) != 0) {
                    xsltTransformError(
                        NULL, style, cur,
                        "Attribute 'cdata-section-elements': The value "
                        "'%s' is not a valid QName.\n",
                        element);
                    xmlFree(element);
                    style->errors++;
                } else {
                    const xmlChar *URI;

                    /*
		    * TODO: Don't use xsltGetQNameURI().
		    */
                    URI = xsltGetQNameURI(cur, &element);
                    if (element == NULL) {
                        /*
			* TODO: We'll report additionally an error
			*  via the stylesheet's error handling.
			*/
                        xsltTransformError(
                            NULL, style, cur,
                            "Attribute 'cdata-section-elements': "
                            "Not a valid QName.\n");
                        style->errors++;
                    } else {
                        xmlNsPtr ns;

                        /*
			* XSLT-1.0 "Each QName is expanded into an
			*  expanded-name using the namespace declarations in
			*  effect on the xsl:output element in which the QName
			*  occurs; if there is a default namespace, it is used
			*  for QNames that do not have a prefix"
			* NOTE: Fix of bug #339570.
			*/
                        if (URI == NULL) {
                            ns = xmlSearchNs(style->doc, cur, NULL);
                            if (ns != NULL)
                                URI = xmlNsGetHref(ns);
                        }
                        xmlHashAddEntry2(style->cdataSection, element, URI,
                                         (void *) "cdata");
                        xmlFree(element);
                    }
                }
            }
            element = end;
        }
        xmlFree(elements);
    }

    prop = xmlGetNsProp(cur, (const xmlChar *) "media-type", NULL);
    if (prop != NULL) {
        if (style->mediaType)
            xmlFree(style->mediaType);
        style->mediaType = prop;
    }
    if (xmlNodeGetChildren(cur) != NULL) {
        xsltParseContentError(style, xmlNodeGetChildren(cur));
    }
}

/**
 * xsltParseStylesheetDecimalFormat:
 * @style:  the XSLT stylesheet
 * @cur:  the "decimal-format" element
 *
 * <!-- Category: top-level-element -->
 * <xsl:decimal-format
 *   name = qname, decimal-separator = char, grouping-separator = char,
 *   infinity = string, minus-sign = char, NaN = string, percent = char
 *   per-mille = char, zero-digit = char, digit = char,
 * pattern-separator = char />
 *
 * parse an XSLT stylesheet decimal-format element and
 * and record the formatting characteristics
 */
static void
xsltParseStylesheetDecimalFormat (xsltStylesheetPtr style, xmlNodePtr cur)
{
    xmlChar *prop;
    xsltDecimalFormatPtr format;
    xsltDecimalFormatPtr iter;

    if (cur == NULL || style == NULL
        || xmlNodeGetType(cur) != XML_ELEMENT_NODE)
        return;

    format = style->decimalFormat;

    prop = xmlGetNsProp(cur, BAD_CAST("name"), NULL);
    if (prop != NULL) {
        const xmlChar *nsUri;

        if (xmlValidateQName(prop, 0) != 0) {
            xsltTransformError(NULL, style, cur,
                               "xsl:decimal-format: Invalid QName '%s'.\n",
                               prop);
            style->warnings++;
            xmlFree(prop);
            return;
        }
        /*
        * TODO: Don't use xsltGetQNameURI().
        */
        nsUri = xsltGetQNameURI(cur, &prop);
        if (prop == NULL) {
            style->warnings++;
            return;
        }
        format = xsltDecimalFormatGetByQName(style, nsUri, prop);
        if (format != NULL) {
            xsltTransformError(
                NULL, style, cur,
                "xsltParseStylestyleDecimalFormat: %s already exists\n", prop);
            style->warnings++;
            xmlFree(prop);
            return;
        }
        format = xsltNewDecimalFormat(nsUri, prop);
        if (format == NULL) {
            xsltTransformError(NULL, style, cur,
                               "xsltParseStylestyleDecimalFormat: failed "
                               "creating new decimal-format\n");
            style->errors++;
            xmlFree(prop);
            return;
        }
        /* Append new decimal-format structure */
        for (iter = style->decimalFormat; iter->next; iter = iter->next)
            ;
        if (iter)
            iter->next = format;
    }

    prop = xmlGetNsProp(cur, (const xmlChar *) "decimal-separator", NULL);
    if (prop != NULL) {
        if (format->decimalPoint != NULL)
            xmlFree(format->decimalPoint);
        format->decimalPoint = prop;
    }

    prop = xmlGetNsProp(cur, (const xmlChar *) "grouping-separator", NULL);
    if (prop != NULL) {
        if (format->grouping != NULL)
            xmlFree(format->grouping);
        format->grouping = prop;
    }

    prop = xmlGetNsProp(cur, (const xmlChar *) "infinity", NULL);
    if (prop != NULL) {
        if (format->infinity != NULL)
            xmlFree(format->infinity);
        format->infinity = prop;
    }

    prop = xmlGetNsProp(cur, (const xmlChar *) "minus-sign", NULL);
    if (prop != NULL) {
        if (format->minusSign != NULL)
            xmlFree(format->minusSign);
        format->minusSign = prop;
    }

    prop = xmlGetNsProp(cur, (const xmlChar *) "NaN", NULL);
    if (prop != NULL) {
        if (format->noNumber != NULL)
            xmlFree(format->noNumber);
        format->noNumber = prop;
    }

    prop = xmlGetNsProp(cur, (const xmlChar *) "percent", NULL);
    if (prop != NULL) {
        if (format->percent != NULL)
            xmlFree(format->percent);
        format->percent = prop;
    }

    prop = xmlGetNsProp(cur, (const xmlChar *) "per-mille", NULL);
    if (prop != NULL) {
        if (format->permille != NULL)
            xmlFree(format->permille);
        format->permille = prop;
    }

    prop = xmlGetNsProp(cur, (const xmlChar *) "zero-digit", NULL);
    if (prop != NULL) {
        if (format->zeroDigit != NULL)
            xmlFree(format->zeroDigit);
        format->zeroDigit = prop;
    }

    prop = xmlGetNsProp(cur, (const xmlChar *) "digit", NULL);
    if (prop != NULL) {
        if (format->digit != NULL)
            xmlFree(format->digit);
        format->digit = prop;
    }

    prop = xmlGetNsProp(cur, (const xmlChar *) "pattern-separator", NULL);
    if (prop != NULL) {
        if (format->patternSeparator != NULL)
            xmlFree(format->patternSeparator);
        format->patternSeparator = prop;
    }
    if (xmlNodeGetChildren(cur) != NULL) {
        xsltParseContentError(style, xmlNodeGetChildren(cur));
    }
}

/**
 * xsltParseStylesheetPreserveSpace:
 * @style:  the XSLT stylesheet
 * @cur:  the "preserve-space" element
 *
 * parse an XSLT stylesheet preserve-space element and record
 * elements needing preserving
 */

static void
xsltParseStylesheetPreserveSpace (xsltStylesheetPtr style, xmlNodePtr cur)
{
    xmlChar *elements;
    xmlChar *element, *end;

    if (cur == NULL || style == NULL
        || xmlNodeGetType(cur) != XML_ELEMENT_NODE)
        return;

    elements = xmlGetNsProp(cur, (const xmlChar *) "elements", NULL);
    if (elements == NULL) {
        xsltTransformError(
            NULL, style, cur,
            "xsltParseStylesheetPreserveSpace: missing elements attribute\n");
        if (style != NULL)
            style->warnings++;
        return;
    }

    if (style->stripSpaces == NULL)
        style->stripSpaces = xmlHashCreate(10);
    if (style->stripSpaces == NULL) {
        xmlFree(elements);
        return;
    }

    element = elements;
    while (*element != 0) {
        while (IS_BLANK(*element))
            element++;
        if (*element == 0)
            break;
        end = element;
        while (*end != 0 && !IS_BLANK(*end))
            end++;
        element = xmlStrndup(element, end - element);
        if (element) {
            xsltVerbose(XSLT_VB_PARSING, xsltGenericDebugContext,
                        "add preserved space element %s\n", element);
            if (xmlStrEqual(element, (const xmlChar *) "*")) {
                style->stripAll = -1;
            } else {
                const xmlChar *URI;

                /*
		* TODO: Don't use xsltGetQNameURI().
		*/
                URI = xsltGetQNameURI(cur, &element);

                xmlHashAddEntry2(style->stripSpaces, element, URI,
                                 (xmlChar *) "preserve");
            }
            xmlFree(element);
        }
        element = end;
    }
    xmlFree(elements);
    if (xmlNodeGetChildren(cur) != NULL) {
        xsltParseContentError(style, xmlNodeGetChildren(cur));
    }
}

/**
 * xsltParseStylesheetExtPrefix:
 * @style:  the XSLT stylesheet
 * @template:  the "extension-element-prefixes" prefix
 *
 * parse an XSLT stylesheet's "extension-element-prefix" attribute value
 * and register the namespaces of extension instruction.
 * SPEC "A namespace is designated as an extension namespace by using
 *   an extension-element-prefixes attribute on:
 *   1) an xsl:stylesheet element
 *   2) an xsl:extension-element-prefixes attribute on a
 *      literal result element
 *   3) an extension instruction."
 */
static void
xsltParseStylesheetExtPrefix (xsltStylesheetPtr style, xmlNodePtr cur,
                              int isXsltElem)
{
    xmlChar *prefixes;
    xmlChar *prefix, *end;

    if (cur == NULL || style == NULL
        || xmlNodeGetType(cur) != XML_ELEMENT_NODE)
        return;

    if (isXsltElem) {
        /* For xsl:stylesheet/xsl:transform. */
        prefixes = xmlGetNsProp(
            cur, (const xmlChar *) "extension-element-prefixes", NULL);
    } else {
        /* For literal result elements and extension instructions. */
        prefixes
            = xmlGetNsProp(cur, (const xmlChar *) "extension-element-prefixes",
                           XSLT_NAMESPACE);
    }
    if (prefixes == NULL) {
        return;
    }

    prefix = prefixes;
    while (*prefix != 0) {
        while (IS_BLANK(*prefix))
            prefix++;
        if (*prefix == 0)
            break;
        end = prefix;
        while (*end != 0 && !IS_BLANK(*end))
            end++;
        prefix = xmlStrndup(prefix, end - prefix);
        if (prefix) {
            xmlNsPtr ns;

            if (xmlStrEqual(prefix, (const xmlChar *) "#default"))
                ns = xmlSearchNs(style->doc, cur, NULL);
            else
                ns = xmlSearchNs(style->doc, cur, prefix);
            if (ns == NULL) {
                xsltTransformError(
                    NULL, style, cur,
                    "xsl:extension-element-prefix : undefined namespace %s\n",
                    prefix);
                if (style != NULL)
                    style->warnings++;
            } else {
                xsltVerbose(XSLT_VB_PARSING, xsltGenericDebugContext,
                            "add extension prefix %s\n", prefix);
                xsltRegisterExtPrefix(style, prefix, xmlNsGetHref(ns));
            }
            xmlFree(prefix);
        }
        prefix = end;
    }
    xmlFree(prefixes);
}

/**
 * xsltParseStylesheetStripSpace:
 * @style:  the XSLT stylesheet
 * @cur:  the "strip-space" element
 *
 * parse an XSLT stylesheet's strip-space element and record
 * the elements needing stripping
 */

static void
xsltParseStylesheetStripSpace (xsltStylesheetPtr style, xmlNodePtr cur)
{
    xmlChar *elements;
    xmlChar *element, *end;

    if (cur == NULL || style == NULL
        || xmlNodeGetType(cur) != XML_ELEMENT_NODE)
        return;

    if (style->stripSpaces == NULL)
        style->stripSpaces = xmlHashCreate(10);
    if (style->stripSpaces == NULL)
        return;

    elements = xmlGetNsProp(cur, (const xmlChar *) "elements", NULL);
    if (elements == NULL) {
        xsltTransformError(
            NULL, style, cur,
            "xsltParseStylesheetStripSpace: missing elements attribute\n");
        if (style != NULL)
            style->warnings++;
        return;
    }

    element = elements;
    while (*element != 0) {
        while (IS_BLANK(*element))
            element++;
        if (*element == 0)
            break;
        end = element;
        while (*end != 0 && !IS_BLANK(*end))
            end++;
        element = xmlStrndup(element, end - element);
        if (element) {
            xsltVerbose(XSLT_VB_PARSING, xsltGenericDebugContext,
                        "add stripped space element %s\n", element);
            if (xmlStrEqual(element, (const xmlChar *) "*")) {
                style->stripAll = 1;
            } else {
                const xmlChar *URI;

                /*
		* TODO: Don't use xsltGetQNameURI().
		*/
                URI = xsltGetQNameURI(cur, &element);

                xmlHashAddEntry2(style->stripSpaces, element, URI,
                                 (xmlChar *) "strip");
            }
            xmlFree(element);
        }
        element = end;
    }
    xmlFree(elements);
    if (xmlNodeGetChildren(cur) != NULL) {
        xsltParseContentError(style, xmlNodeGetChildren(cur));
    }
}

/**
 * xsltParseStylesheetExcludePrefix:
 * @style:  the XSLT stylesheet
 * @cur:  the current point in the stylesheet
 *
 * parse an XSLT stylesheet exclude prefix and record
 * namespaces needing stripping
 *
 * Returns the number of Excluded prefixes added at that level
 */

static int
xsltParseStylesheetExcludePrefix (xsltStylesheetPtr style, xmlNodePtr cur,
                                  int isXsltElem)
{
    int nb = 0;
    xmlChar *prefixes;
    xmlChar *prefix, *end;

    if (cur == NULL || style == NULL
        || xmlNodeGetType(cur) != XML_ELEMENT_NODE)
        return 0;

    if (isXsltElem)
        prefixes = xmlGetNsProp(
            cur, (const xmlChar *) "exclude-result-prefixes", NULL);
    else
        prefixes = xmlGetNsProp(
            cur, (const xmlChar *) "exclude-result-prefixes", XSLT_NAMESPACE);

    if (prefixes == NULL) {
        return 0;
    }

    prefix = prefixes;
    while (*prefix != 0) {
        while (IS_BLANK(*prefix))
            prefix++;
        if (*prefix == 0)
            break;
        end = prefix;
        while (*end != 0 && !IS_BLANK(*end))
            end++;
        prefix = xmlStrndup(prefix, end - prefix);
        if (prefix) {
            xmlNsPtr ns;

            if (xmlStrEqual(prefix, (const xmlChar *) "#default"))
                ns = xmlSearchNs(style->doc, cur, NULL);
            else
                ns = xmlSearchNs(style->doc, cur, prefix);
            if (ns == NULL) {
                xsltTransformError(
                    NULL, style, cur,
                    "xsl:exclude-result-prefixes : undefined namespace %s\n",
                    prefix);
                if (style != NULL)
                    style->warnings++;
            } else {
                if (exclPrefixPush(style, (xmlChar *) xmlNsGetHref(ns)) >= 0) {
                    xsltVerbose(XSLT_VB_PARSING, xsltGenericDebugContext,
                                "exclude result prefix %s\n", prefix);
                    nb++;
                }
            }
            xmlFree(prefix);
        }
        prefix = end;
    }
    xmlFree(prefixes);
    return nb;
}


static void
xsltPreprocessStylesheet (xsltStylesheetPtr style, xmlNodePtr cur)
{
    xmlNodePtr deleteNode, styleelem;
    int internalize = 0;

    if (style == NULL || cur == NULL)
        return;

    if (xmlNodeGetDoc(cur) != NULL && style->dict != NULL
        && xmlNodeGetDoc(cur)->dict == style->dict)
        internalize = 1;
    else
        style->internalized = 0;

    if (cur != NULL && IS_XSLT_ELEM(cur)
        && IS_XSLT_NAME(cur, "stylesheet")) {
        styleelem = cur;
    } else {
        styleelem = NULL;
    }

    /*
     * This content comes from the stylesheet
     * For stylesheets, the set of whitespace-preserving
     * element names consists of just xsl:text.
     */
    deleteNode = NULL;
    while (cur != NULL) {
        if (deleteNode != NULL) {
            xsltVerbose(XSLT_VB_BLANKS, xsltGenericDebugContext,
                        "xsltPreprocessStylesheet: removing ignorable blank node\n");
            xmlUnlinkNode(deleteNode);
            xmlFreeNode(deleteNode);
            deleteNode = NULL;
        }
        if (xmlNodeGetType(cur) == XML_ELEMENT_NODE) {
            int exclPrefixes;
            /*
	     * Internalize attributes values.
	     */
            if (internalize && xmlNodeGetProperties(cur) != NULL) {
                xmlAttrPtr attr = xmlNodeGetProperties(cur);
                xmlNodePtr txt;

                while (attr != NULL) {
                    txt = xmlAttrGetChildren(attr);
                    if (txt != NULL && xmlNodeGetType(txt) == XML_TEXT_NODE
                        && xmlNodeGetContentRaw(txt) != NULL
                        && !xmlDictOwns(style->dict,
                                         xmlNodeGetContentRaw(txt))) {
                        xmlChar *tmp;

                        /*
			 * internalize the text string, goal is to speed
			 * up operations and minimize used space by compiled
			 * stylesheets.
			 */
                        tmp = (xmlChar *) xmlDictLookup(
                            style->dict, xmlNodeGetContentRaw(txt), -1);
                        if (tmp != xmlNodeGetContentRaw(txt)) {
                            xmlNodeSetContent(txt, NULL);
                            xmlNodeSetContentRaw(txt, tmp);
                        }
                    }
                    attr = xmlAttrGetNext(attr);
                }
            }
            if (IS_XSLT_ELEM(cur)) {
                exclPrefixes = 0;
                if (IS_XSLT_NAME(cur, "text")) {
                    for (; exclPrefixes > 0; exclPrefixes--)
                        exclPrefixPop(style);
                    goto skip_children;
                }
            } else {
                exclPrefixes = xsltParseStylesheetExcludePrefix(style, cur, 0);
            }

            if (xmlNodeGetNsDef(cur) != NULL && style->exclPrefixNr > 0) {
                xmlNsPtr ns = xmlNodeGetNsDef(cur), prev = NULL, next;
                xmlNodePtr root = NULL;
                int i, moved;

                root = xmlDocGetRootElement(xmlNodeGetDoc(cur));
                if (root != NULL && root != cur) {
                    while (ns != NULL) {
                        moved = 0;
                        next = xmlNsGetNext(ns);
                        for (i = 0; i < style->exclPrefixNr; i++) {
                            if (xmlNsGetPrefix(ns) != NULL
                                && xmlStrEqual(xmlNsGetHref(ns),
                                                style->exclPrefixTab[i])) {
                                /*
				 * Move the namespace definition on the root
				 * element to avoid duplicating it without
				 * loosing it.
				 */
                                if (prev == NULL) {
                                    xmlNodeSetNsDef(cur, xmlNsGetNext(ns));
                                } else {
                                    xmlNsSetNext(prev, xmlNsGetNext(ns));
                                }
                                xmlNsSetNext(ns, xmlNodeGetNsDef(root));
                                xmlNodeSetNsDef(root, ns);
                                moved = 1;
                                break;
                            }
                        }
                        if (moved == 0)
                            prev = ns;
                        ns = next;
                    }
                }
            }
            /*
	     * If we have prefixes locally, recurse and pop them up when
	     * going back
	     */
            if (exclPrefixes > 0) {
                xsltPreprocessStylesheet(style, xmlNodeGetChildren(cur));
                for (; exclPrefixes > 0; exclPrefixes--)
                    exclPrefixPop(style);
                goto skip_children;
            }
        } else if (xmlNodeGetType(cur) == XML_TEXT_NODE) {
            if (IS_BLANK_NODE(cur)) {
                if (xmlNodeGetSpacePreserve(xmlNodeGetParent(cur)) != 1) {
                    deleteNode = cur;
                }
            } else if (xmlNodeGetContentRaw(cur) != NULL && internalize
                       && !xmlDictOwns(style->dict,
                                        xmlNodeGetContentRaw(cur))) {
                xmlChar *tmp;

                /*
		 * internalize the text string, goal is to speed
		 * up operations and minimize used space by compiled
		 * stylesheets.
		 */
                tmp = (xmlChar *) xmlDictLookup(style->dict,
                                                xmlNodeGetContentRaw(cur), -1);
                xmlNodeSetContent(cur, NULL);
                xmlNodeSetContentRaw(cur, tmp);
            }
        } else if (xmlNodeGetType(cur) != XML_ELEMENT_NODE
                   && xmlNodeGetType(cur) != XML_CDATA_SECTION_NODE) {
            deleteNode = cur;
            goto skip_children;
        }

        /*
	 * Skip to next node. In case of a namespaced element children of
	 * the stylesheet and not in the XSLT namespace and not an extension
	 * element, ignore its content.
	 */
        if (xmlNodeGetType(cur) == XML_ELEMENT_NODE
            && xmlNodeGetNs(cur) != NULL && styleelem != NULL
            && xmlNodeGetParent(cur) == styleelem
            && !xmlStrEqual(xmlNsGetHref(xmlNodeGetNs(cur)), XSLT_NAMESPACE)
            && !xsltCheckExtURI(style, xmlNsGetHref(xmlNodeGetNs(cur)))) {
            goto skip_children;
        } else if (xmlNodeGetChildren(cur) != NULL) {
            cur = xmlNodeGetChildren(cur);
            continue;
        }

    skip_children:
        if (xmlNodeGetNext(cur) != NULL) {
            cur = xmlNodeGetNext(cur);
            continue;
        }
        do {
            cur = xmlNodeGetParent(cur);
            if (cur == NULL)
                break;
            if (cur == (xmlNodePtr) style->doc) {
                cur = NULL;
                break;
            }
            if (xmlNodeGetNext(cur) != NULL) {
                cur = xmlNodeGetNext(cur);
                break;
            }
        } while (cur != NULL);
    }
    if (deleteNode != NULL) {
        xsltVerbose(XSLT_VB_PARSING, xsltGenericDebugContext,
                    "xsltPreprocessStylesheet: removing ignorable blank node\n");
        xmlUnlinkNode(deleteNode);
        xmlFreeNode(deleteNode);
    }
}

/**
 * xsltGatherNamespaces:
 * @style:  the XSLT stylesheet
 *
 * Browse the stylesheet and build the namspace hash table which
 * will be used for XPath interpretation. If needed do a bit of normalization
 */

static void
xsltGatherNamespaces (xsltStylesheetPtr style)
{
    xmlNodePtr cur;
    const xmlChar *URI;

    if (style == NULL)
        return;
    /*
     * TODO: basically if the stylesheet uses the same prefix for different
     *       patterns, well they may be in problem, hopefully they will get
     *       a warning first.
     */
    /*
    * TODO: Eliminate the use of the hash for XPath expressions.
    *   An expression should be evaluated in the context of the in-scope
    *   namespaces; eliminate the restriction of an XML document to contain
    *   no duplicate prefixes for different namespace names.
    *
    */
    cur = xmlDocGetRootElement(style->doc);
    while (cur != NULL) {
        if (xmlNodeGetType(cur) == XML_ELEMENT_NODE) {
            xmlNsPtr ns = xmlNodeGetNsDef(cur);
            while (ns != NULL) {
                if (xmlNsGetPrefix(ns) != NULL) {
                    if (style->nsHash == NULL) {
                        style->nsHash = xmlHashCreate(10);
                        if (style->nsHash == NULL) {
                            xsltTransformError(NULL, style, cur,
                                               "xsltGatherNamespaces: failed "
                                               "to create hash table\n");
                            style->errors++;
                            return;
                        }
                    }
                    URI = xmlHashLookup(style->nsHash, xmlNsGetPrefix(ns));
                    if (URI != NULL
                        && !xmlStrEqual(URI, xmlNsGetHref(ns))) {
                        xsltTransformError(NULL, style, cur,
                                           "Namespaces prefix %s used for "
                                           "multiple namespaces\n",
                                           xmlNsGetPrefix(ns));
                        style->warnings++;
                    } else if (URI == NULL) {
                        xmlHashUpdateEntry(style->nsHash, xmlNsGetPrefix(ns),
                                           (void *) xmlNsGetHref(ns), NULL);

                        xsltVerbose(XSLT_VB_PARSING, xsltGenericDebugContext,
                                    "Added namespace: %s mapped to %s\n",
                                    xmlNsGetPrefix(ns), xmlNsGetHref(ns));
                    }
                }
                ns = xmlNsGetNext(ns);
            }
        }

        /*
	 * Skip to next node
	 */
        if (xmlNodeGetChildren(cur) != NULL) {
            if (xmlNodeGetType(xmlNodeGetChildren(cur)) != XML_ENTITY_DECL) {
                cur = xmlNodeGetChildren(cur);
                continue;
            }
        }
        if (xmlNodeGetNext(cur) != NULL) {
            cur = xmlNodeGetNext(cur);
            continue;
        }

        do {
            cur = xmlNodeGetParent(cur);
            if (cur == NULL)
                break;
            if (cur == (xmlNodePtr) style->doc) {
                cur = NULL;
                break;
            }
            if (xmlNodeGetNext(cur) != NULL) {
                cur = xmlNodeGetNext(cur);
                break;
            }
        } while (cur != NULL);
    }
}


/**
 * xsltParseTemplateContent:
 * @style:  the XSLT stylesheet
 * @templ:  the container node (can be a document for literal results)
 *
 * parse a template content-model
 * Clean-up the template content from unwanted ignorable blank nodes
 * and process xslt:text
 */
void
xsltParseTemplateContent (xsltStylesheetPtr style, xmlNodePtr templ)
{
    xmlNodePtr cur, delete;

    if (style == NULL || templ == NULL
        || xmlNodeGetType(templ) == XML_NAMESPACE_DECL)
        return;

    /*
     * This content comes from the stylesheet
     * For stylesheets, the set of whitespace-preserving
     * element names consists of just xsl:text.
     */
    cur = xmlNodeGetChildren(templ);
    delete = NULL;
    while (cur != NULL) {
        style->principal->opCount += 1;

        if (delete != NULL) {
            xsltVerbose(XSLT_VB_BLANKS, xsltGenericDebugContext,
                        "xsltParseTemplateContent: removing text\n");
            xmlUnlinkNode(delete);
            xmlFreeNode(delete);
            delete = NULL;
        }
        if (IS_XSLT_ELEM(cur)) {
            xsltStylePreCompute(style, cur);

            if (IS_XSLT_NAME(cur, "text")) {
                /*
		* TODO: Processing of xsl:text should be moved to
		*   xsltPreprocessStylesheet(), since otherwise this
		*   will be performed for every multiply included
		*   stylesheet; i.e. this here is not skipped with
		*   the use of the style->nopreproc flag.
		*/
                if (xmlNodeGetChildren(cur) != NULL) {
                    xmlChar *prop;
                    xmlNodePtr text = xmlNodeGetChildren(cur), next;
                    int noesc = 0;

                    prop = xmlGetNsProp(
                        cur, (const xmlChar *) "disable-output-escaping", NULL);
                    if (prop != NULL) {
                        xsltVerbose(XSLT_VB_PARSING, xsltGenericDebugContext,
                                    "Disable escaping: %s\n",
                                    xmlNodeGetContentRaw(text));
                        if (xmlStrEqual(prop, (const xmlChar *) "yes")) {
                            noesc = 1;
                        } else if (!xmlStrEqual(prop, (const xmlChar *) "no")) {
                            xsltTransformError(
                                NULL, style, cur,
                                "xsl:text: disable-output-escaping allows only "
                                "yes or no\n");
                            style->warnings++;
                        }
                        xmlFree(prop);
                    }

                    while (text != NULL) {
                        if (xmlNodeGetType(text) == XML_COMMENT_NODE) {
                            text = xmlNodeGetNext(text);
                            continue;
                        }
                        if (xmlNodeGetType(text) != XML_TEXT_NODE
                            && xmlNodeGetType(text)
                                != XML_CDATA_SECTION_NODE) {
                            xsltTransformError(NULL, style, cur,
                                               "xsltParseTemplateContent: "
                                               "xslt:text content problem\n");
                            style->errors++;
                            break;
                        }
                        if (noesc
                            && xmlNodeGetType(text) != XML_CDATA_SECTION_NODE)
                            xmlNodeSetNameRaw(text, xmlStringTextNoenc);
                        text = xmlNodeGetNext(text);
                    }

                    /*
		     * replace xsl:text by the list of childs
		     */
                    if (text == NULL) {
                        text = xmlNodeGetChildren(cur);
                        while (text != NULL) {
                            if (style->internalized
                                && xmlNodeGetContentRaw(text) != NULL
                                && !xmlDictOwns(style->dict,
                                                 xmlNodeGetContentRaw(text))) {
                                /*
				 * internalize the text string
				 */
                                if (xmlDocGetDict(xmlNodeGetDoc(text))
                                    != NULL) {
                                    const xmlChar *tmp;

                                    tmp = xmlDictLookup(
                                        xmlDocGetDict(xmlNodeGetDoc(text)),
                                        xmlNodeGetContentRaw(text), -1);
                                    if (tmp != xmlNodeGetContentRaw(text)) {
                                        xmlNodeSetContent(text, NULL);
                                        xmlNodeSetContentRaw(text,
                                                             (xmlChar *) tmp);
                                    }
                                }
                            }

                            next = xmlNodeGetNext(text);
                            xmlUnlinkNode(text);
                            if (xmlAddPrevSibling(cur, text) == NULL) {
                                xsltTransformError(NULL, style, NULL,
                                                   "out of memory\n");
                                xmlFreeNode(text);
                            }
                            text = next;
                        }
                    }
                }
                delete = cur;
                goto skip_children;
            }
        } else if (xmlNodeGetNs(cur) != NULL && style->nsDefs != NULL
                   && xsltCheckExtPrefix(style,
                                          xmlNsGetPrefix(xmlNodeGetNs(cur)))) {
            /*
	     * okay this is an extension element compile it too
	     */
            xsltStylePreCompute(style, cur);
        } else if (xmlNodeGetType(cur) == XML_ELEMENT_NODE) {
            /*
	     * This is an element which will be output as part of the
	     * template exectution, precompile AVT if found.
	     */
            if (xmlNodeGetNs(cur) == NULL && style->defaultAlias != NULL) {
                xmlNodeSetNs(cur,
                             xmlSearchNsByHref(xmlNodeGetDoc(cur), cur,
                                               style->defaultAlias));
            }
            if (xmlNodeGetProperties(cur) != NULL) {
                xmlAttrPtr attr = xmlNodeGetProperties(cur);

                while (attr != NULL) {
                    xsltCompileAttr(style, attr);
                    attr = xmlAttrGetNext(attr);
                }
            }
        }
        /*
	 * Skip to next node
	 */
        if (xmlNodeGetChildren(cur) != NULL) {
            if (xmlNodeGetType(xmlNodeGetChildren(cur)) != XML_ENTITY_DECL) {
                cur = xmlNodeGetChildren(cur);
                continue;
            }
        }
    skip_children:
        if (xmlNodeGetNext(cur) != NULL) {
            cur = xmlNodeGetNext(cur);
            continue;
        }

        do {
            cur = xmlNodeGetParent(cur);
            if (cur == NULL)
                break;
            if (cur == templ) {
                cur = NULL;
                break;
            }
            if (xmlNodeGetNext(cur) != NULL) {
                cur = xmlNodeGetNext(cur);
                break;
            }
        } while (cur != NULL);
    }
    if (delete != NULL) {
        xsltVerbose(XSLT_VB_PARSING, xsltGenericDebugContext,
                    "xsltParseTemplateContent: removing text\n");
        xmlUnlinkNode(delete);
        xmlFreeNode(delete);
        delete = NULL;
    }

    /*
     * Skip the first params
     */
    cur = xmlNodeGetChildren(templ);
    while (cur != NULL) {
        if (IS_XSLT_ELEM(cur) && !IS_XSLT_NAME(cur, "param"))
            break;
        cur = xmlNodeGetNext(cur);
    }

    /*
     * Browse the remainder of the template
     */
    while (cur != NULL) {
        if (IS_XSLT_ELEM(cur) && IS_XSLT_NAME(cur, "param")) {
            xmlNodePtr param = cur;

            xsltTransformError(
                NULL, style, cur,
                "xsltParseTemplateContent: ignoring misplaced param element\n");
            if (style != NULL)
                style->warnings++;
            cur = xmlNodeGetNext(cur);
            xmlUnlinkNode(param);
            xmlFreeNode(param);
        } else
            break;
    }
}


/**
 * xsltParseStylesheetKey:
 * @style:  the XSLT stylesheet
 * @key:  the "key" element
 *
 * <!-- Category: top-level-element -->
 * <xsl:key name = qname, match = pattern, use = expression />
 *
 * parse an XSLT stylesheet key definition and register it
 */

static void
xsltParseStylesheetKey (xsltStylesheetPtr style, xmlNodePtr key)
{
    xmlChar *prop = NULL;
    xmlChar *use = NULL;
    xmlChar *match = NULL;
    xmlChar *name = NULL;
    xmlChar *nameURI = NULL;

    if (style == NULL || key == NULL
        || xmlNodeGetType(key) != XML_ELEMENT_NODE)
        return;

    /*
     * Get arguments
     */
    prop = xmlGetNsProp(key, (const xmlChar *) "name", NULL);
    if (prop != NULL) {
        const xmlChar *URI;

        /*
	* TODO: Don't use xsltGetQNameURI().
	*/
        URI = xsltGetQNameURI(key, &prop);
        if (prop == NULL) {
            if (style != NULL)
                style->errors++;
            goto error;
        } else {
            name = prop;
            if (URI != NULL)
                nameURI = xmlStrdup(URI);
        }
        xsltVerbose(XSLT_VB_PARSING, xsltGenericDebugContext,
                    "xsltParseStylesheetKey: name %s\n", name);
    } else {
        xsltTransformError(NULL, style, key, "xsl:key : error missing name\n");
        if (style != NULL)
            style->errors++;
        goto error;
    }

    match = xmlGetNsProp(key, (const xmlChar *) "match", NULL);
    if (match == NULL) {
        xsltTransformError(NULL, style, key, "xsl:key : error missing match\n");
        if (style != NULL)
            style->errors++;
        goto error;
    }

    use = xmlGetNsProp(key, (const xmlChar *) "use", NULL);
    if (use == NULL) {
        xsltTransformError(NULL, style, key, "xsl:key : error missing use\n");
        if (style != NULL)
            style->errors++;
        goto error;
    }

    /*
     * register the keys
     */
    xsltAddKey(style, name, nameURI, match, use, key);

error:
    if (use != NULL)
        xmlFree(use);
    if (match != NULL)
        xmlFree(match);
    if (name != NULL)
        xmlFree(name);
    if (nameURI != NULL)
        xmlFree(nameURI);

    if (xmlNodeGetChildren(key) != NULL) {
        xsltParseContentError(style, xmlNodeGetChildren(key));
    }
}


/**
 * xsltParseStylesheetTemplate:
 * @style:  the XSLT stylesheet
 * @template:  the "template" element
 *
 * parse an XSLT stylesheet template building the associated structures
 */

static void
xsltParseStylesheetTemplate (xsltStylesheetPtr style, xmlNodePtr template)
{
    xsltTemplatePtr ret;
    xmlChar *prop;
    xmlChar *mode = NULL;
    xmlChar *modeURI = NULL;
    double priority;

    if (style == NULL || template == NULL
        || xmlNodeGetType(template) != XML_ELEMENT_NODE)
        return;

    if (style->principal->opLimit > 0) {
        if (style->principal->opCount > style->principal->opLimit) {
            xsltTransformError(NULL, style, NULL,
                               "XSLT parser operation limit exceeded\n");
            style->errors++;
            return;
        }
    }

    /*
     * Create and link the structure
     */
    ret = xsltNewTemplate();
    if (ret == NULL)
        return;
    ret->next = style->templates;
    style->templates = ret;
    ret->style = style;

    /*
     * Get inherited namespaces
     */
    /*
    * TODO: Apply the optimized in-scope-namespace mechanism
    *   as for the other XSLT instructions.
    */
    xsltGetInheritedNsList(style, ret, template);

    /*
     * Get arguments
     */
    prop = xmlGetNsProp(template, (const xmlChar *) "mode", NULL);
    if (prop != NULL) {
        const xmlChar *URI;

        /*
	* TODO: Don't use xsltGetQNameURI().
	*/
        URI = xsltGetQNameURI(template, &prop);
        if (prop == NULL) {
            if (style != NULL)
                style->errors++;
            goto error;
        } else {
            mode = prop;
            if (URI != NULL)
                modeURI = xmlStrdup(URI);
        }
        ret->mode = xmlDictLookup(style->dict, mode, -1);
        ret->modeURI = xmlDictLookup(style->dict, modeURI, -1);
        xsltVerbose(XSLT_VB_PARSING, xsltGenericDebugContext,
                    "xsltParseStylesheetTemplate: mode %s\n", mode);
        if (mode != NULL)
            xmlFree(mode);
        if (modeURI != NULL)
            xmlFree(modeURI);
    }
    prop = xmlGetNsProp(template, (const xmlChar *) "match", NULL);
    if (prop != NULL) {
        if (ret->match != NULL)
            xmlFree(ret->match);
        ret->match = prop;
    }

    prop = xmlGetNsProp(template, (const xmlChar *) "priority", NULL);
    if (prop != NULL) {
        priority = xmlXPathStringEvalNumber(prop);
        ret->priority = (float) priority;
        xmlFree(prop);
    }

    prop = xmlGetNsProp(template, (const xmlChar *) "name", NULL);
    if (prop != NULL) {
        const xmlChar *URI;

        /*
	* TODO: Don't use xsltGetQNameURI().
	*/
        URI = xsltGetQNameURI(template, &prop);
        if (prop == NULL) {
            if (style != NULL)
                style->errors++;
            goto error;
        } else {
            if (xmlValidateNCName(prop, 0)) {
                xsltTransformError(NULL, style, template,
                                   "xsl:template : error invalid name '%s'\n",
                                   prop);
                if (style != NULL)
                    style->errors++;
                xmlFree(prop);
                goto error;
            }
            ret->name = xmlDictLookup(style->dict, BAD_CAST prop, -1);
            xmlFree(prop);
            prop = NULL;
            if (URI != NULL)
                ret->nameURI = xmlDictLookup(style->dict, BAD_CAST URI, -1);
            else
                ret->nameURI = NULL;
        }
    }

    /*
     * parse the content and register the pattern
     */
    xsltParseTemplateContent(style, template);
    ret->elem = template;
    ret->content = xmlNodeGetChildren(template);
    xsltAddTemplate(style, ret, ret->mode, ret->modeURI);

error:
    return;
}



/**
 * xsltParseStylesheetTop:
 * @style:  the XSLT stylesheet
 * @top:  the top level "stylesheet" or "transform" element
 *
 * scan the top level elements of an XSL stylesheet
 */
static void
xsltParseStylesheetTop (xsltStylesheetPtr style, xmlNodePtr top)
{
    xmlNodePtr cur;
    xmlChar *prop;
    int templates = 0;

    if (top == NULL || xmlNodeGetType(top) != XML_ELEMENT_NODE)
        return;

    if (style->principal->opLimit > 0) {
        if (style->principal->opCount > style->principal->opLimit) {
            xsltTransformError(NULL, style, NULL,
                               "XSLT parser operation limit exceeded\n");
            style->errors++;
            return;
        }
    }

    prop = xmlGetNsProp(top, (const xmlChar *) "version", NULL);
    if (prop == NULL) {
        xsltTransformError(
            NULL, style, top,
            "xsl:version is missing: document may not be a stylesheet\n");
        if (style != NULL)
            style->warnings++;
    } else {
        if (!xmlStrEqual(prop, (const xmlChar *) "1.0")
            && !xmlStrEqual(prop, (const xmlChar *) "1.1")) {
            xsltTransformError(
                NULL, style, top,
                "xsl:version: only 1.1 features are supported\n");
            if (style != NULL) {
                style->forwards_compatible = 1;
                style->warnings++;
            }
        }
        xmlFree(prop);
    }

    /*
     * process xsl:import elements
     */
    cur = xmlNodeGetChildren(top);
    while (cur != NULL) {
        style->principal->opCount += 1;

        if (IS_BLANK_NODE(cur)) {
            cur = xmlNodeGetNext(cur);
            continue;
        }
        if (IS_XSLT_ELEM(cur) && IS_XSLT_NAME(cur, "import")) {
            if (xsltParseStylesheetImport(style, cur) != 0)
                if (style != NULL)
                    style->errors++;
        } else
            break;
        cur = xmlNodeGetNext(cur);
    }

    /*
     * process other top-level elements
     */
    while (cur != NULL) {
        style->principal->opCount += 1;

        if (IS_BLANK_NODE(cur)) {
            cur = xmlNodeGetNext(cur);
            continue;
        }
        if (xmlNodeGetType(cur) == XML_TEXT_NODE) {
            if (xmlNodeGetContentRaw(cur) != NULL) {
                xsltTransformError(NULL, style, cur,
                                   "misplaced text node: '%s'\n",
                                   xmlNodeGetContentRaw(cur));
            }
            if (style != NULL)
                style->errors++;
            cur = xmlNodeGetNext(cur);
            continue;
        }
        if (xmlNodeGetType(cur) == XML_ELEMENT_NODE
            && xmlNodeGetNs(cur) == NULL) {
            xsltGenericError(
                xsltGenericErrorContext,
                "Found a top-level element %s with null namespace URI\n",
                xmlNodeGetName(cur));
            if (style != NULL)
                style->errors++;
            cur = xmlNodeGetNext(cur);
            continue;
        }
        if (xmlNodeGetType(cur) == XML_ELEMENT_NODE
            && !(IS_XSLT_ELEM(cur))) {
            xsltTopLevelFunction function;

            function = xsltExtModuleTopLevelLookup(
                xmlNodeGetName(cur), xmlNsGetHref(xmlNodeGetNs(cur)));
            if (function != NULL)
                function(style, cur);

            xsltVerbose(XSLT_VB_PARSING, xsltGenericDebugContext,
                        "xsltParseStylesheetTop : found foreign element %s\n",
                        xmlNodeGetName(cur));
            cur = xmlNodeGetNext(cur);
            continue;
        }
        if (IS_XSLT_NAME(cur, "import")) {
            xsltTransformError(
                NULL, style, cur,
                "xsltParseStylesheetTop: ignoring misplaced import element\n");
            if (style != NULL)
                style->errors++;
        } else if (IS_XSLT_NAME(cur, "include")) {
            if (xsltParseStylesheetInclude(style, cur) != 0)
                if (style != NULL)
                    style->errors++;
        } else if (IS_XSLT_NAME(cur, "strip-space")) {
            xsltParseStylesheetStripSpace(style, cur);
        } else if (IS_XSLT_NAME(cur, "preserve-space")) {
            xsltParseStylesheetPreserveSpace(style, cur);
        } else if (IS_XSLT_NAME(cur, "output")) {
            xsltParseStylesheetOutput(style, cur);
        } else if (IS_XSLT_NAME(cur, "key")) {
            xsltParseStylesheetKey(style, cur);
        } else if (IS_XSLT_NAME(cur, "decimal-format")) {
            xsltParseStylesheetDecimalFormat(style, cur);
        } else if (IS_XSLT_NAME(cur, "attribute-set")) {
            xsltParseStylesheetAttributeSet(style, cur);
        } else if (IS_XSLT_NAME(cur, "variable")) {
            xsltParseGlobalVariable(style, cur);
        } else if (IS_XSLT_NAME(cur, "param")) {
            xsltParseGlobalParam(style, cur);
        } else if (IS_XSLT_NAME(cur, "template")) {
            templates++;
            xsltParseStylesheetTemplate(style, cur);
        } else if (IS_XSLT_NAME(cur, "namespace-alias")) {
            xsltNamespaceAlias(style, cur);
        } else {
            if (style != NULL && style->forwards_compatible == 0) {
                xsltTransformError(
                    NULL, style, cur,
                    "xsltParseStylesheetTop: unknown %s element\n",
                    xmlNodeGetName(cur));
                if (style != NULL)
                    style->errors++;
            }
        }
        cur = xmlNodeGetNext(cur);
    }
    xsltVerbose(XSLT_VB_PARSING, xsltGenericDebugContext,
                "parsed %d templates\n", templates);
}



/**
 * xsltParseStylesheetProcess:
 * @ret:  the XSLT stylesheet (the current stylesheet-level)
 * @doc:  and xmlDoc parsed XML
 *
 * Parses an XSLT stylesheet, adding the associated structures.
 * Called by:
 *  xsltParseStylesheetImportedDoc() (xslt.c)
 *  xsltParseStylesheetInclude() (imports.c)
 *
 * Returns the value of the @style parameter if everything
 * went right, NULL if something went amiss.
 */
xsltStylesheetPtr
xsltParseStylesheetProcess (xsltStylesheetPtr ret, xmlDocPtr doc)
{
    xmlNodePtr cur;

    xsltInitGlobals();

    if (doc == NULL)
        return NULL;
    if (ret == NULL)
        return ret;

    /*
     * First steps, remove blank nodes,
     * locate the xsl:stylesheet element and the
     * namespace declaration.
     */
    cur = xmlDocGetRootElement(doc);
    if (cur == NULL) {
        xsltTransformError(NULL, ret, (xmlNodePtr) doc,
                           "xsltParseStylesheetProcess : empty stylesheet\n");
        return NULL;
    }

    if (IS_XSLT_ELEM(cur)
        && ((IS_XSLT_NAME(cur, "stylesheet"))
            || (IS_XSLT_NAME(cur, "transform")))) {
        xsltVerbose(XSLT_VB_PARSING, xsltGenericDebugContext,
                    "xsltParseStylesheetProcess : found stylesheet\n");
        ret->literal_result = 0;
        xsltParseStylesheetExcludePrefix(ret, cur, 1);
        xsltParseStylesheetExtPrefix(ret, cur, 1);
    } else {
        xsltParseStylesheetExcludePrefix(ret, cur, 0);
        xsltParseStylesheetExtPrefix(ret, cur, 0);
        ret->literal_result = 1;
    }
    if (!ret->nopreproc) {
        xsltPreprocessStylesheet(ret, cur);
    }
    if (ret->literal_result == 0) {
        xsltParseStylesheetTop(ret, cur);
    } else {
        xmlChar *prop;
        xsltTemplatePtr template;

        /*
	 * the document itself might be the template, check xsl:version
	 */
        prop = xmlGetNsProp(cur, (const xmlChar *) "version", XSLT_NAMESPACE);
        if (prop == NULL) {
            xsltTransformError(
                NULL, ret, cur,
                "xsltParseStylesheetProcess : document is not a stylesheet\n");
            return NULL;
        }

        xsltVerbose(XSLT_VB_PARSING, xsltGenericDebugContext,
                    "xsltParseStylesheetProcess : document is stylesheet\n");

        if (!xmlStrEqual(prop, (const xmlChar *) "1.0")
            && !xmlStrEqual(prop, (const xmlChar *) "1.1")) {
            xsltTransformError(
                NULL, ret, cur,
                "xsl:version: only 1.1 features are supported\n");
            ret->forwards_compatible = 1;
            ret->warnings++;
        }
        xmlFree(prop);

        /*
	 * Create and link the template
	 */
        template = xsltNewTemplate();
        if (template == NULL) {
            return NULL;
        }
        template->next = ret->templates;
        ret->templates = template;
        template->match = xmlStrdup((const xmlChar *) "/");

        /*
	 * parse the content and register the pattern
	 */
        xsltParseTemplateContent(ret, (xmlNodePtr) doc);
        template->elem = (xmlNodePtr) doc;
        template->content = xmlDocGetChildren(doc);
        xsltAddTemplate(ret, template, NULL, NULL);
        ret->literal_result = 1;
    }

    return ret;
}


/**
 * xsltParseStylesheetImportedDoc:
 * @doc:  an xmlDoc parsed XML
 * @parentStyle: pointer to the parent stylesheet (if it exists)
 *
 * parse an XSLT stylesheet building the associated structures
 * except the processing not needed for imported documents.
 *
 * Returns a new XSLT stylesheet structure.
 */

xsltStylesheetPtr
xsltParseStylesheetImportedDoc (xmlDocPtr doc, xsltStylesheetPtr parentStyle)
{
    xsltStylesheetPtr retStyle;

    if (doc == NULL)
        return NULL;

    retStyle = xsltNewStylesheetInternal(parentStyle);
    if (retStyle == NULL)
        return NULL;

    if (xsltParseStylesheetUser(retStyle, doc) != 0) {
        xsltFreeStylesheet(retStyle);
        return NULL;
    }

    return retStyle;
}

/**
 * xsltParseStylesheetUser:
 * @style: pointer to the stylesheet
 * @doc:  an xmlDoc parsed XML
 *
 * Parse an XSLT stylesheet with a user-provided stylesheet struct.
 *
 * Returns 0 if successful, -1 in case of error.
 */
int
xsltParseStylesheetUser (xsltStylesheetPtr style, xmlDocPtr doc)
{
    if (style == NULL || doc == NULL)
        return -1;

    /*
    * Adjust the string dict.
    */
    if (xmlDocGetDict(doc) != NULL) {
        xmlDictFree(style->dict);
        style->dict = xmlDocGetDict(doc);
        xsltVerbose(XSLT_VB_GENERAL, xsltGenericDebugContext,
                    "reusing dictionary from %s for stylesheet\n",
                    xmlDocGetURL(doc));
        xmlDictReference(style->dict);
    }

    /*
    * TODO: Eliminate xsltGatherNamespaces(); we must not restrict
    *  the stylesheet to containt distinct namespace prefixes.
    */
    xsltGatherNamespaces(style);

    /*
    * Old behaviour.
    */
    style->doc = doc;
    if (xsltParseStylesheetProcess(style, doc) == NULL) {
        style->doc = NULL;
        return -1;
    }

    if (style->parent == NULL)
        xsltResolveStylesheetAttributeSet(style);

    if (style->errors != 0) {
        /*
        * Detach the doc from the stylesheet; otherwise the doc
        * will be freed in xsltFreeStylesheet().
        */
        style->doc = NULL;
        /*
        * Cleanup the doc if its the main stylesheet.
        */
        if (style->parent == NULL)
            xsltCleanupStylesheetTree(doc, xmlDocGetRootElement(doc));
        return -1;
    }

    return 0;
}

/**
 * xsltParseStylesheetDoc:
 * @doc:  an xmlDoc parsed XML
 *
 * parse an XSLT stylesheet, building the associated structures.  doc
 * is kept as a reference within the returned stylesheet, so changes
 * to doc after the parsing will be reflected when the stylesheet
 * is applied, and the doc is automatically freed when the
 * stylesheet is closed.
 *
 * Returns a new XSLT stylesheet structure.
 */

xsltStylesheetPtr
xsltParseStylesheetDoc (xmlDocPtr doc)
{
    xsltInitGlobals();

    return xsltParseStylesheetImportedDoc(doc, NULL);
}

/**
 * xsltParseStylesheetFile:
 * @filename:  the filename/URL to the stylesheet
 *
 * Load and parse an XSLT stylesheet
 *
 * Returns a new XSLT stylesheet structure.
 */

xsltStylesheetPtr
xsltParseStylesheetFile (const xmlChar *filename)
{
    xsltSecurityPrefsPtr sec;
    xsltStylesheetPtr ret;
    xmlDocPtr doc;

    xsltInitGlobals();

    if (filename == NULL)
        return NULL;

    xsltVerbose(XSLT_VB_PARSING, xsltGenericDebugContext,
                "xsltParseStylesheetFile : parse %s\n", filename);

    /*
     * Security framework check
     */
    sec = xsltGetDefaultSecurityPrefs();
    if (sec != NULL) {
        int res;

        res = xsltCheckRead(sec, NULL, filename);
        if (res <= 0) {
            if (res == 0)
                xsltTransformError(
                    NULL, NULL, NULL,
                    "xsltParseStylesheetFile: read rights for %s denied\n",
                    filename);
            return NULL;
        }
    }

    doc = xsltDocDefaultLoader(filename, NULL, XSLT_PARSE_OPTIONS, NULL,
                               XSLT_LOAD_START);
    if (doc == NULL) {
        xsltTransformError(NULL, NULL, NULL,
                           "xsltParseStylesheetFile : cannot parse %s\n",
                           filename);
        return NULL;
    }
    ret = xsltParseStylesheetDoc(doc);
    if (ret == NULL) {
        xmlFreeDoc(doc);
        return NULL;
    }

    return ret;
}

/************************************************************************
 *									*
 *			Handling of Stylesheet PI			*
 *									*
 ************************************************************************/

#define CUR       (*cur)
#define SKIP(val) cur += (val)
#define NXT(val)  cur[(val)]
#define SKIP_BLANKS       \
    while (IS_BLANK(CUR)) \
    NEXT
#define NEXT ((*cur) ? cur++ : cur)

/**
 * xsltParseStylesheetPI:
 * @value: the value of the PI
 *
 * This function checks that the type is text/xml and extracts
 * the URI-Reference for the stylesheet
 *
 * Returns the URI-Reference for the stylesheet or NULL (it need to
 *         be freed by the caller)
 */
static xmlChar *
xsltParseStylesheetPI (const xmlChar *value)
{
    const xmlChar *cur;
    const xmlChar *start;
    xmlChar *val;
    xmlChar tmp;
    xmlChar *href = NULL;
    int isXml = 0;

    if (value == NULL)
        return NULL;

    cur = value;
    while (CUR != 0) {
        SKIP_BLANKS;
        if (CUR == 't' && NXT(1) == 'y' && NXT(2) == 'p'
            && NXT(3) == 'e') {
            SKIP(4);
            SKIP_BLANKS;
            if (CUR != '=')
                continue;
            NEXT;
            if (CUR != '\'' && CUR != '"')
                continue;
            tmp = CUR;
            NEXT;
            start = cur;
            while (CUR != 0 && CUR != tmp)
                NEXT;
            if (CUR != tmp)
                continue;
            val = xmlStrndup(start, cur - start);
            NEXT;
            if (val == NULL)
                return NULL;
            if (xmlStrcasecmp(val, BAD_CAST "text/xml")
                && xmlStrcasecmp(val, BAD_CAST "text/xsl")
                && xmlStrcasecmp(val, BAD_CAST "application/xslt+xml")) {
                xmlFree(val);
                break;
            }
            isXml = 1;
            xmlFree(val);
        } else if (CUR == 'h' && NXT(1) == 'r' && NXT(2) == 'e'
                   && NXT(3) == 'f') {
            SKIP(4);
            SKIP_BLANKS;
            if (CUR != '=')
                continue;
            NEXT;
            if (CUR != '\'' && CUR != '"')
                continue;
            tmp = CUR;
            NEXT;
            start = cur;
            while (CUR != 0 && CUR != tmp)
                NEXT;
            if (CUR != tmp)
                continue;
            if (href == NULL)
                href = xmlStrndup(start, cur - start);
            NEXT;
        } else {
            while (CUR != 0 && !IS_BLANK(CUR))
                NEXT;
        }
    }

    if (!isXml) {
        if (href != NULL)
            xmlFree(href);
        href = NULL;
    }
    return href;
}

/**
 * xsltLoadStylesheetPI:
 * @doc:  a document to process
 *
 * This function tries to locate the stylesheet PI in the given document
 * If found, and if contained within the document, it will extract
 * that subtree to build the stylesheet to process @doc (doc itself will
 * be modified). If found but referencing an external document it will
 * attempt to load it and generate a stylesheet from it. In both cases,
 * the resulting stylesheet and the document need to be freed once the
 * transformation is done.
 *
 * Returns a new XSLT stylesheet structure or NULL if not found.
 */
xsltStylesheetPtr
xsltLoadStylesheetPI (xmlDocPtr doc)
{
    xmlNodePtr child;
    xsltStylesheetPtr ret = NULL;
    xmlChar *href = NULL;
    xmlURIPtr URI;

    xsltInitGlobals();

    if (doc == NULL)
        return NULL;

    /*
     * Find the text/xml stylesheet PI id any before the root
     */
    child = xmlDocGetChildren(doc);
    while (child != NULL && xmlNodeGetType(child) != XML_ELEMENT_NODE) {
        if (xmlNodeGetType(child) == XML_PI_NODE
            && xmlStrEqual(xmlNodeGetName(child),
                            BAD_CAST "xml-stylesheet")) {
            href = xsltParseStylesheetPI(xmlNodeGetContentRaw(child));
            if (href != NULL)
                break;
        }
        child = xmlNodeGetNext(child);
    }

    /*
     * If found check the href to select processing
     */
    if (href != NULL) {
        xsltVerbose(XSLT_VB_PARSING, xsltGenericDebugContext,
                    "xsltLoadStylesheetPI : found PI href=%s\n", href);
        URI = xmlParseURI((const char *) href);
        if (URI == NULL) {
            xsltTransformError(NULL, NULL, child,
                               "xml-stylesheet : href %s is not valid\n", href);
            xmlFree(href);
            return NULL;
        }
        if (URI->fragment != NULL && URI->scheme == NULL
            && URI->opaque == NULL && URI->authority == NULL
            && URI->server == NULL && URI->user == NULL
            && URI->path == NULL && URI->query == NULL) {
            xmlAttrPtr ID;

            xsltVerbose(XSLT_VB_PARSING, xsltGenericDebugContext,
                        "xsltLoadStylesheetPI : Reference to ID %s\n",
                        href);
            if (URI->fragment[0] == '#')
                ID = xmlGetID(doc, (const xmlChar *) &(URI->fragment[1]));
            else
                ID = xmlGetID(doc, (const xmlChar *) URI->fragment);
            if (ID == NULL) {
                xsltTransformError(NULL, NULL, child,
                                   "xml-stylesheet : no ID %s found\n",
                                   URI->fragment);
            } else {
                xmlDocPtr fake;
                xmlNodePtr subtree, newtree;
                xmlNsPtr ns;

                xsltVerbose(XSLT_VB_GENERAL, xsltGenericDebugContext,
                            "creating new document from %s for embedded stylesheet\n",
                            xmlDocGetURL(doc));
                /*
		 * move the subtree in a new document passed to
		 * the stylesheet analyzer
		 */
                subtree = xmlAttrGetParent(ID);
                fake = xmlNewDoc(NULL);
                if (fake != NULL) {
                    /*
		    * Should the dictionary still be shared even though
		    * the nodes are being copied rather than moved?
		    */
                    xmlDocSetDict(fake, xmlDocGetDict(doc));
                    xmlDictReference(xmlDocGetDict(doc));
                    xsltVerbose(XSLT_VB_GENERAL, xsltGenericDebugContext,
                                "reusing dictionary from %s for embedded stylesheet\n",
                                xmlDocGetURL(doc));

                    newtree = xmlDocCopyNode(subtree, fake, 1);

                    xmlDocSetURL(
                        fake, xmlNodeGetBase(doc, xmlNodeGetParent(subtree)));
                    xsltVerbose(XSLT_VB_GENERAL, xsltGenericDebugContext,
                                "set base URI for embedded stylesheet as %s\n",
                                xmlDocGetURL(fake));

                    /*
		    * Add all namespaces in scope of embedded stylesheet to
		    * root element of newly created stylesheet document
		    */
                    while ((subtree = xmlNodeGetParent(subtree))
                           != (xmlNodePtr) doc) {
                        for (ns = xmlNodeGetNs(subtree); ns;
                             ns = xmlNsGetNext(ns)) {
                            xmlNewNs(newtree, xmlNsGetHref(ns),
                                     xmlNsGetPrefix(ns));
                        }
                    }

                    xmlAddChild((xmlNodePtr) fake, newtree);
                    ret = xsltParseStylesheetDoc(fake);
                    if (ret == NULL)
                        xmlFreeDoc(fake);
                }
            }
        } else {
            xmlChar *URL, *base;

            /*
	     * Reference to an external stylesheet
	     */

            base = xmlNodeGetBase(doc, (xmlNodePtr) doc);
            URL = xmlBuildURI(href, base);
            if (URL != NULL) {
                xsltVerbose(XSLT_VB_PARSING, xsltGenericDebugContext,
                            "xsltLoadStylesheetPI : fetching %s\n", URL);
                ret = xsltParseStylesheetFile(URL);
                xmlFree(URL);
            } else {
                xsltVerbose(XSLT_VB_PARSING, xsltGenericDebugContext,
                            "xsltLoadStylesheetPI : fetching %s\n", href);
                ret = xsltParseStylesheetFile(href);
            }
            if (base != NULL)
                xmlFree(base);
        }
        xmlFreeURI(URI);
        xmlFree(href);
    }
    return ret;
}
