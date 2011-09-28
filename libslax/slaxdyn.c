/*
 * $Id$
 *
 * Copyright (c) 2006-2010, Juniper Networks, Inc.
 * All rights reserved.
 * See ../Copyright for the status of this software
 *
 * slaxdyn.c -- dynamic loading of extension libraries
 *
 * libslax allows extension functions to be loaded dynamically
 * based on the "extension-element-prefixes" attribute of the
 * <xsl:stylesheet> element.  For each listed prefix, we find the
 * associated URI and translate this string into a url-escaped value.
 *
 *     This escaping turns ugly URIs into even uglier names but any
 *     '/'s are escaped into "%2F", which is our real motivation here
 *     since we're using these as base names for files.
 *
 * We then suffix this value with ".so" and search for a shared
 * library named by that filename, using a built-in directory as
 * well as any directories passed fir slaxDynAdd().
 *
 * When a shared library is found, we open it, find the magic symbol
 * "slaxDynLibInit" and invoke it.  This function is responsible
 * for registering any functions or elements defined by the namespace.
 * Errors, missing librarys, and missing namespaces are not considered
 * errors and are not reported.
 *
 * The libxslt web pages consider this a "portability nightmare", and
 * they may well be correct.  This feature may be limited to platforms
 * that support dlopen() and dlsym().
 */

#include <sys/queue.h>
#include <dlfcn.h>
#include <string.h>

#include <libxml/uri.h>
#include <libxml/tree.h>

#include "config.h"

#include "slaxinternals.h"
#include <libslax/slax.h>
#include "slaxdata.h"

#ifdef HAVE_DLFCN_H
#include "slaxdyn.h"
#if !defined(HAVE_DLFUNC) && defined(HAVE_DLSYM)
#define dlfunc(_p, _n)		dlsym(_p, _n)
#endif
#else /* HAVE_DLFCN_H */
#define dlopen(_n, _f)		NULL /* Fail */
#define dlsym(_p, _n)		NULL /* Fail */
#define dlfunc(_p, _n)		NULL /* Fail */
#endif /* HAVE_DLFCN_H */

slax_data_list_t slaxDynDirList;
slax_data_list_t slaxDynLoaded;
slax_data_list_t slaxDynLibraries;

void
slaxDynAdd (const char *dir)
{
    slaxDataListAddNul(&slaxDynDirList, dir);
}



static void
slaxDynLoadNamespace (xmlDocPtr docp UNUSED, xmlNodePtr root UNUSED,
		      const char *ns)
{
    slax_data_node_t *dnp;
    xmlChar *ret;
    void *dlp = NULL;
    size_t bufsiz = BUFSIZ;
    char *buf = alloca(bufsiz);

    SLAXDATALIST_FOREACH(dnp, &slaxDynLoaded) {
	if (streq(ns, dnp->dn_data))
	    return;		/* Already loaded */
    }
    slaxDataListAddNul(&slaxDynLoaded, ns);

    ret = xmlURIEscapeStr((const xmlChar *) ns, (const xmlChar *) "-_.");
    if (ret == NULL)
	return;

    SLAXDATALIST_FOREACH(dnp, &slaxDynDirList) {
	static const char fmt[] = "%s/%s.ext";
	char *dir = dnp->dn_data;
	size_t len = snprintf(buf, bufsiz, fmt, dir, ret);

	if (len > bufsiz) {
	    bufsiz = len + BUFSIZ;
	    buf = alloca(bufsiz);
	    len = snprintf(buf, bufsiz, fmt, dir, ret);
	}

	dlp = dlopen((const char *) buf, RTLD_NOW);
	if (dlp)
	    break;
    }

    if (dlp) {
	/*
	 * If the library exists, find the initializer function and
	 * call it.
	 */
	slaxDynInitFunc_t func;

	func = dlfunc(dlp, SLAX_DYN_INIT_NAME);
	if (func) {
	    static slax_dyn_arg_t arg; /* Static zeros */
	    slax_dyn_arg_t *dap;

	    /*
	     * We're building a list of open libraries, using the
	     * "args" structure as our handle.  We store these in a
	     * slaxDataList, which is odd but too convenient too pass
	     * up.  The static arg above it just used to initialize
	     * dnp->dn_data.
	     */
	    dnp = slaxDataListAddLen(&slaxDynLibraries,
				     (char *) &arg, sizeof(arg));
	    if (dnp) {
		dap = (slax_dyn_arg_t *) dnp->dn_data;
		dap->da_version = SLAX_DYN_VERSION;
		dap->da_handle = dlp;
		dap->da_uri = xmlStrdup2(ns);

		(*func)(SLAX_DYN_VERSION, dap);

		if (dap->da_functions)
		    slaxRegisterFunctionTable(dap->da_uri, dap->da_functions);
		if (dap->da_elements)
		    slaxRegisterElementTable(dap->da_uri,
						dap->da_elements);
	    }
	}
    }

    xmlFree(ret);
}

static char *
slaxDynExtensionPrefixes (xmlDocPtr docp UNUSED, xmlNodePtr nodep, int top)
{
    if (top)
	return slaxGetAttrib(nodep, ATT_EXTENSION_ELEMENT_PREFIXES);

    return (char *) xmlGetNsProp(nodep,
			     (const xmlChar *) ATT_EXTENSION_ELEMENT_PREFIXES,
			     (const xmlChar *) XSL_URI);
}

/*
 * Find all the namespaces indicated by the "extension-element-prefixes"
 * attribute of the node.  This is a bit tricky because the spec says:
 *
 *      A namespace is designated as an extension namespace by using
 *      an extension-element-prefixes attribute on an xsl:stylesheet
 *      element or an xsl:extension-element-prefixes attribute on a
 *      literal result element or extension element.
 */
static void
slaxDynFindNamespaces (slax_data_list_t *listp, xmlDocPtr docp,
		       xmlNodePtr nodep, int top)
{
    static char white_space[] = " \t\n\r";
    char *prefixes, *cp, *np, *ep;
    xmlNsPtr nsp;
    xmlNodePtr childp;

    prefixes = slaxDynExtensionPrefixes(docp, nodep, top);
    if (prefixes) {
	for (cp = prefixes, ep = cp + strlen(cp); cp && cp < ep; cp = np) {

	    /* Find the next prefix token */
	    cp += strspn(cp, white_space); /* Skip leading whitespace */
	    np = cp + strcspn(cp, white_space);
	    if (*np)
		*np++ = '\0';

	    /* Find the associated namespace and add it */
	    nsp = xmlSearchNs(docp, nodep, (const xmlChar *) cp);
	    if (nsp && nsp->href)
		slaxDataListAddNul(listp, (const char *) nsp->href);
	}
    }

    /*
     * Since any element can be tagged with <xsl:extension-element-prefixes>,
     * we need to recurse through all our descendents.
     */
    for (childp = nodep->children; childp; childp = childp->next) {
	if (childp->type == XML_ELEMENT_NODE)
	    slaxDynFindNamespaces(listp, docp, childp, FALSE);
    }
}

/*
 * Load any extension libraries referenced in the given document.  The
 * implementation is fairly simple: find them and then load them.
 */
void
slaxDynLoad (xmlDocPtr docp)
{
    xmlNodePtr root = xmlDocGetRootElement(docp);
    slax_data_list_t nslist;
    slax_data_node_t *dnp;

    slaxDataListInit(&nslist);

    slaxDynFindNamespaces(&nslist, docp, root, TRUE);

    SLAXDATALIST_FOREACH(dnp, &nslist) {
	slaxDynLoadNamespace(docp, root, (const char *) dnp->dn_data);
    }

    slaxDataListClean(&nslist);
}

/*
 * Initialize the entire dynamic extension loading mechanism.
 */
void
slaxDynInit (void)
{
    slaxDataListInit(&slaxDynDirList);
    slaxDataListAddNul(&slaxDynDirList, SLAX_EXTDIR);

    slaxDataListInit(&slaxDynLoaded);
    slaxDataListInit(&slaxDynLibraries);
}

/*
 * De-initialize the entire dynamic extension loading mechanism.
 */
void
slaxDynClean (void)
{
    slax_data_node_t *dnp;

    slaxDataListClean(&slaxDynDirList);
    slaxDataListClean(&slaxDynLoaded);

    SLAXDATALIST_FOREACH(dnp, &slaxDynLibraries) {
	slax_dyn_arg_t *dap;
	dap = (slax_dyn_arg_t *) dnp->dn_data;
	if (dap->da_handle) {
	    slaxDynInitFunc_t func;

	    /*
	     * If the extension gave us a list of functions or element,
	     * we can go ahead and unregister them
	     */
	    if (dap->da_functions)
		    slaxUnregisterFunctionTable(dap->da_uri,
						dap->da_functions);
	    if (dap->da_elements)
		    slaxUnregisterElementTable(dap->da_uri,
						dap->da_elements);

	    /* If there's a cleanup function, then call it */
	    func = dlfunc(dap->da_handle, SLAX_DYN_CLEAN_NAME);
	    if (func)
		(*func)(SLAX_DYN_VERSION, dap);

	    dlclose(dap->da_handle);
	}
    }

    slaxDataListClean(&slaxDynLibraries);
}
