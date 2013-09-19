/*
 * Copyright (c) 2010-2013, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * This file includes hooks and additions to the libxml2 and libxslt APIs.
 */

#ifndef LIBSLAX_XMLSOFT_H
#define LIBSLAX_XMLSOFT_H

#ifdef LIBSLAX_XMLSOFT_NEED_PRIVATE

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#include <ctype.h>

#include <libxslt/xsltutils.h>	/* For xsltHandleDebuggerCallback, etc */
#include <libxslt/extensions.h>
#include <libslax/slaxdyn.h>

#define NUM_ARRAY(x)    (sizeof(x)/sizeof(x[0]))

/* ----------------------------------------------------------------------
 * Functions that aren't prototyped in libxml2 headers
 */

/**
 * nodePush:
 * @ctxt:  an XML parser context
 * @value:  the element node
 *
 * Pushes a new element node on top of the node stack
 *
 * Returns 0 in case of error, the index in the stack otherwise
 */
int
nodePush(xmlParserCtxtPtr ctxt, xmlNodePtr value);

/**
 * nodePop:
 * @ctxt: an XML parser context
 *
 * Pops the top element node from the node stack
 *
 * Returns the node just removed
 */
xmlNodePtr
nodePop(xmlParserCtxtPtr ctxt);

/*
 * Simple error call
 */
#define LX_ERR(_msg...) xsltGenericError(xsltGenericErrorContext, _msg)

/**
 * Call xsltSetDebuggerCallbacks() with the properly typed argument.
 * I'm not sure of the reason but the arguments are untyped, giving it
 * a significant "ick" factor.  We preserve our dignity by hiding behind
 * an inline.
 * @see http://mail.gnome.org/archives/xslt/2010-September/msg00013.html
 */
static inline void
xsltSetDebuggerCallbacksHelper (xsltHandleDebuggerCallback handler,
				xsltAddCallCallback add_frame,
				xsltDropCallCallback drop_frame)
{
    void *cb[] = { handler, add_frame, drop_frame };

    xsltSetDebuggerCallbacks(NUM_ARRAY(cb), &cb);
}

/*
 * Free a chunk of memory
 */
static inline void
xmlFreeAndEasy (void *ptr)
{
    if (ptr)
	xmlFree(ptr);
}

/*
 * The stock xmlStrchr() returns a "const" pointer, which isn't good.
 */
static inline xmlChar *
xmlStrchru (xmlChar *str, xmlChar val)
{
    for ( ; str && *str != 0; str++)
        if (*str == val)
	    return str;

    return NULL;
}

static inline void
slaxRegisterFunction (const char *uri, const char *fn, xmlXPathFunction func)
{
    if (xsltRegisterExtModuleFunction((const xmlChar *) fn,
				      (const xmlChar *) uri,
				      func))
         xsltGenericError(xsltGenericErrorContext,
		  "could not register extension function for {%s}:%s\n",
			  uri ?: "", fn);
}

static inline void
slaxUnregisterFunction (const char *uri, const char *fn)
{
    xsltUnregisterExtModuleFunction((const xmlChar *) fn,
				    (const xmlChar *) uri);
}

static inline void
slaxRegisterFunctionTable (const char *uri, slax_function_table_t *ftp)
{
    if (ftp)
	for ( ; ftp->ft_name; ftp++)
	    slaxRegisterFunction(uri, ftp->ft_name, ftp->ft_func);
}

static inline void
slaxUnregisterFunctionTable (const char *uri, slax_function_table_t *ftp)
{
    if (ftp)
	for ( ; ftp->ft_name; ftp++)
	    slaxUnregisterFunction(uri, ftp->ft_name);
}

static inline void
slaxRegisterElement (const char *uri, const char *fn, 
		     xsltPreComputeFunction fcompile,
		     xsltTransformFunction felement)
{
    xsltRegisterExtModuleElement((const xmlChar *) fn, (const xmlChar *) uri,
				 fcompile, felement);
}

static inline void
slaxUnregisterElement (const char *uri, const char *fn)
{
    xsltUnregisterExtModuleElement((const xmlChar *) fn,
				   (const xmlChar *) uri);
}

static inline void
slaxRegisterElementTable (const char *uri, slax_element_table_t *etp)
{
    if (etp)
	for ( ; etp->et_name; etp++)
	    slaxRegisterElement(uri, etp->et_name,
				etp->et_fcompile, etp->et_felement);
}

static inline void
slaxUnregisterElementTable (const char *uri, slax_element_table_t *etp)
{
    if (etp)
	for ( ; etp->et_name; etp++)
	    slaxUnregisterElement(uri, etp->et_name);
}

static inline int
xmlAtoi (const xmlChar *nptr)
{
    return atoi((const char *) nptr);
}

static inline char *
xmlStrdup2 (const char *str)
{
    return (char *) xmlStrdup((const xmlChar *) str);
}

/*
 * There's no clear way to stop the XSLT engine in the middle of
 * a script, but this is the way <xsl:message terminate="yes">
 * does it.  See xsltMessage().
 */
static inline void
xsltStopEngine (xsltTransformContextPtr ctxt)
{
    if (ctxt)
	ctxt->state = XSLT_STATE_STOPPED;
}

static inline xmlNodePtr
xmlAddChildContent (xmlDocPtr docp, xmlNodePtr parent,
		    const xmlChar *name, const xmlChar *value)
{
    xmlNodePtr nodep = xmlNewDocRawNode(docp, NULL, name, value);
    if (nodep) {
	xmlAddChild(parent, nodep);
    }

    return nodep;
}


/*
 * Return the name of a node
 */
static inline const char *
xmlNodeName (xmlNodePtr np)
{
    if (np == NULL || np->name == NULL)
	return NULL;
    return (const char *) np->name;
}

static inline int
slaxStringIsWhitespace (const char *str)
{
    for ( ; *str; str++)
	if (!isspace((int) *str))
	    return FALSE;
    return TRUE;
}

/*
 * Return the value of this element
 */
static inline const char *
xmlNodeValue (xmlNodePtr np)
{
    xmlNodePtr gp;
  
    if (np->type == XML_ELEMENT_NODE) {
	for (gp = np->children; gp; gp = gp->next)
	    if (gp->type == XML_TEXT_NODE
		    && !slaxStringIsWhitespace((char *) gp->content))
		return (char *) gp->content;
	/*
	 * If we found the element but not a valid text node,
	 * return an empty string to the caller can see
	 * empty elements.
	 */
	return "";
    }
     
    return NULL;
}

/* libxslt provides no means for setting the max call depth */
extern int xsltMaxDepth;

static inline int
xsltGetMaxDepth (void)
{
    return xsltMaxDepth;
}

static inline void
xsltSetMaxDepth (int value)
{
    xsltMaxDepth = value;
}

/*
 * Add a new attribute (aka property) to a node
 */
static inline void
xmlAddProp (xmlNodePtr nodep, xmlAttrPtr newp)
{
    if (nodep == NULL)
	return;

    newp->parent = nodep;
    if (nodep->properties == NULL) {
	nodep->properties = newp;
    } else {
	xmlAttrPtr prev = nodep->properties;

	while (prev->next != NULL)
	    prev = prev->next;
	prev->next = newp;
	newp->prev = prev;
    }
}

/**
 * Determine if a name is reserved.  A useful function, perhaps, but
 * I'm not sure how/when to use it.  If I fail elements and attributes
 * that are reserved, then in the future if/when some new feature
 * uses 'xml.*', I will fail it, which isn't acceptable.
 *
 * So for now, nothing will call this.
 *
 * @see http://www.w3.org/TR/xml/#sec-common-syn
 * @param name [in] name to test
 * @return boolean TRUE if the name is reserved by the XML spec
 */
static inline int
xmlIsReserved (const char *name)
{
    return (name && strlen(name) > 3 && strncasecmp(name, "xml", 3) == 0);
}

#endif /* LIBSLAX_XMLSOFT_NEED_PRIVATE */
#endif /* LIBSLAX_XMLSOFT_H */
