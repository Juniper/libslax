/*
 * xsltutils.c: Utilities for the XSL Transformation 1.0 engine
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

#ifndef XSLT_NEED_TRIO
#include <stdio.h>
#else
#include <trio.h>
#endif

#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <libxml/xmlmemory.h>
#include <libxml/tree.h>
#include <libxml/HTMLtree.h>
#include <libxml/xmlerror.h>
#include <libxml/xmlIO.h>
#include <libpsu/psutime.h>
#include "xsltutils.h"
#include "templates.h"
#include "xsltInternals.h"
#include "imports.h"
#include "transform.h"

#if defined(_WIN32)
#include <windows.h>
#define XSLT_WIN32_PERFORMANCE_COUNTER
#endif

/************************************************************************
 *									*
 *			Convenience function				*
 *									*
 ************************************************************************/

/**
 * xsltGetCNsProp:
 * @style: the stylesheet
 * @node:  the node
 * @name:  the attribute name
 * @nameSpace:  the URI of the namespace
 *
 * Similar to xmlGetNsProp() but with a slightly different semantic
 *
 * Search and get the value of an attribute associated to a node
 * This attribute has to be anchored in the namespace specified,
 * or has no namespace and the element is in that namespace.
 *
 * This does the entity substitution.
 * This function looks in DTD attribute declaration for #FIXED or
 * default declaration values unless DTD use has been turned off.
 *
 * Returns the attribute value or NULL if not found. The string is allocated
 *         in the stylesheet dictionary.
 */
const xmlChar *
xsltGetCNsProp (xsltStylesheetPtr style, xmlNodePtr node, const xmlChar *name,
                const xmlChar *nameSpace)
{
    xmlAttrPtr prop;
    xmlDocPtr doc;
    xmlNsPtr ns;
    xmlChar *tmp;
    const xmlChar *ret;

    if (node == NULL || style == NULL || style->dict == NULL)
        return NULL;

    if (nameSpace == NULL)
        return xmlGetProp(node, name);

    if (xmlNodeGetType(node) == XML_NAMESPACE_DECL)
        return NULL;
    if (xmlNodeGetType(node) == XML_ELEMENT_NODE)
        prop = xmlNodeGetProperties(node);
    else
        prop = NULL;
    while (prop != NULL) {
        /*
	 * One need to have
	 *   - same attribute names
	 *   - and the attribute carrying that namespace
	 */
        if (xmlStrEqual(xmlAttrGetName(prop), name)
            && (((xmlAttrGetNs(prop) == NULL) && (xmlNodeGetNs(node) != NULL)
                 && (xmlStrEqual(xmlNsGetHref(xmlNodeGetNs(node)), nameSpace)))
                || ((xmlAttrGetNs(prop) != NULL)
                    && (xmlStrEqual(xmlNsGetHref(xmlAttrGetNs(prop)),
                                    nameSpace))))) {
            tmp = xmlNodeListGetString(xmlNodeGetDoc(node),
                                       xmlAttrGetChildren(prop), 1);
            if (tmp == NULL)
                ret = xmlDictLookup(style->dict, BAD_CAST "", 0);
            else {
                ret = xmlDictLookup(style->dict, tmp, -1);
                xmlFree(tmp);
            }
            return ret;
        }
        prop = xmlAttrGetNext(prop);
    }
    tmp = NULL;
    /*
     * Check if there is a default declaration in the internal
     * or external subsets
     */
    doc = xmlNodeGetDoc(node);
    if (doc != NULL) {
        if (xmlDocGetIntSubset(doc) != NULL) {
            xmlAttributePtr attrDecl;

            attrDecl = xmlGetDtdAttrDesc(xmlDocGetIntSubset(doc),
                                         xmlNodeGetName(node), name);
            if (attrDecl == NULL && xmlDocGetExtSubset(doc) != NULL)
                attrDecl = xmlGetDtdAttrDesc(xmlDocGetExtSubset(doc),
                                             xmlNodeGetName(node), name);

            if (attrDecl != NULL && attrDecl->prefix != NULL) {
                /*
		 * The DTD declaration only allows a prefix search
		 */
                ns = xmlSearchNs(doc, node, attrDecl->prefix);
                if (ns != NULL && xmlStrEqual(xmlNsGetHref(ns), nameSpace))
                    return xmlDictLookup(style->dict, attrDecl->defaultValue,
                                         -1);
            }
        }
    }
    return NULL;
}
/**
 * xsltGetNsProp:
 * @node:  the node
 * @name:  the attribute name
 * @nameSpace:  the URI of the namespace
 *
 * Similar to xmlGetNsProp() but with a slightly different semantic
 *
 * Search and get the value of an attribute associated to a node
 * This attribute has to be anchored in the namespace specified,
 * or has no namespace and the element is in that namespace.
 *
 * This does the entity substitution.
 * This function looks in DTD attribute declaration for #FIXED or
 * default declaration values unless DTD use has been turned off.
 *
 * Returns the attribute value or NULL if not found.
 *     It's up to the caller to free the memory.
 */
xmlChar *
xsltGetNsProp (xmlNodePtr node, const xmlChar *name, const xmlChar *nameSpace)
{
    xmlAttrPtr prop;
    xmlDocPtr doc;
    xmlNsPtr ns;

    if (node == NULL)
        return NULL;

    if (nameSpace == NULL)
        return xmlGetProp(node, name);

    if (xmlNodeGetType(node) == XML_NAMESPACE_DECL)
        return NULL;
    if (xmlNodeGetType(node) == XML_ELEMENT_NODE)
        prop = xmlNodeGetProperties(node);
    else
        prop = NULL;
    /*
    * TODO: Substitute xmlGetProp() for xmlGetNsProp(), since the former
    * is not namespace-aware and will return an attribute with equal
    * name regardless of its namespace.
    * Example:
    *   <xsl:element foo:name="myName"/>
    *   So this would return "myName" even if an attribute @name
    *   in the XSLT was requested.
    */
    while (prop != NULL) {
        /*
	 * One need to have
	 *   - same attribute names
	 *   - and the attribute carrying that namespace
	 */
        if (xmlStrEqual(xmlAttrGetName(prop), name)
            && (((xmlAttrGetNs(prop) == NULL) && (xmlNodeGetNs(node) != NULL)
                 && (xmlStrEqual(xmlNsGetHref(xmlNodeGetNs(node)), nameSpace)))
                || ((xmlAttrGetNs(prop) != NULL)
                    && (xmlStrEqual(xmlNsGetHref(xmlAttrGetNs(prop)),
                                    nameSpace))))) {
            xmlChar *ret;

            ret = xmlNodeListGetString(xmlNodeGetDoc(node),
                                       xmlAttrGetChildren(prop), 1);
            if (ret == NULL)
                return xmlStrdup((xmlChar *) "");
            return ret;
        }
        prop = xmlAttrGetNext(prop);
    }

    /*
     * Check if there is a default declaration in the internal
     * or external subsets
     */
    doc = xmlNodeGetDoc(node);
    if (doc != NULL) {
        if (xmlDocGetIntSubset(doc) != NULL) {
            xmlAttributePtr attrDecl;

            attrDecl = xmlGetDtdAttrDesc(xmlDocGetIntSubset(doc),
                                         xmlNodeGetName(node), name);
            if (attrDecl == NULL && xmlDocGetExtSubset(doc) != NULL)
                attrDecl = xmlGetDtdAttrDesc(xmlDocGetExtSubset(doc),
                                             xmlNodeGetName(node), name);

            if (attrDecl != NULL && attrDecl->prefix != NULL) {
                /*
		 * The DTD declaration only allows a prefix search
		 */
                ns = xmlSearchNs(doc, node, attrDecl->prefix);
                if (ns != NULL && xmlStrEqual(xmlNsGetHref(ns), nameSpace))
                    return xmlStrdup(attrDecl->defaultValue);
            }
        }
    }
    return NULL;
}

/**
 * xsltGetUTF8Char:
 * @utf:  a sequence of UTF-8 encoded bytes
 * @len:  a pointer to @bytes len
 *
 * Read one UTF8 Char from @utf
 * Function copied from libxml2 xmlGetUTF8Char() ... to discard ultimately
 * and use the original API
 *
 * Returns the char value or -1 in case of error and update @len with the
 *        number of bytes used
 */
int
xsltGetUTF8Char (const unsigned char *utf, int *len)
{
    unsigned int c;

    if (utf == NULL)
        goto error;
    if (len == NULL)
        goto error;
    if (*len < 1)
        goto error;

    c = utf[0];
    if (c & 0x80) {
        if (*len < 2)
            goto error;
        if ((utf[1] & 0xc0) != 0x80)
            goto error;
        if ((c & 0xe0) == 0xe0) {
            if (*len < 3)
                goto error;
            if ((utf[2] & 0xc0) != 0x80)
                goto error;
            if ((c & 0xf0) == 0xf0) {
                if (*len < 4)
                    goto error;
                if ((c & 0xf8) != 0xf0 || (utf[3] & 0xc0) != 0x80)
                    goto error;
                *len = 4;
                /* 4-byte code */
                c = (utf[0] & 0x7) << 18;
                c |= (utf[1] & 0x3f) << 12;
                c |= (utf[2] & 0x3f) << 6;
                c |= utf[3] & 0x3f;
            } else {
                /* 3-byte code */
                *len = 3;
                c = (utf[0] & 0xf) << 12;
                c |= (utf[1] & 0x3f) << 6;
                c |= utf[2] & 0x3f;
            }
        } else {
            /* 2-byte code */
            *len = 2;
            c = (utf[0] & 0x1f) << 6;
            c |= utf[1] & 0x3f;
        }
    } else {
        /* 1-byte code */
        *len = 1;
    }
    return c;

error:
    if (len != NULL)
        *len = 0;
    return -1;
}

/**
 * xsltGetUTF8CharZ:
 * @utf:  a sequence of UTF-8 encoded bytes
 * @len:  a pointer to @bytes len
 *
 * Read one UTF8 Char from a null-terminated string.
 *
 * Returns the char value or -1 in case of error and update @len with the
 *        number of bytes used
 */
int
xsltGetUTF8CharZ (const unsigned char *utf, int *len)
{
    unsigned int c;

    if (utf == NULL)
        goto error;
    if (len == NULL)
        goto error;

    c = utf[0];
    if (c & 0x80) {
        if ((utf[1] & 0xc0) != 0x80)
            goto error;
        if ((c & 0xe0) == 0xe0) {
            if ((utf[2] & 0xc0) != 0x80)
                goto error;
            if ((c & 0xf0) == 0xf0) {
                if ((c & 0xf8) != 0xf0 || (utf[3] & 0xc0) != 0x80)
                    goto error;
                *len = 4;
                /* 4-byte code */
                c = (utf[0] & 0x7) << 18;
                c |= (utf[1] & 0x3f) << 12;
                c |= (utf[2] & 0x3f) << 6;
                c |= utf[3] & 0x3f;
            } else {
                /* 3-byte code */
                *len = 3;
                c = (utf[0] & 0xf) << 12;
                c |= (utf[1] & 0x3f) << 6;
                c |= utf[2] & 0x3f;
            }
        } else {
            /* 2-byte code */
            *len = 2;
            c = (utf[0] & 0x1f) << 6;
            c |= utf[1] & 0x3f;
        }
    } else {
        /* 1-byte code */
        *len = 1;
    }
    return c;

error:
    if (len != NULL)
        *len = 0;
    return -1;
}


/************************************************************************
 *									*
 *		Handling of XSLT stylesheets messages			*
 *									*
 ************************************************************************/

/**
 * xsltMessage:
 * @ctxt:  an XSLT processing context
 * @node:  The current node
 * @inst:  The node containing the message instruction
 *
 * Process and xsl:message construct
 */
void
xsltMessage (xsltTransformContextPtr ctxt, xmlNodePtr node, xmlNodePtr inst)
{
    xmlGenericErrorFunc error = xsltGenericError;
    void *errctx = xsltGenericErrorContext;
    xmlChar *prop, *message;
    int terminate = 0;

    if (ctxt == NULL || inst == NULL)
        return;

    if (ctxt->error != NULL) {
        error = ctxt->error;
        errctx = ctxt->errctx;
    }

    prop = xmlGetNsProp(inst, (const xmlChar *) "terminate", NULL);
    if (prop != NULL) {
        if (xmlStrEqual(prop, (const xmlChar *) "yes")) {
            terminate = 1;
        } else if (xmlStrEqual(prop, (const xmlChar *) "no")) {
            terminate = 0;
        } else {
            xsltTransformError(
                ctxt, NULL, inst,
                "xsl:message : terminate expecting 'yes' or 'no'\n");
        }
        xmlFree(prop);
    }
    message = xsltEvalTemplateString(ctxt, node, inst);
    if (message != NULL) {
        int len = xmlStrlen(message);

        error(errctx, "%s", (const char *) message);
        if (len > 0 && message[len - 1] != '\n')
            error(errctx, "\n");
        xmlFree(message);
    }
    if (terminate)
        ctxt->state = XSLT_STATE_STOPPED;
}

/**
 * xsltStopEngine:
 * @ctxt:  an XSLT transformation context
 *
 * Stop the XSLT transformation engine in the middle of a script, the
 * same way <xsl:message terminate="yes"/> does above.  There's no
 * other direct way to halt an in-progress transform.
 */
void
xsltStopEngine (xsltTransformContextPtr ctxt)
{
    if (ctxt != NULL)
        ctxt->state = XSLT_STATE_STOPPED;
}

/**
 * xsltGetMaxDepth:
 *
 * Returns the current value of xsltMaxDepth, the maximum call depth
 * allowed before the transformation engine aborts with a "potential
 * infinite template recursion" error.
 *
 * Returns the current max call depth.
 */
int
xsltGetMaxDepth (void)
{
    return xsltMaxDepth;
}

/**
 * xsltSetMaxDepth:
 * @value:  the new maximum call depth
 *
 * Sets xsltMaxDepth, the maximum call depth allowed before the
 * transformation engine aborts with a "potential infinite template
 * recursion" error.
 */
void
xsltSetMaxDepth (int value)
{
    xsltMaxDepth = value;
}

/************************************************************************
 *									*
 *		Handling of out of context errors			*
 *									*
 ************************************************************************/

#define XSLT_GET_VAR_STR(msg, str)                                   \
    {                                                                \
        int size;                                                    \
        int chars;                                                   \
        char *larger;                                                \
        va_list ap;                                                  \
                                                                     \
        str = (char *) xmlMalloc(150);                               \
        if (str == NULL)                                             \
            return;                                                  \
                                                                     \
        size = 150;                                                  \
                                                                     \
        while (size < 64000) {                                       \
            va_start(ap, msg);                                       \
            chars = vsnprintf(str, size, msg, ap);                   \
            va_end(ap);                                              \
            if ((chars > -1) && (chars < size))                      \
                break;                                               \
            if (chars > -1)                                          \
                size += chars + 1;                                   \
            else                                                     \
                size += 100;                                         \
            if ((larger = (char *) xmlRealloc(str, size)) == NULL) { \
                xmlFree(str);                                        \
                return;                                              \
            }                                                        \
            str = larger;                                            \
        }                                                            \
    }
/**
 * xsltGenericErrorDefaultFunc:
 * @ctx:  an error context
 * @msg:  the message to display/transmit
 * @...:  extra parameters for the message display
 *
 * Default handler for out of context error messages.
 */
static void PSU_PRINTFLIKE(2, 3)
    xsltGenericErrorDefaultFunc(void *ctx UNUSED, const char *msg, ...)
{
    va_list args;

    if (xsltGenericErrorContext == NULL)
        xsltGenericErrorContext = (void *) stderr;

    va_start(args, msg);
    vfprintf((FILE *) xsltGenericErrorContext, msg, args);
    va_end(args);
}

xmlGenericErrorFunc xsltGenericError = xsltGenericErrorDefaultFunc;
void *xsltGenericErrorContext = NULL;

/**
 * xsltSetGenericErrorFunc:
 * @ctx:  the new error handling context
 * @handler:  the new handler function
 *
 * Function to reset the handler and the error context for out of
 * context error messages.
 * This simply means that @handler will be called for subsequent
 * error messages while not parsing nor validating. And @ctx will
 * be passed as first argument to @handler
 * One can simply force messages to be emitted to another FILE * than
 * stderr by setting @ctx to this file handle and @handler to NULL.
 */
void
xsltSetGenericErrorFunc (void *ctx, xmlGenericErrorFunc handler)
{
    xsltGenericErrorContext = ctx;
    if (handler != NULL)
        xsltGenericError = handler;
    else
        xsltGenericError = xsltGenericErrorDefaultFunc;
}

/**
 * xsltGenericDebugDefaultFunc:
 * @ctx:  an error context
 * @msg:  the message to display/transmit
 * @...:  extra parameters for the message display
 *
 * Default handler for out of context error messages.
 */
static void PSU_PRINTFLIKE(2, 3)
    xsltGenericDebugDefaultFunc(void *ctx UNUSED, const char *msg, ...)
{
    va_list args;

    if (xsltGenericDebugContext == NULL)
        return;

    va_start(args, msg);
    vfprintf((FILE *) xsltGenericDebugContext, msg, args);
    va_end(args);
}

xmlGenericErrorFunc xsltGenericDebug = xsltGenericDebugDefaultFunc;
void *xsltGenericDebugContext = NULL;

/**
 * xsltSetGenericDebugFunc:
 * @ctx:  the new error handling context
 * @handler:  the new handler function
 *
 * Function to reset the handler and the error context for out of
 * context error messages.
 * This simply means that @handler will be called for subsequent
 * error messages while not parsing or validating. And @ctx will
 * be passed as first argument to @handler
 * One can simply force messages to be emitted to another FILE * than
 * stderr by setting @ctx to this file handle and @handler to NULL.
 */
void
xsltSetGenericDebugFunc (void *ctx, xmlGenericErrorFunc handler)
{
    xsltGenericDebugContext = ctx;
    if (handler != NULL)
        xsltGenericDebug = handler;
    else
        xsltGenericDebug = xsltGenericDebugDefaultFunc;
}

/**
 * xsltGenericDebugV:
 * @ctx:  an error context
 * @fmt:  the message format
 * @ap:   the already-started argument list
 *
 * va_list counterpart to xsltGenericDebug(), for callers (xsltVerbose(),
 * xsltverbose.h) that only have a va_list, not "...", to forward.  The
 * message is formatted here and passed on to the installed
 * xsltGenericDebug handler as a single "%s" argument, so that handler
 * (default or user-installed via xsltSetGenericDebugFunc()) still gets
 * to decide what to do with it, exactly as if the caller had invoked
 * xsltGenericDebug() directly.
 */
void
xsltGenericDebugV (void *ctx, const char *fmt, va_list ap)
{
    char buf[2048];

    vsnprintf(buf, sizeof(buf), fmt, ap);
    xsltGenericDebug(ctx, "%s", buf);
}

/**
 * xsltPrintErrorContext:
 * @ctxt:  the transformation context
 * @style:  the stylesheet
 * @node:  the current node being processed
 *
 * Display the context of an error.
 */
void
xsltPrintErrorContext (xsltTransformContextPtr ctxt, xsltStylesheetPtr style,
                       xmlNodePtr node)
{
    int line = 0;
    const xmlChar *file = NULL;
    const xmlChar *name = NULL;
    const char *type = "error";
    xmlGenericErrorFunc error = xsltGenericError;
    void *errctx = xsltGenericErrorContext;

    if (ctxt != NULL) {
        if (ctxt->state == XSLT_STATE_OK)
            ctxt->state = XSLT_STATE_ERROR;
        if (ctxt->error != NULL) {
            error = ctxt->error;
            errctx = ctxt->errctx;
        }
    }
    if (node == NULL && ctxt != NULL)
        node = ctxt->inst;

    if (node != NULL) {
        if (xmlNodeGetType(node) == XML_DOCUMENT_NODE
            || xmlNodeGetType(node) == XML_HTML_DOCUMENT_NODE) {
            xmlDocPtr doc = (xmlDocPtr) node;

            file = xmlDocGetURL(doc);
        } else {
            line = xmlGetLineNo(node);
            if (xmlNodeGetDoc(node) != NULL
                && xmlDocGetURL(xmlNodeGetDoc(node)) != NULL)
                file = xmlDocGetURL(xmlNodeGetDoc(node));
            if (xmlNodeGetName(node) != NULL)
                name = xmlNodeGetName(node);
        }
    }

    if (ctxt != NULL)
        type = "runtime error";
    else if (style != NULL) {
        type = "compilation error";
    }

    if (file != NULL && line != 0 && name != NULL)
        error(errctx, "%s: file %s line %d element %s\n", type, file, line,
              name);
    else if (file != NULL && name != NULL)
        error(errctx, "%s: file %s element %s\n", type, file, name);
    else if (file != NULL && line != 0)
        error(errctx, "%s: file %s line %d\n", type, file, line);
    else if (file != NULL)
        error(errctx, "%s: file %s\n", type, file);
    else if (name != NULL)
        error(errctx, "%s: element %s\n", type, name);
    else
        error(errctx, "%s\n", type);
}

/**
 * xsltSetTransformErrorFunc:
 * @ctxt:  the XSLT transformation context
 * @ctx:  the new error handling context
 * @handler:  the new handler function
 *
 * Function to reset the handler and the error context for out of
 * context error messages specific to a given XSLT transromation.
 *
 * This simply means that @handler will be called for subsequent
 * error messages while running the transformation.
 */
void
xsltSetTransformErrorFunc (xsltTransformContextPtr ctxt, void *ctx,
                           xmlGenericErrorFunc handler)
{
    ctxt->error = handler;
    ctxt->errctx = ctx;
}

/**
 * xsltTransformError:
 * @ctxt:  an XSLT transformation context
 * @style:  the XSLT stylesheet used
 * @node:  the current node in the stylesheet
 * @msg:  the message to display/transmit
 * @...:  extra parameters for the message display
 *
 * Display and format an error messages, gives file, line, position and
 * extra parameters, will use the specific transformation context if available
 */
void
xsltTransformError (xsltTransformContextPtr ctxt, xsltStylesheetPtr style,
                    xmlNodePtr node, const char *msg, ...)
{
    xmlGenericErrorFunc error = xsltGenericError;
    void *errctx = xsltGenericErrorContext;
    char *str;

    if (ctxt != NULL) {
        if (ctxt->state == XSLT_STATE_OK)
            ctxt->state = XSLT_STATE_ERROR;
        if (ctxt->error != NULL) {
            error = ctxt->error;
            errctx = ctxt->errctx;
        }
    }
    if (node == NULL && ctxt != NULL)
        node = ctxt->inst;
    xsltPrintErrorContext(ctxt, style, node);
    XSLT_GET_VAR_STR(msg, str);
    error(errctx, "%s", str);
    if (str != NULL)
        xmlFree(str);
}

/************************************************************************
 *									*
 *				QNames					*
 *									*
 ************************************************************************/

/**
 * xsltSplitQName:
 * @dict: a dictionary
 * @name:  the full QName
 * @prefix: the return value
 *
 * Split QNames into prefix and local names, both allocated from a dictionary.
 *
 * Returns: the localname or NULL in case of error.
 */
const xmlChar *
xsltSplitQName (xmlDictPtr dict, const xmlChar *name, const xmlChar **prefix)
{
    int len = 0;
    const xmlChar *ret = NULL;

    *prefix = NULL;
    if (name == NULL || dict == NULL)
        return NULL;
    if (name[0] == ':')
        return xmlDictLookup(dict, name, -1);
    while (name[len] != 0 && name[len] != ':')
        len++;
    if (name[len] == 0)
        return xmlDictLookup(dict, name, -1);
    *prefix = xmlDictLookup(dict, name, len);
    ret = xmlDictLookup(dict, &name[len + 1], -1);
    return ret;
}

/**
 * xsltGetQNameURI:
 * @node:  the node holding the QName
 * @name:  pointer to the initial QName value
 *
 * This function analyzes @name, if the name contains a prefix,
 * the function seaches the associated namespace in scope for it.
 * It will also replace @name value with the NCName, the old value being
 * freed.
 * Errors in the prefix lookup are signalled by setting @name to NULL.
 *
 * NOTE: the namespace returned is a pointer to the place where it is
 *       defined and hence has the same lifespan as the document holding it.
 *
 * Returns the namespace URI if there is a prefix, or NULL if @name is
 *         not prefixed.
 */
const xmlChar *
xsltGetQNameURI (xmlNodePtr node, xmlChar **name)
{
    int len = 0;
    xmlChar *qname;
    xmlNsPtr ns;

    if (name == NULL)
        return NULL;
    qname = *name;
    if (qname == NULL || *qname == 0)
        return NULL;
    if (node == NULL) {
        xsltGenericError(xsltGenericErrorContext,
                         "QName: no element for namespace lookup %s\n", qname);
        xmlFree(qname);
        *name = NULL;
        return NULL;
    }

    /* nasty but valid */
    if (qname[0] == ':')
        return NULL;

    /*
     * we are not trying to validate but just to cut, and yes it will
     * work even if this is a set of UTF-8 encoded chars
     */
    while (qname[len] != 0 && qname[len] != ':')
        len++;

    if (qname[len] == 0)
        return NULL;

    /*
     * handle xml: separately, this one is magical
     */
    if (qname[0] == 'x' && qname[1] == 'm' && qname[2] == 'l'
        && qname[3] == ':') {
        if (qname[4] == 0)
            return NULL;
        *name = xmlStrdup(&qname[4]);
        xmlFree(qname);
        return XML_XML_NAMESPACE;
    }

    qname[len] = 0;
    ns = xmlSearchNs(xmlNodeGetDoc(node), node, qname);
    if (ns == NULL) {
        xsltGenericError(xsltGenericErrorContext,
                         "%s:%s : no namespace bound to prefix %s\n", qname,
                         &qname[len + 1], qname);
        *name = NULL;
        xmlFree(qname);
        return NULL;
    }
    *name = xmlStrdup(&qname[len + 1]);
    xmlFree(qname);
    return xmlNsGetHref(ns);
}

/**
 * xsltGetQNameURI2:
 * @style:  stylesheet pointer
 * @node:   the node holding the QName
 * @name:   pointer to the initial QName value
 *
 * This function is similar to xsltGetQNameURI, but is used when
 * @name is a dictionary entry.
 *
 * Returns the namespace URI if there is a prefix, or NULL if @name is
 * not prefixed.
 */
const xmlChar *
xsltGetQNameURI2 (xsltStylesheetPtr style, xmlNodePtr node,
                  const xmlChar **name)
{
    int len = 0;
    xmlChar *qname;
    xmlNsPtr ns;

    if (name == NULL)
        return NULL;
    qname = (xmlChar *) *name;
    if (qname == NULL || *qname == 0)
        return NULL;
    if (node == NULL) {
        xsltGenericError(xsltGenericErrorContext,
                         "QName: no element for namespace lookup %s\n", qname);
        *name = NULL;
        return NULL;
    }

    /*
     * we are not trying to validate but just to cut, and yes it will
     * work even if this is a set of UTF-8 encoded chars
     */
    while (qname[len] != 0 && qname[len] != ':')
        len++;

    if (qname[len] == 0)
        return NULL;

    /*
     * handle xml: separately, this one is magical
     */
    if (qname[0] == 'x' && qname[1] == 'm' && qname[2] == 'l'
        && qname[3] == ':') {
        if (qname[4] == 0)
            return NULL;
        *name = xmlDictLookup(style->dict, &qname[4], -1);
        return XML_XML_NAMESPACE;
    }

    qname = xmlStrndup(*name, len);
    ns = xmlSearchNs(xmlNodeGetDoc(node), node, qname);
    if (ns == NULL) {
        if (style) {
            xsltTransformError(NULL, style, node,
                               "No namespace bound to prefix '%s'.\n", qname);
            style->errors++;
        } else {
            xsltGenericError(xsltGenericErrorContext,
                             "%s : no namespace bound to prefix %s\n", *name,
                             qname);
        }
        *name = NULL;
        xmlFree(qname);
        return NULL;
    }
    *name = xmlDictLookup(style->dict, (*name) + len + 1, -1);
    xmlFree(qname);
    return xmlNsGetHref(ns);
}

/************************************************************************
 *									*
 *				Sorting					*
 *									*
 ************************************************************************/

/**
 * xsltDocumentSortFunction:
 * @list:  the node set
 *
 * reorder the current node list @list accordingly to the document order
 * This function is slow, obsolete and should not be used anymore.
 */
void
xsltDocumentSortFunction (xmlNodeSetPtr list)
{
    int i, j;
    int len, tst;
    xmlNodePtr node;

    if (list == NULL)
        return;
    len = xmlNodeSetGetNodeNr(list);
    if (len <= 1)
        return;
    /* TODO: sort is really not optimized, does it needs to ? */
    for (i = 0; i < len - 1; i++) {
        for (j = i + 1; j < len; j++) {
            tst = xmlXPathCmpNodes(xmlNodeSetGetNodeEntry(list, i),
                                   xmlNodeSetGetNodeEntry(list, j));
            if (tst == -1) {
                node = xmlNodeSetGetNodeEntry(list, i);
                xmlNodeSetSetNodeEntry(list, i,
                                       xmlNodeSetGetNodeEntry(list, j));
                xmlNodeSetSetNodeEntry(list, j, node);
            }
        }
    }
}

/**
 * xsltComputeSortResultInternal:
 * @ctxt:  a XSLT process context
 * @sort:  xsl:sort node
 * @number:  data-type is number
 * @locale:  transform strings according to locale
 *
 * reorder the current node list accordingly to the set of sorting
 * requirement provided by the array of nodes.
 *
 * Returns a ordered XPath nodeset or NULL in case of error.
 */
static xmlXPathObjectPtr *
xsltComputeSortResultInternal (xsltTransformContextPtr ctxt, xmlNodePtr sort,
                               int number, void *locale)
{
    const xsltStylePreComp *comp;
    xmlXPathObjectPtr *results = NULL;
    xmlNodeSetPtr list = NULL;
    xmlXPathObjectPtr res;
    int len = 0;
    int i;
    xmlNodePtr oldNode;
    xmlNodePtr oldInst;
    int oldPos, oldSize;
    int oldNsNr;
    xmlNsPtr *oldNamespaces;

    comp = xmlNodeGetPsvi(sort);
    if (comp == NULL) {
        xsltGenericError(xsltGenericErrorContext,
                         "xsl:sort : compilation failed\n");
        return NULL;
    }

    if (comp->select == NULL || comp->comp == NULL)
        return NULL;

    list = ctxt->nodeList;
    if (list == NULL || xmlNodeSetGetNodeNr(list) <= 1)
        return NULL;

    len = xmlNodeSetGetNodeNr(list);

    /* TODO: xsl:sort lang attribute */
    /* TODO: xsl:sort case-order attribute */

    results = xmlMalloc(len * sizeof(xmlXPathObjectPtr));
    if (results == NULL) {
        xsltGenericError(xsltGenericErrorContext,
                         "xsltComputeSortResult: memory allocation failure\n");
        return NULL;
    }

    oldInst = ctxt->inst;
    oldNode = xmlXPathContextGetNode(ctxt->xpathCtxt);
    oldPos = xmlXPathContextGetProximityPosition(ctxt->xpathCtxt);
    oldSize = xmlXPathContextGetContextSize(ctxt->xpathCtxt);
    oldNsNr = xmlXPathContextGetNsNr(ctxt->xpathCtxt);
    oldNamespaces = xmlXPathContextGetNamespaces(ctxt->xpathCtxt);
    for (i = 0; i < len; i++) {
        ctxt->inst = sort;
        xmlXPathContextSetContextSize(ctxt->xpathCtxt, len);
        xmlXPathContextSetProximityPosition(ctxt->xpathCtxt, i + 1);
        ctxt->node = xmlNodeSetGetNodeEntry(list, i);
        xmlXPathContextSetNode(ctxt->xpathCtxt, ctxt->node);
        xmlXPathContextSetNamespaces(ctxt->xpathCtxt, comp->nsList);
        xmlXPathContextSetNsNr(ctxt->xpathCtxt, comp->nsNr);
        xmlResetLastError();
        res = xmlXPathCompiledEval(comp->comp, ctxt->xpathCtxt);
        if (res != NULL) {
            if (xmlXPathObjectGetType(res) != XPATH_STRING)
                res = xmlXPathConvertString(res);
            if (number)
                res = xmlXPathConvertNumber(res);
        }
        if (res != NULL) {
            xmlXPathObjectSetIndex(res,
                                   i); /* Save original pos for dupl resolv */
            if (number) {
                if (xmlXPathObjectGetType(res) == XPATH_NUMBER) {
                    results[i] = res;
                } else {
#ifdef WITH_XSLT_DEBUG_PROCESS
                    xsltGenericDebug(xsltGenericDebugContext,
                                     "xsltComputeSortResult: select didn't "
                                     "evaluate to a number\n");
#endif
                    results[i] = NULL;
                }
            } else {
                if (xmlXPathObjectGetType(res) == XPATH_STRING) {
                    if (locale != NULL) {
                        xmlChar *str = xmlXPathObjectGetStringval(res);
                        xmlChar *sortKey = ctxt->genSortKey(locale, str);

                        if (sortKey == NULL) {
                            xsltTransformError(
                                ctxt, NULL, sort,
                                "xsltComputeSortResult: sort key is null\n");
                        } else {
                            xmlXPathObjectSetStringval(res, sortKey);
                            xmlFree(str);
                        }
                    }

                    results[i] = res;
                } else {
#ifdef WITH_XSLT_DEBUG_PROCESS
                    xsltGenericDebug(xsltGenericDebugContext,
                                     "xsltComputeSortResult: select didn't "
                                     "evaluate to a string\n");
#endif
                    results[i] = NULL;
                }
            }
        } else {
            int handled = 0;
            const char *note = xsltLastXPathErrorMessage();

#ifdef WITH_XSLT_DEBUGGER
            handled = xslHandleDebuggerError(
                sort, ctxt, "expression for 'xsl:sort' failed to "
                "evaluate", note);
#endif /* WITH_XSLT_DEBUGGER */

            if (!handled) {
                if (note != NULL)
                    xsltTransformError(ctxt, NULL, sort,
                                       "Failed to evaluate the expression "
                                       "of 'xsl:sort'.\nnote: %s\n", note);
                else
                    xsltTransformError(ctxt, NULL, sort,
                                       "Failed to evaluate the expression "
                                       "of 'xsl:sort'.\n");
            }

            ctxt->state = XSLT_STATE_STOPPED;
            results[i] = NULL;
        }
    }
    ctxt->inst = oldInst;
    xmlXPathContextSetNode(ctxt->xpathCtxt, oldNode);
    xmlXPathContextSetContextSize(ctxt->xpathCtxt, oldSize);
    xmlXPathContextSetProximityPosition(ctxt->xpathCtxt, oldPos);
    xmlXPathContextSetNsNr(ctxt->xpathCtxt, oldNsNr);
    xmlXPathContextSetNamespaces(ctxt->xpathCtxt, oldNamespaces);

    return results;
}

/**
 * xsltComputeSortResult:
 * @ctxt:  a XSLT process context
 * @sort:  node list
 *
 * reorder the current node list accordingly to the set of sorting
 * requirement provided by the array of nodes.
 *
 * Returns a ordered XPath nodeset or NULL in case of error.
 */
xmlXPathObjectPtr *
xsltComputeSortResult (xsltTransformContextPtr ctxt, xmlNodePtr sort)
{
    const xsltStylePreComp *comp = xmlNodeGetPsvi(sort);
    int number = 0;

    if (comp != NULL)
        number = comp->number;
    return xsltComputeSortResultInternal(ctxt, sort, number,
                                         /* locale */ NULL);
}

/**
 * xsltDefaultSortFunction:
 * @ctxt:  a XSLT process context
 * @sorts:  array of sort nodes
 * @nbsorts:  the number of sorts in the array
 *
 * reorder the current node list accordingly to the set of sorting
 * requirement provided by the arry of nodes.
 */
void
xsltDefaultSortFunction (xsltTransformContextPtr ctxt, xmlNodePtr *sorts,
                         int nbsorts)
{
    const xsltStylePreComp *comp;
    xmlXPathObjectPtr *resultsTab[XSLT_MAX_SORT];
    xmlXPathObjectPtr *results = NULL, *res;
    xmlNodeSetPtr list = NULL;
    int len = 0;
    int i, j, incr;
    int tst;
    int depth;
    xmlNodePtr node;
    xmlXPathObjectPtr tmp;
    int number[XSLT_MAX_SORT], desc[XSLT_MAX_SORT];
    void *locale[XSLT_MAX_SORT];

    if (ctxt == NULL || sorts == NULL || nbsorts <= 0
        || nbsorts >= XSLT_MAX_SORT)
        return;
    if (sorts[0] == NULL)
        return;
    comp = xmlNodeGetPsvi(sorts[0]);
    if (comp == NULL)
        return;

    list = ctxt->nodeList;
    if (list == NULL || xmlNodeSetGetNodeNr(list) <= 1)
        return; /* nothing to do */

    for (j = 0; j < nbsorts; j++) {
        xmlChar *lang;

        comp = xmlNodeGetPsvi(sorts[j]);
        if (comp->stype == NULL && comp->has_stype != 0) {
            xmlChar *stype = xsltEvalAttrValueTemplate(
                ctxt, sorts[j], BAD_CAST "data-type", NULL);
            number[j] = 0;
            if (stype != NULL) {
                if (xmlStrEqual(stype, (const xmlChar *) "text"))
                    ;
                else if (xmlStrEqual(stype, (const xmlChar *) "number"))
                    number[j] = 1;
                else {
                    xsltTransformError(
                        ctxt, NULL, sorts[j],
                        "xsltDoSortFunction: no support for data-type = %s\n",
                        stype);
                }
                xmlFree(stype);
            }
        } else {
            number[j] = comp->number;
        }
        if (comp->order == NULL && comp->has_order != 0) {
            xmlChar *order = xsltEvalAttrValueTemplate(ctxt, sorts[j],
                                                       BAD_CAST "order", NULL);
            desc[j] = 0;
            if (order != NULL) {
                if (xmlStrEqual(order, (const xmlChar *) "ascending"))
                    ;
                else if (xmlStrEqual(order, (const xmlChar *) "descending"))
                    desc[j] = 1;
                else {
                    xsltTransformError(
                        ctxt, NULL, sorts[j],
                        "xsltDoSortFunction: invalid value %s for order\n",
                        order);
                }
                xmlFree(order);
            }
        } else {
            desc[j] = comp->descending;
        }
        if (comp->lang == NULL && comp->has_lang != 0) {
            lang = xsltEvalAttrValueTemplate(ctxt, sorts[j], (xmlChar *) "lang",
                                             NULL);
        } else {
            lang = (xmlChar *) comp->lang;
        }
        if (lang != NULL) {
            locale[j] = ctxt->newLocale(lang, comp->lower_first);
            if (lang != comp->lang)
                xmlFree(lang);
        } else {
            locale[j] = NULL;
        }
    }

    len = xmlNodeSetGetNodeNr(list);

    resultsTab[0]
        = xsltComputeSortResultInternal(ctxt, sorts[0], number[0], locale[0]);
    for (i = 1; i < XSLT_MAX_SORT; i++)
        resultsTab[i] = NULL;

    results = resultsTab[0];

    comp = xmlNodeGetPsvi(sorts[0]);
    if (results == NULL)
        goto cleanup;

    /* Shell's sort of node-set */
    for (incr = len / 2; incr > 0; incr /= 2) {
        for (i = incr; i < len; i++) {
            j = i - incr;
            if (results[i] == NULL)
                continue;

            while (j >= 0) {
                if (results[j] == NULL)
                    tst = 1;
                else {
                    if (number[0]) {
                        /* We make NaN smaller than number in accordance
			   with XSLT spec */
                        if (xmlXPathIsNaN(
                                xmlXPathObjectGetFloatval(results[j]))) {
                            if (xmlXPathIsNaN(xmlXPathObjectGetFloatval(
                                    results[j + incr])))
                                tst = 0;
                            else
                                tst = -1;
                        } else if (xmlXPathIsNaN(xmlXPathObjectGetFloatval(
                                       results[j + incr])))
                            tst = 1;
                        else if (xmlXPathObjectGetFloatval(results[j])
                                 == xmlXPathObjectGetFloatval(
                                     results[j + incr]))
                            tst = 0;
                        else if (xmlXPathObjectGetFloatval(results[j])
                                 > xmlXPathObjectGetFloatval(results[j + incr]))
                            tst = 1;
                        else
                            tst = -1;
                    } else {
                        tst = xmlStrcmp(
                            xmlXPathObjectGetStringval(results[j]),
                            xmlXPathObjectGetStringval(results[j + incr]));
                    }
                    if (desc[0])
                        tst = -tst;
                }
                if (tst == 0) {
                    /*
		     * Okay we need to use multi level sorts
		     */
                    depth = 1;
                    while (depth < nbsorts) {
                        if (sorts[depth] == NULL)
                            break;
                        comp = xmlNodeGetPsvi(sorts[depth]);
                        if (comp == NULL)
                            break;

                        /*
			 * Compute the result of the next level for the
			 * full set, this might be optimized ... or not
			 */
                        if (resultsTab[depth] == NULL)
                            resultsTab[depth] = xsltComputeSortResultInternal(
                                ctxt, sorts[depth], number[depth],
                                locale[depth]);
                        res = resultsTab[depth];
                        if (res == NULL)
                            break;
                        if (res[j] == NULL) {
                            if (res[j + incr] != NULL)
                                tst = 1;
                        } else if (res[j + incr] == NULL) {
                            tst = -1;
                        } else {
                            if (number[depth]) {
                                /* We make NaN smaller than number in
				   accordance with XSLT spec */
                                if (xmlXPathIsNaN(
                                        xmlXPathObjectGetFloatval(res[j]))) {
                                    if (xmlXPathIsNaN(xmlXPathObjectGetFloatval(
                                            res[j + incr])))
                                        tst = 0;
                                    else
                                        tst = -1;
                                } else if (xmlXPathIsNaN(
                                               xmlXPathObjectGetFloatval(
                                                   res[j + incr])))
                                    tst = 1;
                                else if (xmlXPathObjectGetFloatval(res[j])
                                         == xmlXPathObjectGetFloatval(
                                             res[j + incr]))
                                    tst = 0;
                                else if (xmlXPathObjectGetFloatval(res[j])
                                         > xmlXPathObjectGetFloatval(
                                             res[j + incr]))
                                    tst = 1;
                                else
                                    tst = -1;
                            } else {
                                tst = xmlStrcmp(
                                    xmlXPathObjectGetStringval(res[j]),
                                    xmlXPathObjectGetStringval(res[j + incr]));
                            }
                            if (desc[depth])
                                tst = -tst;
                        }

                        /*
			 * if we still can't differenciate at this level
			 * try one level deeper.
			 */
                        if (tst != 0)
                            break;
                        depth++;
                    }
                }
                if (tst == 0) {
                    tst = xmlXPathObjectGetIndex(results[j])
                        > xmlXPathObjectGetIndex(results[j + incr]);
                }
                if (tst > 0) {
                    tmp = results[j];
                    results[j] = results[j + incr];
                    results[j + incr] = tmp;
                    node = xmlNodeSetGetNodeEntry(list, j);
                    xmlNodeSetSetNodeEntry(
                        list, j, xmlNodeSetGetNodeEntry(list, j + incr));
                    xmlNodeSetSetNodeEntry(list, j + incr, node);
                    depth = 1;
                    while (depth < nbsorts) {
                        if (sorts[depth] == NULL)
                            break;
                        if (resultsTab[depth] == NULL)
                            break;
                        res = resultsTab[depth];
                        tmp = res[j];
                        res[j] = res[j + incr];
                        res[j + incr] = tmp;
                        depth++;
                    }
                    j -= incr;
                } else
                    break;
            }
        }
    }

cleanup:
    for (j = 0; j < nbsorts; j++) {
        if (locale[j] != NULL) {
            ctxt->freeLocale(locale[j]);
        }
        if (resultsTab[j] != NULL) {
            for (i = 0; i < len; i++)
                xmlXPathFreeObject(resultsTab[j][i]);
            xmlFree(resultsTab[j]);
        }
    }
}

static xsltSortFunc xsltSortFunction = xsltDefaultSortFunction;

/**
 * xsltDoSortFunction:
 * @ctxt:  a XSLT process context
 * @sorts:  array of sort nodes
 * @nbsorts:  the number of sorts in the array
 *
 * reorder the current node list accordingly to the set of sorting
 * requirement provided by the arry of nodes.
 * This is a wrapper function, the actual function used is specified
 * using xsltSetCtxtSortFunc() to set the context specific sort function,
 * or xsltSetSortFunc() to set the global sort function.
 * If a sort function is set on the context, this will get called.
 * Otherwise the global sort function is called.
 */
void
xsltDoSortFunction (xsltTransformContextPtr ctxt, xmlNodePtr *sorts,
                    int nbsorts)
{
    if (ctxt->sortfunc != NULL)
        (ctxt->sortfunc)(ctxt, sorts, nbsorts);
    else if (xsltSortFunction != NULL)
        xsltSortFunction(ctxt, sorts, nbsorts);
}

/**
 * xsltSetSortFunc:
 * @handler:  the new handler function
 *
 * DEPRECATED: Use xsltSetCtxtLocaleHandlers.
 *
 * Function to reset the global handler for XSLT sorting.
 * If the handler is NULL, the default sort function will be used.
 */
void
xsltSetSortFunc (xsltSortFunc handler)
{
    if (handler != NULL)
        xsltSortFunction = handler;
    else
        xsltSortFunction = xsltDefaultSortFunction;
}

/**
 * xsltSetCtxtSortFunc:
 * @ctxt:  a XSLT process context
 * @handler:  the new handler function
 *
 * DEPRECATED: Use xsltSetCtxtLocaleHandlers.
 *
 * Function to set the handler for XSLT sorting
 * for the specified context.
 * If the handler is NULL, then the global
 * sort function will be called
 */
void
xsltSetCtxtSortFunc (xsltTransformContextPtr ctxt, xsltSortFunc handler)
{
    ctxt->sortfunc = handler;
}

/**
 * xsltSetCtxtLocaleHandlers:
 * @ctxt:  an XSLT transform context
 * @newLocale:  locale constructor
 * @freeLocale:  locale destructor
 * @genSortKey:  sort key generator
 *
 * Set the locale handlers.
 */
void
xsltSetCtxtLocaleHandlers (xsltTransformContextPtr ctxt,
                           xsltNewLocaleFunc newLocale,
                           xsltFreeLocaleFunc freeLocale,
                           xsltGenSortKeyFunc genSortKey)
{
    if (ctxt == NULL)
        return;

    ctxt->newLocale = newLocale;
    ctxt->freeLocale = freeLocale;
    ctxt->genSortKey = genSortKey;
}

/************************************************************************
 *									*
 *				Parsing options				*
 *									*
 ************************************************************************/

/**
 * xsltSetCtxtParseOptions:
 * @ctxt:  a XSLT process context
 * @options:  a combination of libxml2 xmlParserOption
 *
 * Change the default parser option passed by the XSLT engine to the
 * parser when using document() loading.
 *
 * Returns the previous options or -1 in case of error
 */
int
xsltSetCtxtParseOptions (xsltTransformContextPtr ctxt, int options)
{
    int oldopts;

    if (ctxt == NULL)
        return -1;
    oldopts = ctxt->parserOptions;
    if (ctxt->xinclude)
        oldopts |= XML_PARSE_XINCLUDE;
    ctxt->parserOptions = options;
    if (options & XML_PARSE_XINCLUDE)
        ctxt->xinclude = 1;
    else
        ctxt->xinclude = 0;
    return oldopts;
}

/************************************************************************
 *									*
 *				Output					*
 *									*
 ************************************************************************/

/**
 * xsltSaveResultTo:
 * @buf:  an output buffer
 * @result:  the result xmlDocPtr
 * @style:  the stylesheet
 *
 * Save the result @result obtained by applying the @style stylesheet
 * to an I/O output channel @buf
 *
 * Returns the number of byte written or -1 in case of failure.
 */
int
xsltSaveResultTo (xmlOutputBufferPtr buf, xmlDocPtr result,
                  xsltStylesheetPtr style)
{
    const xmlChar *encoding;
    int base;
    const xmlChar *method;
    int indent;

    if (buf == NULL || result == NULL || style == NULL)
        return -1;
    if (xmlDocGetChildren(result) == NULL
        || xmlNodeGetType(xmlDocGetChildren(result)) == XML_DTD_NODE
            && xmlNodeGetNext(xmlDocGetChildren(result)) == NULL)
        return 0;

    if (style->methodURI != NULL
        && ((style->method == NULL)
            || (!xmlStrEqual(style->method, (const xmlChar *) "xhtml")))) {
        xsltGenericError(xsltGenericErrorContext,
                         "xsltSaveResultTo : unknown output method\n");
        return -1;
    }

    base = buf->written;

    XSLT_GET_IMPORT_PTR(method, style, method)
    XSLT_GET_IMPORT_PTR(encoding, style, encoding)
    XSLT_GET_IMPORT_INT(indent, style, indent);

    if (method == NULL && xmlDocGetType(result) == XML_HTML_DOCUMENT_NODE)
        method = (const xmlChar *) "html";

    if (method != NULL && xmlStrEqual(method, (const xmlChar *) "html")) {
        if (encoding != NULL) {
            htmlSetMetaEncoding(result, (const xmlChar *) encoding);
        } else {
            htmlSetMetaEncoding(result, (const xmlChar *) "UTF-8");
        }
        if (indent == -1)
            indent = 1;
        htmlDocContentDumpFormatOutput(buf, result, (const char *) encoding,
                                       indent);
        xmlOutputBufferFlush(buf);
    } else if (method != NULL
               && xmlStrEqual(method, (const xmlChar *) "xhtml")) {
        if (encoding != NULL) {
            htmlSetMetaEncoding(result, (const xmlChar *) encoding);
        } else {
            htmlSetMetaEncoding(result, (const xmlChar *) "UTF-8");
        }
        htmlDocContentDumpOutput(buf, result, (const char *) encoding);
        xmlOutputBufferFlush(buf);
    } else if (method != NULL
               && xmlStrEqual(method, (const xmlChar *) "text")) {
        xmlNodePtr cur;

        cur = xmlDocGetChildren(result);
        while (cur != NULL) {
            if (xmlNodeGetType(cur) == XML_TEXT_NODE)
                xmlOutputBufferWriteString(
                    buf, (const char *) xmlNodeGetContentRaw(cur));

            /*
	     * Skip to next node
	     */
            if (xmlNodeGetChildren(cur) != NULL) {
                if (xmlNodeGetType(xmlNodeGetChildren(cur)) != XML_ENTITY_DECL
                    && xmlNodeGetType(xmlNodeGetChildren(cur))
                        != XML_ENTITY_REF_NODE
                    && xmlNodeGetType(xmlNodeGetChildren(cur))
                        != XML_ENTITY_NODE) {
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
        xmlOutputBufferFlush(buf);
    } else {
        int omitXmlDecl;
        int standalone;

        XSLT_GET_IMPORT_INT(omitXmlDecl, style, omitXmlDeclaration);
        XSLT_GET_IMPORT_INT(standalone, style, standalone);

        if (omitXmlDecl != 1) {
            xmlOutputBufferWriteString(buf, "<?xml version=");
            if (xmlDocGetVersion(result) != NULL) {
                xmlOutputBufferWriteString(buf, "\"");
                xmlOutputBufferWriteString(
                    buf, (const char *) xmlDocGetVersion(result));
                xmlOutputBufferWriteString(buf, "\"");
            } else
                xmlOutputBufferWriteString(buf, "\"1.0\"");
            if (encoding == NULL) {
                if (xmlDocGetEncoding(result) != NULL)
                    encoding = xmlDocGetEncoding(result);
                else if (xmlDocGetCharset(result) != XML_CHAR_ENCODING_UTF8)
                    encoding = (const xmlChar *) xmlGetCharEncodingName(
                        (xmlCharEncoding) xmlDocGetCharset(result));
            }
            if (encoding != NULL) {
                xmlOutputBufferWriteString(buf, " encoding=");
                xmlOutputBufferWriteString(buf, "\"");
                xmlOutputBufferWriteString(buf, (const char *) encoding);
                xmlOutputBufferWriteString(buf, "\"");
            }
            switch (standalone) {
            case 0:
                xmlOutputBufferWriteString(buf, " standalone=\"no\"");
                break;
            case 1:
                xmlOutputBufferWriteString(buf, " standalone=\"yes\"");
                break;
            default:
                break;
            }
            xmlOutputBufferWriteString(buf, "?>\n");
        }
        if (xmlDocGetChildren(result) != NULL) {
            xmlNodePtr children = xmlDocGetChildren(result);
            xmlNodePtr child = children;

            /*
             * Hack to avoid quadratic behavior when scanning
             * result->children in xmlGetIntSubset called by
             * xmlNodeDumpOutput.
             */
            xmlDocSetChildren(result, NULL);

            while (child != NULL) {
                xmlNodeDumpOutput(buf, result, child, 0, indent == 1,
                                  (const char *) encoding);
                if (indent
                    && ((xmlNodeGetType(child) == XML_DTD_NODE)
                        || ((xmlNodeGetType(child) == XML_COMMENT_NODE)
                            && (xmlNodeGetNext(child) != NULL))))
                    xmlOutputBufferWriteString(buf, "\n");
                child = xmlNodeGetNext(child);
            }
            if (indent)
                xmlOutputBufferWriteString(buf, "\n");

            xmlDocSetChildren(result, children);
        }
        xmlOutputBufferFlush(buf);
    }
    return buf->written - base;
}

/**
 * xsltSaveResultToFilename:
 * @URL:  a filename or URL
 * @result:  the result xmlDocPtr
 * @style:  the stylesheet
 * @compression:  the compression factor (0 - 9 included)
 *
 * Save the result @result obtained by applying the @style stylesheet
 * to a file or @URL
 *
 * Returns the number of byte written or -1 in case of failure.
 */
int
xsltSaveResultToFilename (const char *URL, xmlDocPtr result,
                          xsltStylesheetPtr style, int compression)
{
    xmlOutputBufferPtr buf;
    const xmlChar *encoding;
    int ret;

    if (URL == NULL || result == NULL || style == NULL)
        return -1;
    if (xmlDocGetChildren(result) == NULL)
        return 0;

    XSLT_GET_IMPORT_PTR(encoding, style, encoding)
    if (encoding != NULL) {
        xmlCharEncodingHandlerPtr encoder = NULL;

        /* Don't use UTF-8 dummy encoder */
        if (xmlStrcasecmp(encoding, BAD_CAST "UTF-8") != 0
            && xmlStrcasecmp(encoding, BAD_CAST "UTF8") != 0)
            encoder = xmlFindCharEncodingHandler((char *) encoding);
        buf = xmlOutputBufferCreateFilename(URL, encoder, compression);
    } else {
        buf = xmlOutputBufferCreateFilename(URL, NULL, compression);
    }
    if (buf == NULL)
        return -1;
    xsltSaveResultTo(buf, result, style);
    ret = xmlOutputBufferClose(buf);
    return ret;
}

/**
 * xsltSaveResultToFile:
 * @file:  a FILE * I/O
 * @result:  the result xmlDocPtr
 * @style:  the stylesheet
 *
 * Save the result @result obtained by applying the @style stylesheet
 * to an open FILE * I/O.
 * This does not close the FILE @file
 *
 * Returns the number of bytes written or -1 in case of failure.
 */
int
xsltSaveResultToFile (FILE *file, xmlDocPtr result, xsltStylesheetPtr style)
{
    xmlOutputBufferPtr buf;
    const xmlChar *encoding;
    int ret;

    if (file == NULL || result == NULL || style == NULL)
        return -1;
    if (xmlDocGetChildren(result) == NULL)
        return 0;

    XSLT_GET_IMPORT_PTR(encoding, style, encoding)
    if (encoding != NULL) {
        xmlCharEncodingHandlerPtr encoder = NULL;

        /* Don't use UTF-8 dummy encoder */
        if (xmlStrcasecmp(encoding, BAD_CAST "UTF-8") != 0
            && xmlStrcasecmp(encoding, BAD_CAST "UTF8") != 0)
            encoder = xmlFindCharEncodingHandler((char *) encoding);
        buf = xmlOutputBufferCreateFile(file, encoder);
    } else {
        buf = xmlOutputBufferCreateFile(file, NULL);
    }

    if (buf == NULL)
        return -1;
    xsltSaveResultTo(buf, result, style);
    ret = xmlOutputBufferClose(buf);
    return ret;
}

/**
 * xsltSaveResultToFd:
 * @fd:  a file descriptor
 * @result:  the result xmlDocPtr
 * @style:  the stylesheet
 *
 * Save the result @result obtained by applying the @style stylesheet
 * to an open file descriptor
 * This does not close the descriptor.
 *
 * Returns the number of bytes written or -1 in case of failure.
 */
int
xsltSaveResultToFd (int fd, xmlDocPtr result, xsltStylesheetPtr style)
{
    xmlOutputBufferPtr buf;
    const xmlChar *encoding;
    int ret;

    if (fd < 0 || result == NULL || style == NULL)
        return -1;
    if (xmlDocGetChildren(result) == NULL)
        return 0;

    XSLT_GET_IMPORT_PTR(encoding, style, encoding)
    if (encoding != NULL) {
        xmlCharEncodingHandlerPtr encoder = NULL;

        /* Don't use UTF-8 dummy encoder */
        if (xmlStrcasecmp(encoding, BAD_CAST "UTF-8") != 0
            && xmlStrcasecmp(encoding, BAD_CAST "UTF8") != 0)
            encoder = xmlFindCharEncodingHandler((char *) encoding);
        buf = xmlOutputBufferCreateFd(fd, encoder);
    } else {
        buf = xmlOutputBufferCreateFd(fd, NULL);
    }
    if (buf == NULL)
        return -1;
    xsltSaveResultTo(buf, result, style);
    ret = xmlOutputBufferClose(buf);
    return ret;
}

/**
 * xsltSaveResultToString:
 * @doc_txt_ptr:  Memory pointer for allocated XML text
 * @doc_txt_len:  Length of the generated XML text
 * @result:  the result xmlDocPtr
 * @style:  the stylesheet
 *
 * Save the result @result obtained by applying the @style stylesheet
 * to a new allocated string.
 *
 * Returns 0 in case of success and -1 in case of error
 */
int
xsltSaveResultToString (xmlChar **doc_txt_ptr, int *doc_txt_len,
                        xmlDocPtr result, xsltStylesheetPtr style)
{
    xmlOutputBufferPtr buf;
    const xmlChar *encoding;

    *doc_txt_ptr = NULL;
    *doc_txt_len = 0;
    if (xmlDocGetChildren(result) == NULL)
        return 0;

    XSLT_GET_IMPORT_PTR(encoding, style, encoding)
    if (encoding != NULL) {
        xmlCharEncodingHandlerPtr encoder = NULL;

        /* Don't use UTF-8 dummy encoder */
        if (xmlStrcasecmp(encoding, BAD_CAST "UTF-8") != 0
            && xmlStrcasecmp(encoding, BAD_CAST "UTF8") != 0)
            encoder = xmlFindCharEncodingHandler((char *) encoding);
        buf = xmlAllocOutputBuffer(encoder);
    } else {
        buf = xmlAllocOutputBuffer(NULL);
    }
    if (buf == NULL)
        return -1;
    xsltSaveResultTo(buf, result, style);
#ifdef LIBXML2_NEW_BUFFER
    if (buf->conv != NULL) {
        *doc_txt_len = xmlBufUse(buf->conv);
        *doc_txt_ptr = xmlStrndup(xmlBufContent(buf->conv), *doc_txt_len);
    } else {
        *doc_txt_len = xmlBufUse(buf->buffer);
        *doc_txt_ptr = xmlStrndup(xmlBufContent(buf->buffer), *doc_txt_len);
    }
#else
    if (buf->conv != NULL) {
        *doc_txt_len = buf->conv->use;
        *doc_txt_ptr = xmlStrndup(buf->conv->content, *doc_txt_len);
    } else {
        *doc_txt_len = buf->buffer->use;
        *doc_txt_ptr = xmlStrndup(buf->buffer->content, *doc_txt_len);
    }
#endif
    (void) xmlOutputBufferClose(buf);
    return 0;
}

/**
 * xsltGetSourceNodeFlags:
 * @node:  Node from source document
 *
 * Returns the flags for a source node.
 */
int
xsltGetSourceNodeFlags (xmlNodePtr node)
{
    /*
     * Squeeze the bit flags into the upper bits of
     *
     * - 'int properties' member in struct _xmlDoc
     * - 'xmlAttributeType atype' member in struct _xmlAttr
     * - 'unsigned short extra' member in struct _xmlNode
     */
    switch (xmlNodeGetType(node)) {
    case XML_DOCUMENT_NODE:
    case XML_HTML_DOCUMENT_NODE:
        return xmlDocGetProperties((xmlDocPtr) node) >> 27;

    case XML_ATTRIBUTE_NODE:
        return xmlAttrGetAtype((xmlAttrPtr) node) >> 27;

    case XML_ELEMENT_NODE:
    case XML_TEXT_NODE:
    case XML_CDATA_SECTION_NODE:
    case XML_PI_NODE:
    case XML_COMMENT_NODE:
        return xmlNodeGetExtra(node) >> 12;

    default:
        return 0;
    }
}

/**
 * xsltSetSourceNodeFlags:
 * @node:  Node from source document
 * @flags:  Flags
 *
 * Sets the specified flags to 1.
 *
 * Returns 0 on success, -1 on error.
 */
int
xsltSetSourceNodeFlags (xsltTransformContextPtr ctxt, xmlNodePtr node,
                        int flags)
{
    if (xmlNodeGetDoc(node) == ctxt->initialContextDoc)
        ctxt->sourceDocDirty = 1;

    switch (xmlNodeGetType(node)) {
    case XML_DOCUMENT_NODE:
    case XML_HTML_DOCUMENT_NODE:
        xmlDocSetProperties((xmlDocPtr) node,
                            xmlDocGetProperties((xmlDocPtr) node)
                                | (flags << 27));
        return 0;

    case XML_ATTRIBUTE_NODE:
        xmlAttrSetAtype((xmlAttrPtr) node,
                        xmlAttrGetAtype((xmlAttrPtr) node) | (flags << 27));
        return 0;

    case XML_ELEMENT_NODE:
    case XML_TEXT_NODE:
    case XML_CDATA_SECTION_NODE:
    case XML_PI_NODE:
    case XML_COMMENT_NODE:
        xmlNodeSetExtra(node, xmlNodeGetExtra(node) | (flags << 12));
        return 0;

    default:
        return -1;
    }
}

/**
 * xsltClearSourceNodeFlags:
 * @node:  Node from source document
 * @flags:  Flags
 *
 * Sets the specified flags to 0.
 *
 * Returns 0 on success, -1 on error.
 */
int
xsltClearSourceNodeFlags (xmlNodePtr node, int flags)
{
    switch (xmlNodeGetType(node)) {
    case XML_DOCUMENT_NODE:
    case XML_HTML_DOCUMENT_NODE:
        xmlDocSetProperties((xmlDocPtr) node,
                            xmlDocGetProperties((xmlDocPtr) node)
                                & ~(flags << 27));
        return 0;

    case XML_ATTRIBUTE_NODE:
        xmlAttrSetAtype((xmlAttrPtr) node,
                        xmlAttrGetAtype((xmlAttrPtr) node) & ~(flags << 27));
        return 0;

    case XML_ELEMENT_NODE:
    case XML_TEXT_NODE:
    case XML_CDATA_SECTION_NODE:
    case XML_PI_NODE:
    case XML_COMMENT_NODE:
        xmlNodeSetExtra(node, xmlNodeGetExtra(node) & ~(flags << 12));
        return 0;

    default:
        return -1;
    }
}

/**
 * xsltGetPSVIPtr:
 * @cur:  Node
 *
 * Returns a pointer to the psvi member of a node or NULL on error.
 */
void **
xsltGetPSVIPtr (xmlNodePtr cur)
{
    switch (xmlNodeGetType(cur)) {
    case XML_DOCUMENT_NODE:
    case XML_HTML_DOCUMENT_NODE:
        return &((xmlDocPtr) cur)->psvi;

    case XML_ATTRIBUTE_NODE:
        return &((xmlAttrPtr) cur)->psvi;

    case XML_ELEMENT_NODE:
    case XML_TEXT_NODE:
    case XML_CDATA_SECTION_NODE:
    case XML_PI_NODE:
    case XML_COMMENT_NODE:
        return &cur->psvi;

    default:
        return NULL;
    }
}

#ifdef WITH_XSLT_PROFILER

/************************************************************************
 *									*
 *		Generating profiling information			*
 *									*
 ************************************************************************/

static long calibration = -1;

/**
 * xsltCalibrateTimestamps:
 *
 * Used for to calibrate the xsltTimestamp() function
 * Should work if launched at startup and we don't loose our quantum :-)
 *
 * Returns the number of milliseconds used by xsltTimestamp()
 */
#if !defined(XSLT_WIN32_PERFORMANCE_COUNTER) \
    && (defined(HAVE_CLOCK_GETTIME) || defined(HAVE_GETTIMEOFDAY))
static long
xsltCalibrateTimestamps (void)
{
    long result;

    PSU_CALIBRATE_OVERHEAD(xsltTimestamp, result);

    return result;
}
#endif

/**
 * xsltCalibrateAdjust:
 * @delta:  a negative dealy value found
 *
 * Used for to correct the calibration for xsltTimestamp()
 */
void
xsltCalibrateAdjust (long delta)
{
    calibration += delta;
}

/**
 * xsltTimestamp:
 *
 * Used for gathering profiling data
 *
 * Returns the number of tenth of milliseconds since the beginning of the
 * profiling
 */
long
xsltTimestamp (void)
{
#ifdef XSLT_WIN32_PERFORMANCE_COUNTER
    BOOL ok;
    LARGE_INTEGER performanceCount;
    LARGE_INTEGER performanceFrequency;
    LONGLONG quadCount;
    double seconds;
    static LONGLONG startupQuadCount = 0;
    static LONGLONG startupQuadFreq = 0;

    ok = QueryPerformanceCounter(&performanceCount);
    if (!ok)
        return 0;
    quadCount = performanceCount.QuadPart;
    if (calibration < 0) {
        calibration = 0;
        ok = QueryPerformanceFrequency(&performanceFrequency);
        if (!ok)
            return 0;
        startupQuadFreq = performanceFrequency.QuadPart;
        startupQuadCount = quadCount;
        return 0;
    }
    if (startupQuadFreq == 0)
        return 0;
    seconds = (quadCount - startupQuadCount) / (double) startupQuadFreq;
    return (long) (seconds * XSLT_TIMESTAMP_TICS_PER_SEC);

#else /* XSLT_WIN32_PERFORMANCE_COUNTER */
#ifdef HAVE_CLOCK_GETTIME
#if defined(CLOCK_MONOTONIC)
#define XSLT_CLOCK CLOCK_MONOTONIC
#elif defined(CLOCK_HIGHRES)
#define XSLT_CLOCK CLOCK_HIGHRES
#else
#define XSLT_CLOCK CLOCK_REALTIME
#endif
    static struct timespec startup;
    struct timespec cur;
    long tics;

    if (calibration < 0) {
        clock_gettime(XSLT_CLOCK, &startup);
        calibration = 0;
        calibration = xsltCalibrateTimestamps();
        clock_gettime(XSLT_CLOCK, &startup);
        return 0;
    }

    clock_gettime(XSLT_CLOCK, &cur);
    tics = (cur.tv_sec - startup.tv_sec) * XSLT_TIMESTAMP_TICS_PER_SEC;
    tics += (cur.tv_nsec - startup.tv_nsec)
        / (1000000000l / XSLT_TIMESTAMP_TICS_PER_SEC);

    tics -= calibration;
    return tics;

#elif HAVE_GETTIMEOFDAY
    static struct timeval startup;
    struct timeval cur;
    long tics;

    if (calibration < 0) {
        gettimeofday(&startup, NULL);
        calibration = 0;
        calibration = xsltCalibrateTimestamps();
        gettimeofday(&startup, NULL);
        return 0;
    }

    gettimeofday(&cur, NULL);
    tics = (cur.tv_sec - startup.tv_sec) * XSLT_TIMESTAMP_TICS_PER_SEC;
    tics += (cur.tv_usec - startup.tv_usec)
        / (1000000l / XSLT_TIMESTAMP_TICS_PER_SEC);

    tics -= calibration;
    return tics;
#else

    /* Neither gettimeofday() nor Win32 performance counter available */

    return 0;

#endif /* HAVE_GETTIMEOFDAY */
#endif /* XSLT_WIN32_PERFORMANCE_COUNTER */
}

static char *
pretty_templ_match (xsltTemplatePtr templ)
{
    static char dst[1001];
    char *src = (char *) templ->match;
    int i = 0, j;

    /* strip white spaces */
    for (j = 0; i < 1000 && src[j]; i++, j++) {
        for (; src[j] == ' '; j++)
            ;
        dst[i] = src[j];
    }
    if (i < 998 && templ->mode) {
        /* append [mode] */
        dst[i++] = '[';
        src = (char *) templ->mode;
        for (j = 0; i < 999 && src[j]; i++, j++) {
            dst[i] = src[j];
        }
        dst[i++] = ']';
    }
    dst[i] = '\0';
    return dst;
}

#define MAX_TEMPLATES 10000

/**
 * xsltSaveProfiling:
 * @ctxt:  an XSLT context
 * @output:  a FILE * for saving the information
 *
 * Save the profiling information on @output
 */
void
xsltSaveProfiling (xsltTransformContextPtr ctxt, FILE *output)
{
    int nb, i, j, k, l;
    int max;
    int total;
    unsigned long totalt;
    xsltTemplatePtr *templates;
    xsltStylesheetPtr style;
    xsltTemplatePtr templ1, templ2;
    int *childt;

    if (output == NULL || ctxt == NULL)
        return;
    if (ctxt->profile == 0)
        return;

    nb = 0;
    max = MAX_TEMPLATES;
    templates = xmlMalloc(max * sizeof(xsltTemplatePtr));
    if (templates == NULL)
        return;

    style = ctxt->style;
    while (style != NULL) {
        templ1 = style->templates;
        while (templ1 != NULL) {
            if (nb >= max)
                break;

            if (templ1->nbCalls > 0)
                templates[nb++] = templ1;
            templ1 = templ1->next;
        }

        style = xsltNextImport(style);
    }

    for (i = 0; i < nb - 1; i++) {
        for (j = i + 1; j < nb; j++) {
            if (templates[i]->time <= templates[j]->time
                || templates[i]->time == templates[j]->time
                    && templates[i]->nbCalls <= templates[j]->nbCalls) {
                templ1 = templates[j];
                templates[j] = templates[i];
                templates[i] = templ1;
            }
        }
    }

    /* print flat profile */

    fprintf(output, "%6s%20s%20s%10s  Calls Tot 100us Avg\n\n", "number",
            "match", "name", "mode");
    total = 0;
    totalt = 0;
    for (i = 0; i < nb; i++) {
        templ1 = templates[i];
        fprintf(output, "%5d ", i);
        if (templ1->match != NULL) {
            if (xmlStrlen(templ1->match) > 20)
                fprintf(output, "%s\n%26s", templ1->match, "");
            else
                fprintf(output, "%20s", templ1->match);
        } else {
            fprintf(output, "%20s", "");
        }
        if (templ1->name != NULL) {
            if (xmlStrlen(templ1->name) > 20)
                fprintf(output, "%s\n%46s", templ1->name, "");
            else
                fprintf(output, "%20s", templ1->name);
        } else {
            fprintf(output, "%20s", "");
        }
        if (templ1->mode != NULL) {
            if (xmlStrlen(templ1->mode) > 10)
                fprintf(output, "%s\n%56s", templ1->mode, "");
            else
                fprintf(output, "%10s", templ1->mode);
        } else {
            fprintf(output, "%10s", "");
        }
        fprintf(output, " %6d", templ1->nbCalls);
        fprintf(output, " %6ld %6ld\n", templ1->time,
                templ1->time / templ1->nbCalls);
        total += templ1->nbCalls;
        totalt += templ1->time;
    }
    fprintf(output, "\n%30s%26s %6d %6ld\n", "Total", "", total, totalt);

    /* print call graph */

    childt = xmlMalloc((nb + 1) * sizeof(int));
    if (childt == NULL)
        return;

    /* precalculate children times */
    for (i = 0; i < nb; i++) {
        templ1 = templates[i];

        childt[i] = 0;
        for (k = 0; k < nb; k++) {
            templ2 = templates[k];
            for (l = 0; l < templ2->templNr; l++) {
                if (templ2->templCalledTab[l] == templ1) {
                    childt[i] += templ2->time;
                }
            }
        }
    }
    childt[i] = 0;

    fprintf(output, "\nindex %% time    self  children    called     name\n");

    for (i = 0; i < nb; i++) {
        char ix_str[20], timep_str[20], times_str[20], timec_str[20],
            called_str[20];
        unsigned long t;

        templ1 = templates[i];
        /* callers */
        for (j = 0; j < templ1->templNr; j++) {
            templ2 = templ1->templCalledTab[j];
            for (k = 0; k < nb; k++) {
                if (templates[k] == templ2)
                    break;
            }
            t = templ2 ? templ2->time : totalt;
            snprintf(times_str, sizeof(times_str), "%8.3f",
                     (float) t / XSLT_TIMESTAMP_TICS_PER_SEC);
            snprintf(timec_str, sizeof(timec_str), "%8.3f",
                     (float) childt[k] / XSLT_TIMESTAMP_TICS_PER_SEC);
            snprintf(called_str, sizeof(called_str), "%6d/%d",
                     templ1->templCountTab
                         [j],          /* number of times caller calls 'this' */
                     templ1->nbCalls); /* total number of calls to 'this' */

            fprintf(output, "             %-8s %-8s %-12s     %s [%d]\n",
                    times_str, timec_str, called_str,
                    templ2 ? (templ2->name ? (char *) templ2->name
                                           : pretty_templ_match(templ2))
                           : "-",
                    k);
        }
        /* this */
        snprintf(ix_str, sizeof(ix_str), "[%d]", i);
        snprintf(timep_str, sizeof(timep_str), "%6.2f",
                 (float) templ1->time * 100.0 / totalt);
        snprintf(times_str, sizeof(times_str), "%8.3f",
                 (float) templ1->time / XSLT_TIMESTAMP_TICS_PER_SEC);
        snprintf(timec_str, sizeof(timec_str), "%8.3f",
                 (float) childt[i] / XSLT_TIMESTAMP_TICS_PER_SEC);
        fprintf(output, "%-5s %-6s %-8s %-8s %6d     %s [%d]\n", ix_str,
                timep_str, times_str, timec_str, templ1->nbCalls,
                templ1->name ? (char *) templ1->name
                             : pretty_templ_match(templ1),
                i);
        /* callees
         * - go over templates[0..nb] and their templCalledTab[]
         * - print those where we in the the call-stack
         */
        total = 0;
        for (k = 0; k < nb; k++) {
            templ2 = templates[k];
            for (l = 0; l < templ2->templNr; l++) {
                if (templ2->templCalledTab[l] == templ1) {
                    total += templ2->templCountTab[l];
                }
            }
        }
        for (k = 0; k < nb; k++) {
            templ2 = templates[k];
            for (l = 0; l < templ2->templNr; l++) {
                if (templ2->templCalledTab[l] == templ1) {
                    snprintf(times_str, sizeof(times_str), "%8.3f",
                             (float) templ2->time
                                 / XSLT_TIMESTAMP_TICS_PER_SEC);
                    snprintf(timec_str, sizeof(timec_str), "%8.3f",
                             (float) childt[k] / XSLT_TIMESTAMP_TICS_PER_SEC);
                    snprintf(called_str, sizeof(called_str), "%6d/%d",
                             templ2->templCountTab
                                 [l], /* number of times 'this' calls callee */
                             total);  /* total number of calls from 'this' */
                    fprintf(output,
                            "             %-8s %-8s %-12s     %s [%d]\n",
                            times_str, timec_str, called_str,
                            templ2->name ? (char *) templ2->name
                                         : pretty_templ_match(templ2),
                            k);
                }
            }
        }
        fprintf(output, "-----------------------------------------------\n");
    }

    fprintf(output, "\f\nIndex by function name\n");
    for (i = 0; i < nb; i++) {
        templ1 = templates[i];
        fprintf(output, "[%d] %s (%s:%d)\n", i,
                templ1->name ? (char *) templ1->name
                             : pretty_templ_match(templ1),
                xmlDocGetURL(templ1->style->doc), xmlNodeGetLine(templ1->elem));
    }

    fprintf(output, "\f\n");
    xmlFree(childt);

    xmlFree(templates);
}

/************************************************************************
 *									*
 *		Fetching profiling information				*
 *									*
 ************************************************************************/

/**
 * xsltGetProfileInformation:
 * @ctxt:  a transformation context
 *
 * This function should be called after the transformation completed
 * to extract template processing profiling information if available.
 * The information is returned as an XML document tree like
 * <?xml version="1.0"?>
 * <profile>
 * <template rank="1" match="*" name=""
 *         mode="" calls="6" time="48" average="8"/>
 * <template rank="2" match="item2|item3" name=""
 *         mode="" calls="10" time="30" average="3"/>
 * <template rank="3" match="item1" name=""
 *         mode="" calls="5" time="17" average="3"/>
 * </profile>
 * The caller will need to free up the returned tree with xmlFreeDoc()
 *
 * Returns the xmlDocPtr corresponding to the result or NULL if not available.
 */

xmlDocPtr
xsltGetProfileInformation (xsltTransformContextPtr ctxt)
{
    xmlDocPtr ret = NULL;
    xmlNodePtr root, child;
    char buf[100];

    xsltStylesheetPtr style;
    xsltTemplatePtr *templates;
    xsltTemplatePtr templ;
    int nb = 0, max = 0, i, j;

    if (!ctxt)
        return NULL;

    if (!ctxt->profile)
        return NULL;

    nb = 0;
    max = 10000;
    templates = (xsltTemplatePtr *) xmlMalloc(max * sizeof(xsltTemplatePtr));
    if (templates == NULL)
        return NULL;

    /*
     * collect all the templates in an array
     */
    style = ctxt->style;
    while (style != NULL) {
        templ = style->templates;
        while (templ != NULL) {
            if (nb >= max)
                break;

            if (templ->nbCalls > 0)
                templates[nb++] = templ;
            templ = templ->next;
        }

        style = (xsltStylesheetPtr) xsltNextImport(style);
    }

    /*
     * Sort the array by time spent
     */
    for (i = 0; i < nb - 1; i++) {
        for (j = i + 1; j < nb; j++) {
            if (templates[i]->time <= templates[j]->time
                || templates[i]->time == templates[j]->time
                    && templates[i]->nbCalls <= templates[j]->nbCalls) {
                templ = templates[j];
                templates[j] = templates[i];
                templates[i] = templ;
            }
        }
    }

    /*
     * Generate a document corresponding to the results.
     */
    ret = xmlNewDoc(BAD_CAST "1.0");
    root = xmlNewDocNode(ret, NULL, BAD_CAST "profile", NULL);
    xmlDocSetRootElement(ret, root);

    for (i = 0; i < nb; i++) {
        child = xmlNewChild(root, NULL, BAD_CAST "template", NULL);
        snprintf(buf, sizeof(buf), "%d", i + 1);
        xmlSetProp(child, BAD_CAST "rank", BAD_CAST buf);
        xmlSetProp(child, BAD_CAST "match", BAD_CAST templates[i]->match);
        xmlSetProp(child, BAD_CAST "name", BAD_CAST templates[i]->name);
        xmlSetProp(child, BAD_CAST "mode", BAD_CAST templates[i]->mode);

        snprintf(buf, sizeof(buf), "%d", templates[i]->nbCalls);
        xmlSetProp(child, BAD_CAST "calls", BAD_CAST buf);

        snprintf(buf, sizeof(buf), "%ld", templates[i]->time);
        xmlSetProp(child, BAD_CAST "time", BAD_CAST buf);

        snprintf(buf, sizeof(buf), "%ld",
                 templates[i]->time / templates[i]->nbCalls);
        xmlSetProp(child, BAD_CAST "average", BAD_CAST buf);
    };

    xmlFree(templates);

    return ret;
}

#endif /* WITH_XSLT_PROFILER */

/************************************************************************
 *									*
 *		Hooks for libxml2 XPath					*
 *									*
 ************************************************************************/

/**
 * xsltXPathCompileFlags:
 * @style: the stylesheet
 * @str:  the XPath expression
 * @flags: extra compilation flags to pass down to libxml2 XPath
 * @inst: the node whose in-scope namespaces should be validated against,
 *        or NULL to skip namespace-prefix validation
 *
 * Compile an XPath expression.  When @inst is non-NULL, validates the
 * expression's namespace prefixes against @inst's in-scope namespaces
 * at compile time (via XML_XPATH_CHECKNS), so an undeclared prefix fails
 * immediately rather than only when evaluated.  Reports such a failure
 * itself via xsltReportFatalError(), since no transform context exists
 * yet at compile time.
 *
 * Returns the xmlXPathCompExprPtr resulting from the compilation or NULL.
 *         the caller has to free the object.
 */
xmlXPathCompExprPtr
xsltXPathCompileFlags (xsltStylesheetPtr style, const xmlChar *str, int flags,
                       xmlNodePtr inst)
{
    xmlXPathContextPtr xpathCtxt;
    xmlXPathCompExprPtr ret;
    xmlNsPtr *nsList = NULL;
    xmlNsPtr *oldNsList = NULL;
    int nsNr = 0;
    int oldNsNr = 0;

    if (style != NULL) {
        xpathCtxt = style->principal->xpathCtxt;
        if (xpathCtxt == NULL)
            return NULL;
        xmlXPathContextSetDict(xpathCtxt, style->dict);
    } else {
        xpathCtxt = xmlXPathNewContext(NULL);
        if (xpathCtxt == NULL)
            return NULL;
    }

    if (inst != NULL) {
        nsList = xmlGetNsList(xmlNodeGetDoc(inst), inst);
        for (nsNr = 0; nsList && nsList[nsNr]; nsNr++)
            continue;

        oldNsList = xmlXPathContextGetNamespaces(xpathCtxt);
        oldNsNr = xmlXPathContextGetNsNr(xpathCtxt);

        xmlXPathContextSetNamespaces(xpathCtxt, nsList);
        xmlXPathContextSetNsNr(xpathCtxt, nsNr);

        flags |= XML_XPATH_CHECKNS;
    }

    xmlXPathContextSetFlags(xpathCtxt, flags);

    /*
    * Compile the expression.
    */
    ret = xmlXPathCtxtCompile(xpathCtxt, str);

    if (inst != NULL) {
        xmlXPathContextSetNamespaces(xpathCtxt, oldNsList);
        xmlXPathContextSetNsNr(xpathCtxt, oldNsNr);
        xmlFree(nsList);

        if (ret == NULL) {
            const xmlError *lastError = xmlGetLastError();
            const char *note = NULL;
            char buf[256];

            if (lastError != NULL && lastError->message != NULL)
                note = lastError->message;

            snprintf(buf, sizeof(buf),
                     "could not compile expression '%s'", (const char *) str);
            xsltReportFatalError(inst, buf, note, NULL);
        }
    }

    if (style == NULL) {
        xmlXPathFreeContext(xpathCtxt);
    }
    /*
     * TODO: there is a lot of optimizations which should be possible
     *       like variable slot precomputations, function precomputations, etc.
     */

    return ret;
}

/**
 * xsltXPathCompile:
 * @style: the stylesheet
 * @str:  the XPath expression
 * @inst: the node whose in-scope namespaces should be validated against,
 *        or NULL to skip namespace-prefix validation
 *
 * Compile an XPath expression
 *
 * Returns the xmlXPathCompExprPtr resulting from the compilation or NULL.
 *         the caller has to free the object.
 */
xmlXPathCompExprPtr
xsltXPathCompile (xsltStylesheetPtr style, const xmlChar *str,
                  xmlNodePtr inst)
{
    return xsltXPathCompileFlags(style, str, 0, inst);
}

/************************************************************************
 *									*
 *		Hooks for the debugger					*
 *									*
 ************************************************************************/

PSU_THREAD_LOCAL(int) xslDebugStatus;

/**
 * xsltGetDebuggerStatus:
 *
 * Get xslDebugStatus.
 *
 * Returns the value of xslDebugStatus.
 */
int
xsltGetDebuggerStatus (void)
{
    return xslDebugStatus;
}

static PSU_THREAD_LOCAL(char) xsltLastXPathErrorBuf[256];

/**
 * xsltLastXPathErrorMessage:
 *
 * Fetch the message from the most recent XPath error raised on this
 * thread (libxml2's global/thread-local last-error, populated by
 * xmlXPathErrFmt() regardless of whether a structured error handler
 * is registered).  This is where the specific "why" behind a failed
 * XPath evaluation lives -- e.g. the message built by CHECK_ARITY(),
 * such as "the function \"substring\" is called with the wrong
 * number of arguments (4)" -- for callers to report as a "note"
 * alongside their own, more general failure message.
 *
 * Returns a pointer to a thread-local static buffer, valid until the
 * next call to this function on the same thread, or NULL if there is
 * no current error, it isn't an XPath error, or it has no message.
 */
const char *
xsltLastXPathErrorMessage (void)
{
    const xmlError *err = xmlGetLastError();
    size_t len;

    if (err == NULL || err->domain != XML_FROM_XPATH || err->message == NULL)
        return NULL;

    len = strlen(err->message);
    while (len > 0 && (err->message[len - 1] == '\n'
                        || err->message[len - 1] == '\r'))
        len--;
    if (len == 0)
        return NULL;
    if (len >= sizeof(xsltLastXPathErrorBuf))
        len = sizeof(xsltLastXPathErrorBuf) - 1;

    memcpy(xsltLastXPathErrorBuf, err->message, len);
    xsltLastXPathErrorBuf[len] = 0;

    return xsltLastXPathErrorBuf;
}

#ifdef WITH_XSLT_DEBUGGER

/*
 * There is currently only 4 debugging callbacks defined
 * Debugger callbacks are disabled by default
 */
#define XSLT_CALLBACK_NUMBER 4

typedef struct _xsltDebuggerCallbacks xsltDebuggerCallbacks;
typedef xsltDebuggerCallbacks *xsltDebuggerCallbacksPtr;
struct _xsltDebuggerCallbacks {
    xsltHandleDebuggerCallback handler;
    xsltAddCallCallback add;
    xsltDropCallCallback drop;
    xsltHandleDebuggerErrorCallback errorHandler;
};

static xsltDebuggerCallbacks xsltDebuggerCurrentCallbacks = {
    NULL, /* handler */
    NULL, /* add */
    NULL, /* drop */
    NULL  /* errorHandler */
};

/**
 * xsltSetDebuggerStatus:
 * @value : the value to be set
 *
 * This function sets the value of xslDebugStatus.
 */
void
xsltSetDebuggerStatus (int value)
{
    xslDebugStatus = value;
}

/**
 * xsltSetDebuggerCallbacks:
 * @no : number of callbacks
 * @block : the block of callbacks
 *
 * This function allow to plug a debugger into the XSLT library
 * @block points to a block of memory containing the address of @no
 * callback routines.
 *
 * Returns 0 in case of success and -1 in case of error
 */
int
xsltSetDebuggerCallbacks (int no, void *block)
{
    xsltDebuggerCallbacksPtr callbacks;

    if (block == NULL || no != XSLT_CALLBACK_NUMBER)
        return -1;

    callbacks = (xsltDebuggerCallbacksPtr) block;
    xsltDebuggerCurrentCallbacks.handler = callbacks->handler;
    xsltDebuggerCurrentCallbacks.add = callbacks->add;
    xsltDebuggerCurrentCallbacks.drop = callbacks->drop;
    xsltDebuggerCurrentCallbacks.errorHandler = callbacks->errorHandler;
    return 0;
}

/**
 * xslHandleDebugger:
 * @cur : source node being executed
 * @node : data node being processed
 * @templ : temlate that applies to node
 * @ctxt : the xslt transform context
 *
 * If either cur or node are a breakpoint, or xslDebugStatus in state
 *   where debugging must occcur at this time then transfer control
 *   to the xslDebugBreak function
 */
void
xslHandleDebugger (xmlNodePtr cur, xmlNodePtr node, xsltTemplatePtr templ,
                   xsltTransformContextPtr ctxt)
{
    if (xsltDebuggerCurrentCallbacks.handler != NULL)
        xsltDebuggerCurrentCallbacks.handler(cur, node, templ, ctxt);
}

/*
 * Names of attributes that carry an XPath expression evaluated at
 * runtime, in the order we prefer to check them.  Used to locate the
 * xmlAttr whose tracked ->line/->column records where the failing
 * expression was actually written, as opposed to "cur" itself, whose
 * ->line/->column (if any) is just the position of the instruction's
 * opening tag.
 */
static const char * const xsltFatalErrorExprAttribs[] = {
    "select", "test", NULL
};

/*
 * Find the attribute on "cur" (an XSLT instruction node) that holds
 * the XPath expression it was executing when a fatal error occurred,
 * and return the real line/column we tracked when that attribute's
 * value was parsed.  Returns non-zero and fills "linep"/"colp" if a
 * known position was found, zero otherwise (synthetic/compiler-
 * generated expression, or no matching attribute).
 */
static int
xsltFindExprPosition (xmlNodePtr cur, int *linep, int *colp)
{
    const char * const *namep;

    if (cur == NULL)
        return 0;

    for (namep = xsltFatalErrorExprAttribs; *namep != NULL; namep++) {
        xmlAttrPtr attr = xmlHasProp(cur, (const xmlChar *) *namep);

        if (attr == NULL)
            continue;

        short col = xmlAttrGetColumn(attr);
        if (col == -1)
            continue;

        unsigned short line = xmlAttrGetLine(attr);
        *linep = line ? line : xmlGetLineNo(cur);
        *colp = col;
        return 1;
    }

    return 0;
}

/*
 * Fetch the raw text of a single source line (no trailing newline) by
 * reading "filename" from disk.  Used by xsltReportFatalError() to
 * show the offending line under a compiler-style error message, when
 * the caller hasn't already handed us the line via "known_srcline".
 * Returns non-zero and fills "buf" on success, zero if the line isn't
 * available (bad filename, line past EOF, etc).
 */
static int
xsltFetchSourceLine (const char *filename, int lineno, char *buf,
                     size_t bufsiz)
{
    FILE *fp;
    int count = 0;
    int found = 0;

    if (filename == NULL || lineno <= 0)
        return 0;

    fp = fopen(filename, "r");
    if (fp == NULL)
        return 0;

    while (fgets(buf, bufsiz, fp) != NULL) {
        count += 1;
        if (count == lineno) {
            size_t len = strlen(buf);
            if (len > 0 && buf[len - 1] == '\n')
                buf[len - 1] = '\0';
            found = 1;
            break;
        }
    }

    fclose(fp);
    return found;
}

/**
 * xsltReportFatalError:
 * @cur : source node where the error occurred
 * @message : the error message to report
 * @note : an optional, more specific detail about the failure, or
 *         NULL if none is available
 * @script_buffer : the full text of the script, if the caller already
 *         has it in memory (e.g. the SLAX debugger's script buffer)
 *         and it may not correspond to a real, re-readable file, or
 *         NULL to read the needed line from disk instead
 *
 * Report a fatal runtime error in a compiler-style format:
 *
 *     file:line:col: fatal error: message
 *     line |     source text
 *          |            ^
 *
 * The line/column are normally taken from the real position tracked
 * for the failing expression (see xsltFindExprPosition()).  But if
 * "message" contains a '...'-quoted token (e.g. "the variable
 * '$pStyle' is undefined") whose first occurrence on the source line
 * falls at or after that tracked position, we point at the token
 * instead -- it's more specific, e.g. the exact undefined-variable
 * reference within a larger expression, rather than just where the
 * expression starts.  A token that matches only *before* the tracked
 * position (e.g. the assignment target "'$b'" in "expression for
 * variable '$b' failed to evaluate", which can coincidentally match
 * earlier, unrelated text on the line) is ignored in favor of the
 * tracked position.  With no tracked position at all, the token (if
 * any) is used outright, and failing that, the caret points at the
 * line's first non-blank column.  Falls back to a plain "file:line:
 * fatal error: message" (no source line/caret) when the source line
 * isn't available.
 *
 * If "note" is non-NULL, it is printed as an indented "note: ..."
 * line following the report, aligned under the gutter when a source
 * line/caret was printed.  This carries more specific detail (e.g.
 * from the underlying XPath error) that "message" doesn't have, such
 * as which function was miscalled and with how many arguments.
 */
void
xsltReportFatalError (xmlNodePtr cur, const char *message, const char *note,
                      const char *script_buffer)
{
    const char *filename = NULL;
    int line_no = 0;
    int known_col = -1;

    if (cur != NULL) {
        int exprline;

        if (xsltFindExprPosition(cur, &exprline, &known_col))
            line_no = exprline;
        else
            line_no = xmlGetLineNo(cur);

        if (xmlNodeGetDoc(cur))
            filename = (const char *) xmlDocGetURL(xmlNodeGetDoc(cur));
    }

    const char *base = filename ? strrchr(filename, '/') : NULL;
    base = base ? base + 1 : filename;

    char srcline[BUFSIZ];
    int have_line;

    if (script_buffer != NULL) {
        const char *last = script_buffer;
        const char *xp;
        int count = 1;

        have_line = 0;
        if (line_no > 0) {
            for (xp = strchr(last, '\n'); xp; xp = strchr(last, '\n')) {
                if (count == line_no)
                    break;
                count += 1;
                last = xp + 1;
            }

            if (count == line_no) {
                size_t len = xp ? (size_t) (xp - last) : strlen(last);
                if (len >= sizeof(srcline))
                    len = sizeof(srcline) - 1;
                memcpy(srcline, last, len);
                srcline[len] = '\0';
                have_line = 1;
            }
        }
    } else {
        have_line = (base != NULL && line_no > 0
                     && xsltFetchSourceLine(filename, line_no, srcline,
                                            sizeof(srcline)));
    }

    int col = 0;
    int marker_col = -1;

    /*
     * Independently of known_col, see if "message" names a '...'-quoted
     * token that actually occurs on the source line -- e.g. an
     * undefined-variable reference like '$pStyle' that appears
     * literally inside the failing expression.  We still prefer
     * known_col when it's at least as far along the line: a token
     * quoted merely to identify the failure (e.g. the variable being
     * assigned in "expression for variable '$b' failed to evaluate",
     * where "$b" is the assignment target, not part of the failing
     * expression) can coincidentally match earlier text on the same
     * line, which would be a worse answer than known_col.
     */
    if (have_line) {
        const char *marker = strchr(message, '\'');
        const char *hit = NULL;

        if (marker != NULL) {
            const char *end = strchr(marker + 1, '\'');
            if (end != NULL && end > marker + 1) {
                size_t mlen = end - (marker + 1);
                char token[BUFSIZ];

                if (mlen < sizeof(token)) {
                    memcpy(token, marker + 1, mlen);
                    token[mlen] = '\0';
                    hit = strstr(srcline, token);
                }
            }
        }

        if (hit != NULL)
            marker_col = (int) (hit - srcline) + 1;
    }

    if (have_line && known_col != -1
        && (marker_col == -1 || marker_col < known_col)) {
        col = known_col;
    } else if (have_line && marker_col != -1) {
        col = marker_col;
    } else if (have_line) {
        const char *cp = srcline;
        while (*cp == ' ' || *cp == '\t')
            cp++;
        col = (int) (cp - srcline) + 1;
    }

    if (base != NULL && line_no > 0 && col > 0)
        fprintf(stderr, "%s:%d:%d: fatal error: %s\n", base, line_no, col,
                message);
    else if (base != NULL && line_no > 0)
        fprintf(stderr, "%s:%d: fatal error: %s\n", base, line_no, message);
    else
        fprintf(stderr, "fatal error: %s\n", message);

    if (have_line) {
        fprintf(stderr, "%d | %s\n", line_no, srcline);

        char gutter[32];
        int gutter_len = snprintf(gutter, sizeof(gutter), "%d", line_no);
        char caret[BUFSIZ];
        int n = 0;
        int i;
        const char *sep = " | ";

        for (i = 0; i < gutter_len && n < (int) sizeof(caret) - 1; i++)
            caret[n++] = ' ';
        for ( ; *sep != '\0' && n < (int) sizeof(caret) - 1; sep++)
            caret[n++] = *sep;

        for (i = 1; i < col && n < (int) sizeof(caret) - 1; i++)
            caret[n++] = ' ';

        if (n < (int) sizeof(caret) - 1)
            caret[n++] = '^';
        caret[n] = '\0';

        fprintf(stderr, "%s\n", caret);

        if (note != NULL)
            fprintf(stderr, "%*snote: %s\n", gutter_len + 1, "", note);
    } else if (note != NULL) {
        fprintf(stderr, "note: %s\n", note);
    }
}

/**
 * xslHandleDebuggerError:
 * @cur : source node where the error occurred
 * @ctxt : the xslt transform context
 * @message : the error message that was reported
 * @note : an optional, more specific detail about the failure (e.g.
 *         from xsltLastXPathErrorMessage()), or NULL if none is
 *         available
 *
 * Gives an attached debugger first chance to handle a fatal runtime
 * error, so the user can inspect the still-live transform context
 * before it unwinds.  If no debugger is attached, reports the error
 * itself via xsltReportFatalError(); ctxt->fatalErrorReported latches
 * after the first report so a failure that propagates through several
 * callers is only reported once.
 *
 * Returns non-zero if the error was reported, zero if the debugger
 * declined -- callers use this to decide whether to print their own
 * fallback message.
 */
int
xslHandleDebuggerError (xmlNodePtr cur, xsltTransformContextPtr ctxt,
                        const char *message, const char *note)
{
    if (xsltDebuggerCurrentCallbacks.errorHandler != NULL)
        return xsltDebuggerCurrentCallbacks.errorHandler(cur, ctxt, message,
                                                           note);

    if (ctxt != NULL) {
        if (ctxt->fatalErrorReported)
            return 1;
        ctxt->fatalErrorReported = 1;
    }

    xsltReportFatalError(cur, message, note, NULL);
    return 1;
}

/**
 * xslAddCall:
 * @templ : current template being applied
 * @source : the source node being processed
 *
 * Add template "call" to call stack
 * Returns : 1 on sucess 0 otherwise an error may be printed if
 *            WITH_XSLT_DEBUG_BREAKPOINTS is defined
 */
int
xslAddCall (xsltTemplatePtr templ, xmlNodePtr source)
{
    if (xsltDebuggerCurrentCallbacks.add != NULL)
        return xsltDebuggerCurrentCallbacks.add(templ, source);
    return 0;
}

/**
 * xslDropCall:
 *
 * Drop the topmost item off the call stack
 */
void
xslDropCall (void)
{
    if (xsltDebuggerCurrentCallbacks.drop != NULL)
        xsltDebuggerCurrentCallbacks.drop();
}

#endif /* WITH_XSLT_DEBUGGER */
