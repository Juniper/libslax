/*
 * Copyright (c) 2010-2025, Juniper Networks, Inc.
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

/*
 * xsltutils.h declares xsltSetDebuggerCallbacks()/xsltSetDebuggerStatus()
 * only when WITH_DEBUGGER is defined, i.e. only when libbxslt was itself
 * built with debugger support.  We used to punt-declare them here
 * unconditionally, but libbxslt is ours now and WITH_DEBUGGER really
 * does control whether those symbols exist in the compiled library --
 * declaring (and calling) them regardless is how "built without the
 * debugger" turns into a SEGV instead of a clean error.  So we rely on
 * the same #ifdef WITH_DEBUGGER here, fed by the same shared config
 * header libbxslt was built from.
 */
#include <libxslt/xsltutils.h>  /* For xsltSetDebuggerCallbacks, etc */

#include <libxslt/extensions.h>
#include <libslax/slaxdyn.h>

#define NUM_ARRAY(x)    (sizeof(x)/sizeof(x[0]))

/*
 * Simple error call
 */
#define LX_ERR(_msg...) xsltGenericError(xsltGenericErrorContext, _msg)

#ifdef WITH_DEBUGGER
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
				xsltDropCallCallback drop_frame,
				xsltHandleDebuggerErrorCallback error_handler)
{
    void *cb[] = { handler, add_frame, drop_frame, error_handler };

    xsltSetDebuggerCallbacks(NUM_ARRAY(cb), &cb);
}
#endif /* WITH_DEBUGGER */

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

/*
 * Sadly, xmlAddChild can coalesce the item we're adding with other
 * nodes and free our node.  It returns the new node, so we need to
 * pay attention.  If it return NULL, something's gone wrong and we
 * have to free the node ourselves.
 */
static inline xmlNodePtr
slaxAddChild (xmlNodePtr parent, xmlNodePtr child)
{
    if (child == NULL)
	return NULL;

    if (parent == NULL) {
	xmlFreeNode(child);
	return NULL;
    }

    xmlNodePtr added = xmlAddChild(parent, child);
    if (added == NULL) {
	xmlFreeNode(child);
	return NULL;
    }

    return added;
}

/*
 * Similar story....
 */
static inline xmlNodePtr
slaxAddChildAttrib (xmlNodePtr parent, xmlNodePtr child,
	      const char *attr, const char *value)
{
    child = slaxAddChild(parent, child);
    if (child && attr)
	xmlSetProp(child, (const xmlChar *) attr, (const xmlChar *) value);

    return child;
}

/*
 * Add a child node to the parent with the given name and value.  It's
 * "fire and forget" in that errors are ignored.  The parent doesn't care
 * about the success or the value of the child.
 */
static inline void
slaxAddChildName (xmlDocPtr docp, xmlNodePtr parent, const char *name, 
                  const char *value)
{
    if (docp == NULL || name == NULL)
        return;

    xmlNodePtr newp = xmlNewDocNode(docp, NULL, (const xmlChar *) name, 
                                    (const xmlChar *) value);

    slaxAddChild(parent, newp);
}

static inline int
slaxStringIsWhitespace (const char *str)
{
    for ( ; *str; str++)
	if (!isspace((int) *str))
	    return FALSE;
    return TRUE;
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

static inline xmlParserCtxtPtr
slaxSetupFakeContext (void)
{
    static const xmlChar just_say_no[] = " ";

    return xmlCreateDocParserCtxt(just_say_no);
}

/*
 * Turns out libxml2's xmlValidateName() allows some invalid stuff, so
 * we enforce our own validity checks.
 */
static inline int
slaxValidateName (const char *name)
{
    if (name == NULL || *name == '\0')
	return FALSE;

    int valid = xmlValidateName((const xmlChar *) name, FALSE);
    if (!valid)
	return valid;

    size_t len = strlen(name);
    if (strspn(name, ":/@\"\'\\") == len)
	return FALSE;

    return TRUE;
}

#endif /* LIBSLAX_XMLSOFT_NEED_PRIVATE */
#endif /* LIBSLAX_XMLSOFT_H */
