/*
 * Copyright (c) 2006-2013, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
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
#include <string.h>
#include <errno.h>

#include <libxml/uri.h>
#include <libxml/tree.h>

#include "slaxinternals.h"
#include <libslax/slax.h>
#include "slaxdata.h"

#include "slaxdyn.h"

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#if !defined(HAVE_DLFUNC)
#define dlfunc(_p, _n)		dlsym(_p, _n)
#endif
#else /* HAVE_DLFCN_H */
#define dlopen(_n, _f)		NULL /* Fail */
#define dlsym(_p, _n)		NULL /* Fail */
#define dlfunc(_p, _n)		NULL /* Fail */
#endif /* HAVE_DLFCN_H */

typedef struct slax_dyn_node_s {
    TAILQ_ENTRY(slax_dyn_node_s) dn_link; /* Next session */
    struct slax_dyn_arg_s dn_da;	   /* Argument data */
} slax_dyn_node_t;

typedef TAILQ_HEAD(slax_dyn_list_s, slax_dyn_node_s) slax_dyn_list_t;

static slax_data_list_t slaxDynDirList;
static slax_data_list_t slaxDynLoaded;
static slax_dyn_list_t slaxDynLibraries;

static int slaxDynInited;

void
slaxDynAdd (const char *dir)
{
    if (!slaxDynInited)
	slaxDynInit();

    slaxDataListAddNul(&slaxDynDirList, dir);
}

void
slaxDynAddPath (const char *path)
{
    int len = strlen(path) + 1;
    char *dir = alloca(len), *np;

    memcpy(dir, path, len);
    for (np = dir; np; dir = np) {
	np = strchr(dir, ':');
	if (np)
	    *np++ = '\0';
	if (*dir)
	    slaxDynAdd(dir);
    }
}


static void
slaxDynLoadNamespace (xmlDocPtr docp UNUSED, xmlNodePtr root UNUSED,
		      const char *ns)
{
    slax_data_node_t *dnp;
    xmlChar *ret;
    void *dlp = NULL;
    char buf[MAXPATHLEN];

    if (slaxDynMarkLoaded(ns))
	return;

    ret = xmlURIEscapeStr((const xmlChar *) ns, (const xmlChar *) "-_.");
    if (ret == NULL)
	return;

    SLAXDATALIST_FOREACH(dnp, &slaxDynDirList) {
	static const char fmt[] = "%s/%s.ext";
	char *dir = dnp->dn_data;
	size_t len = snprintf(buf, sizeof(buf), fmt, dir, ret);

	if (len > sizeof(buf))	/* Should not occur */
	    continue;

	slaxLog("extension: attempting %s", buf);
	dlp = dlopen((const char *) buf, RTLD_NOW);
	if (dlp)
	    break;
	slaxLog("extension failed: %s", dlerror() ?: "none");
    }

    if (dlp) {
	/*
	 * If the library exists, find the initializer function and
	 * call it.
	 */
	slax_dyn_init_func_t func;

	slaxLog("extension: success for %s", buf);

	func = (slax_dyn_init_func_t) dlfunc(dlp, SLAX_DYN_INIT_NAME);
	if (func) {
	    /*
	     * Build a list of open libraries.
	     */
	    slax_dyn_node_t *dynp = xmlMalloc(sizeof(*dynp));
	    if (dynp) {
		slax_dyn_arg_t *dap = &dynp->dn_da;

		bzero(dynp, sizeof(*dynp));

		TAILQ_INSERT_TAIL(&slaxDynLibraries, dynp, dn_link);

		dap->da_version = SLAX_DYN_VERSION;
		dap->da_handle = dlp;
		dap->da_uri = xmlStrdup2(ns);

		slaxLog("extension: calling %s::%s", buf, SLAX_DYN_INIT_NAME);
		(*func)(SLAX_DYN_VERSION, dap);

		if (dap->da_functions)
		    slaxRegisterFunctionTable(dap->da_uri, dap->da_functions);
		if (dap->da_elements)
		    slaxRegisterElementTable(dap->da_uri,
						dap->da_elements);
	    } else {
		slaxLog("%u was not found for %s", SLAX_DYN_VERSION, buf);
		dlclose(dlp);
	    }
	} else
	    dlclose(dlp);
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
	xmlFree(prefixes);
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
 * Find the uri behind a "well-known" prefix
 */
int
slaxDynFindPrefix (char *uri, size_t urisiz, const char *name)
{
    char buf[MAXPATHLEN];
    slax_data_node_t *dnp;

    SLAXDATALIST_FOREACH(dnp, &slaxDynDirList) {
	static const char fmt[] = "%s/%s.prefix";
	static const char ext[] = ".ext";

	char *dir = dnp->dn_data;
	size_t len = snprintf(buf, sizeof(buf) - 1, fmt, dir, name);

	if (len > sizeof(buf))	/* Should not occur */
	    continue;

	len = readlink(buf, uri, urisiz - 1);
	if (len > urisiz)	/* Should not occur */
	    continue;

	uri[len] = '\0';
	if (strlen(uri) != len)	/* Should not occur */
	    continue;


	if (xmlURIUnescapeString(uri, 0, uri) == NULL)
	    continue;

	int rc = 0;

	len = strlen(uri);
	if (len > sizeof(ext) && streq(uri + len - sizeof(ext) + 1, ext)) {
	    rc = 1;
	    uri[len - sizeof(ext) + 1] = '\0';
	}

	return rc; /* Success */
    }

    return -1; /* Failure */
}

/*
 * Load any extension libraries referenced in the given document.  The
 * implementation is fairly simple: find them and then load them.
 */
void
slaxDynLoad (xmlDocPtr docp)
{
    xmlNodePtr root;
    slax_data_list_t nslist;
    slax_data_node_t *dnp;

    if (docp == NULL)
	return;

    root = xmlDocGetRootElement(docp);
    if (root == NULL)
	return;

    slaxDataListInit(&nslist);

    slaxDynFindNamespaces(&nslist, docp, root, TRUE);

    SLAXDATALIST_FOREACH(dnp, &nslist) {
	slaxDynLoadNamespace(docp, root, (const char *) dnp->dn_data);
    }

    slaxDataListClean(&nslist);
}

int
slaxDynMarkLoaded (const char *ns)
{
    slax_data_node_t *dnp;

    if (streq(ns, SLAX_URI))
	return TRUE;

    SLAXDATALIST_FOREACH(dnp, &slaxDynLoaded) {
	if (streq(ns, dnp->dn_data))
	    return TRUE;		/* Already loaded */
    }

    slaxDataListAddNul(&slaxDynLoaded, ns);
    return FALSE;
}

void
slaxDynMarkExslt (void)
{
    slaxDynMarkLoaded("http://exslt.org/common.ext");
    slaxDynMarkLoaded("http://exslt.org/crypto.ext");
    slaxDynMarkLoaded("http://exslt.org/dates-and-times.ext");
    slaxDynMarkLoaded("http://exslt.org/dynamic.ext");
    slaxDynMarkLoaded("http://exslt.org/functions.ext");
    slaxDynMarkLoaded("http://exslt.org/math.ext");
    slaxDynMarkLoaded("http://exslt.org/sets.ext");
    slaxDynMarkLoaded("http://exslt.org/strings.ext");
    slaxDynMarkLoaded("http://icl.com/saxon.ext");
}

/*
 * Initialize the entire dynamic extension loading mechanism.
 */
void
slaxDynInit (void)
{
    char *cp;

    if (!slaxDynInited) {
	slaxDynInited = TRUE;
	slaxDataListInit(&slaxDynDirList);
    }

    slaxDataListInit(&slaxDynLoaded);
    TAILQ_INIT(&slaxDynLibraries);

    cp = getenv("SLAX_EXTDIR");
    if (cp)
	slaxDynAddPath(cp);

    slaxDataListAddNul(&slaxDynDirList, SLAX_EXTDIR);
}

/*
 * De-initialize the entire dynamic extension loading mechanism.
 */
void
slaxDynClean (void)
{
    slax_dyn_node_t *dnp;

    if (slaxDynInited)
	slaxDataListClean(&slaxDynDirList);
    slaxDataListClean(&slaxDynLoaded);

    if (slaxDynLibraries.tqh_last != NULL) {
	for (;;) {
	    dnp = TAILQ_FIRST(&slaxDynLibraries);
	    if (dnp == NULL)
		break;
	    slax_dyn_arg_t *dap;
	    dap = &dnp->dn_da;
	    if (dap->da_handle) {
		slax_dyn_init_func_t func;

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
		func = (slax_dyn_init_func_t) dlfunc(dap->da_handle,
						     SLAX_DYN_CLEAN_NAME);
		if (func)
		    (*func)(SLAX_DYN_VERSION, dap);

		dlclose(dap->da_handle);

		xmlFreeAndEasy(dap->da_uri);
	    }

	    TAILQ_REMOVE(&slaxDynLibraries, dnp, dn_link);
	    xmlFree(dnp);
	}
    }
}
