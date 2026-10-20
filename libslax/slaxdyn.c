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
#include <libxml/xpathInternals.h>

#include "slaxinternals.h"
#include <libslax/slax.h>
#include "slaxdata.h"

#include "slaxdyn.h"

#include <libxo/xo.h>
#include <libpin/pin_dyn.h>

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

/*
 * The bit: extension's URI, duplicated here (rather than shared via a
 * header) because extensions/bit is a separately built module; keep
 * this in sync with ext_bit.c's private URI_BIT.
 */
#define SLAXDYN_BIT_URI "http://xml.libslax.org/bit"

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
 * A single, generic adapter that lets libpin's xo_filter-based
 * evaluator call an xmlXPathFunction -- the calling convention shared
 * by libxslt's builtin functions and dynamically loaded extension
 * functions -- through the xo_eval_ext_fn_t signature.  Each call is
 * scalar in, scalar out: string arguments go in, and a single number,
 * boolean, or string comes back.
 *
 * slaxPinFindFunction() resolves a name to an xmlXPathFunction and
 * stashes it here immediately before handing back this adapter, so
 * the same adapter can be reused for every function it resolves.
 */
static xmlXPathFunction slaxPinScalarFunc;

/*
 * A single buffer, reused (and grown as needed) across calls, used to
 * hand back XO_EVAL_EXT_STRING results.  The caller consumes the
 * returned xee_str before this adapter is called again, so reusing
 * one buffer rather than allocating a fresh one per call is safe.
 */
static char *slaxPinScalarBuf;
static size_t slaxPinScalarBufMax;

static const char *
slaxPinScalarSaveString (const char *str)
{
    size_t len = strlen(str) + 1;

    if (len > slaxPinScalarBufMax) {
	slaxPinScalarBuf = xmlRealloc(slaxPinScalarBuf, len);
	slaxPinScalarBufMax = len;
    }

    memcpy(slaxPinScalarBuf, str, len);
    return slaxPinScalarBuf;
}

static xo_eval_ext_value_t
slaxPinScalarCall (xo_filter_data_t *dp UNUSED, int argc, const char **argv)
{
    xo_eval_ext_value_t rv;
    xmlXPathContext *ctxt;
    xmlXPathParserContext *pctxt;
    xmlXPathObject *obj;
    int i;

    bzero(&rv, sizeof(rv));
    rv.xee_type = XO_EVAL_EXT_MISSING;

    if (slaxPinScalarFunc == NULL)
	return rv;

    ctxt = xmlXPathNewContext(NULL);
    if (ctxt == NULL)
	return rv;

    pctxt = xmlXPathNewParserContext(NULL, ctxt);
    if (pctxt == NULL) {
	xmlXPathFreeContext(ctxt);
	return rv;
    }

    /*
     * xmlXPathNewParserContext() leaves valueTab/valueMax at zero,
     * and valuePush()'s grow-from-zero path does not preserve the
     * entries already on the stack when it reallocates (confirmed
     * against libxml2 2.13.5: each push after the first silently
     * drops everything pushed before it).  Extension functions are
     * normally only ever invoked by libxml2's own compiled-expression
     * evaluator, which pre-sizes this stack itself, so this path is
     * never exercised there.  Preallocating enough room up front
     * (every argument, plus headroom for the function's own return
     * push) keeps every push inside valuePush()'s "already have
     * room" fast path, avoiding the buggy growth path entirely.
     */
    pctxt->valueTab = (xmlXPathObject **)
	xmlMalloc(sizeof(xmlXPathObject *) * (argc + 4));
    if (pctxt->valueTab == NULL) {
	xmlXPathFreeParserContext(pctxt);
	xmlXPathFreeContext(ctxt);
	return rv;
    }
    pctxt->valueMax = argc + 4;

    for (i = 0; i < argc; i++)
	valuePush(pctxt, xmlXPathNewCString(argv[i]));

    (*slaxPinScalarFunc)(pctxt, argc);

    if (pctxt->error == XPATH_EXPRESSION_OK) {
	obj = valuePop(pctxt);
	if (obj) {
	    switch (obj->type) {
	    case XPATH_NUMBER:
		rv.xee_type = XO_EVAL_EXT_NUMBER;
		rv.xee_num = obj->floatval;
		break;

	    case XPATH_BOOLEAN:
		rv.xee_type = XO_EVAL_EXT_BOOLEAN;
		rv.xee_bool = obj->boolval;
		break;

	    default: {
		xmlChar *str = xmlXPathCastToString(obj);
		if (str) {
		    rv.xee_type = XO_EVAL_EXT_STRING;
		    rv.xee_str = slaxPinScalarSaveString((const char *) str);
		    if (str != obj->stringval)
			xmlFree(str);
		}
		break;
	    }
	    }

	    xmlXPathFreeObject(obj);
	}
    }

    xmlXPathFreeParserContext(pctxt);
    xmlXPathFreeContext(ctxt);

    return rv;
}

/*
 * Scan the retained slax_function_table_t for the dynamically loaded
 * module registered under "uri" (if any) for "name", returning its
 * recorded arity.  Returns 0 and fills nargsp/min_argsp when found,
 * -1 (untouched) otherwise.
 */
static int
slaxDynFindArity (const char *uri, const char *name,
		   int *nargsp, int *min_argsp,
		   const char **descp, const char **arghelpp)
{
    slax_dyn_node_t *dnp;
    slax_function_table_t *ftp;

    TAILQ_FOREACH(dnp, &slaxDynLibraries, dn_link) {
	slax_dyn_arg_t *dap = &dnp->dn_da;

	if (dap->da_uri == NULL || !streq(dap->da_uri, uri))
	    continue;

	for (ftp = dap->da_functions; ftp && ftp->ft_name; ftp++) {
	    if (streq(ftp->ft_name, name)) {
		*nargsp = ftp->ft_nargs;
		*min_argsp = ftp->ft_min_args;
		if (descp)
		    *descp = ftp->ft_desc;
		if (arghelpp)
		    *arghelpp = ftp->ft_arghelp;
		return 0;
	    }
	}
    }

    return -1;
}

/*
 * Look up the recorded arity for "local" under "uri", for the subset
 * of namespaces we have real arity data for (slax: and bit:).  Every
 * other namespace -- including the other dynamically loaded modules
 * (os:, db:, curl:, xutil:), whose tables don't yet carry real
 * ft_nargs/ft_min_args values -- falls through to "not found", which
 * slaxPinFindFunction() turns into the unchecked -1/-1.
 */
static int
slaxPinFindArity (const char *local, const char *uri,
		   int *nargsp, int *min_argsp,
		   const char **descp, const char **arghelpp)
{
    if (streq(uri, SLAX_URI))
	return slaxExtFindArity(local, nargsp, min_argsp, descp, arghelpp);

    if (streq(uri, SLAXDYN_BIT_URI))
	return slaxDynFindArity(uri, local, nargsp, min_argsp,
				 descp, arghelpp);

    return -1;
}

/*
 * pdo_find_function: resolve "local" under "uri" to an xmlXPathFunction
 * (loading the extension module for "uri" if needed) and hand back the
 * scalar-call adapter in its place.  Arity is consulted for slax: and
 * bit:, via slaxPinFindArity(); every other namespace gets the
 * unchecked -1/-1 (not checked).
 */
static xo_eval_ext_fn_t
slaxPinFindFunction (const char *local, const char *uri,
		      int *nargsp, int *min_argsp,
		      const char **descp, const char **arghelpp)
{
    xmlXPathFunction func;

    slaxDynLoadNamespace(NULL, NULL, uri);

    func = xsltExtModuleFunctionLookup((const xmlChar *) local,
					(const xmlChar *) uri);
    if (func == NULL)
	return NULL;

    slaxPinScalarFunc = func;

    if (slaxPinFindArity(local, uri, nargsp, min_argsp,
			  descp, arghelpp) < 0) {
	*nargsp = -1;
	*min_argsp = -1;
	if (descp)
	    *descp = NULL;
	if (arghelpp)
	    *arghelpp = NULL;
    }

    return slaxPinScalarCall;
}

/*
 * pdo_resolve_prefix: resolve a "well-known" prefix (one of the
 * extension directory's ".prefix" symlinks) to its URI.
 */
static const char *
slaxPinResolvePrefix (const char *prefix)
{
    static char buf[MAXPATHLEN];

    if (slaxDynFindPrefix(buf, sizeof(buf), prefix) < 0)
	return NULL;

    return buf;
}

static const pin_dyn_ops_t slaxPinDynOps = {
    .pdo_version = PIN_DYN_OPS_VERSION,
    .pdo_resolve_prefix = slaxPinResolvePrefix,
    .pdo_find_function = slaxPinFindFunction,
};

/*
 * Register (or, with 'enable' FALSE, unregister) libpin's dynamic
 * function-lookup provider.  Called from slaxEnable().
 */
void
slaxDynSetPinOps (int enable)
{
    pin_set_dyn_ops(enable ? &slaxPinDynOps : NULL);
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
