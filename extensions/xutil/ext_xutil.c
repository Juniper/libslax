/*
 * $Id$
 *
 * Copyright (c) 2005-2012, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <stdlib.h>
#include <time.h>

#include <sys/queue.h>

#include <libxml/xpathInternals.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlsave.h>

#include <libslax/slaxconfig.h>

#include <libslax/xmlsoft.h>
#include <libxslt/extensions.h>
#include <libslax/slaxdata.h>
#include <libslax/slaxdyn.h>
#include <libslax/slaxext.h>
#include <libslax/slaxio.h>
#include <libslax/slaxinternals.h>
#include <libpsu/psucommon.h>
#include <libpsu/psulog.h>

#include "jsonlexer.h"
#include "jsonwriter.h"

#define XML_FULL_NS "http://xml.libslax.org/xutil"

#define ELT_TYPES	"types"
#define ELT_ROOT	"root"
#define ELT_CLEAN_NAMES	"clean-names"
#define VAL_NO		"no"
#define VAL_YES		"yes"

/*
 * Parse a string into an XML hierarchy:
 *     var $xml = xutil:string-to-xml($string);
 * Multiple strings can be passed in and they are automatically concatenated:
 *     var $xml = xutil:string-to-xml($string1, $string2, $string3);
 */
static void
extXutilStringToXml (xmlXPathParserContext *ctxt, int nargs)
{
    xmlXPathObjectPtr ret = NULL;
    xmlDocPtr xmlp = NULL;
    xmlDocPtr container = NULL;
    xmlNodePtr childp;
    xmlChar *strstack[nargs];	/* Stack for strings */
    int ndx;
    int bufsiz;
    char *buf;

    bzero(strstack, sizeof(strstack));
    for (ndx = nargs - 1; ndx >= 0; ndx--) {
	strstack[ndx] = xmlXPathPopString(ctxt);
    }

    for (bufsiz = 0, ndx = 0; ndx < nargs; ndx++)
	if (strstack[ndx])
	    bufsiz += xmlStrlen(strstack[ndx]);

    buf = alloca(bufsiz + 1);
    buf[bufsiz] = '\0';
    for (bufsiz = 0, ndx = 0; ndx < nargs; ndx++) {
	if (strstack[ndx]) {
	    int len = xmlStrlen(strstack[ndx]);
	    memcpy(buf + bufsiz, strstack[ndx], len);
	    bufsiz += len;
	}
    }

    /* buf now has the complete string */
    xmlp = xmlReadMemory(buf, bufsiz, "raw_data", NULL, XML_PARSE_NOENT);
    if (xmlp == NULL)
	goto bail;

    ret = xmlXPathNewNodeSet(NULL);
    if (ret == NULL)
	goto bail;

    /* Fake an RVT to hold the output of the template */
    container = slaxMakeRtf(ctxt);
    if (container == NULL)
	goto bail;

    /*
     * XXX There should be a way to read the xml input directly
     * into the RTF container.  Lacking that, we copy it.
     */
    childp = xmlDocGetRootElement(xmlp);
    if (childp) {
	xmlNodePtr newp = xmlDocCopyNode(childp, container, 1);
	if (newp) {
	    newp = slaxAddChild((xmlNodePtr) container, newp);
	    if (newp)
		xmlXPathNodeSetAdd(ret->nodesetval, newp);
	}
    }

bail:
    if (ret != NULL)
	valuePush(ctxt, ret);
    else
	valuePush(ctxt, xmlXPathNewNodeSet(NULL));

    if (xmlp)
	xmlFreeDoc(xmlp);

    for (ndx = nargs - 1; ndx >= 0; ndx--) {
	if (strstack[ndx])
	    xmlFree(strstack[ndx]);
    }
}

/*
 * Callback from the libxml2 IO mechanism to build the output string
 */
static int
extXutilRawwriteCallback (void *opaque, const char *buf, int len)
{
    slax_data_list_t *listp = opaque;

    if (listp == NULL)
	return 0;

    slaxDataListAddLen(listp, buf, len);

    return len;
}

/*
 * Encode an XML hierarchy into a string:
 *     var $string = xutil:xml-to-string($xml);
 * Multiple XML hierarchies can be passed in:
 *     var $string = xutil:xml-to-string($xml1, $xml2, $xml3);
 */
static void
extXutilXmlToString (xmlXPathParserContext *ctxt, int nargs)
{
    xmlSaveCtxtPtr handle;
    slax_data_list_t list;
    slax_data_node_t *dnp;
    xmlXPathObjectPtr xop;
    xmlXPathObjectPtr objstack[nargs];	/* Stack for objects */
    int ndx;
    char *buf;
    int bufsiz;
    int hit = 0;

    slaxDataListInit(&list);

    bzero(objstack, sizeof(objstack));
    for (ndx = nargs - 1; ndx >= 0; ndx--) {
	objstack[ndx] = valuePop(ctxt);
	if (objstack[ndx])
	    hit += 1;
    }

    /* If the args are empty, let's get out now */
    if (hit == 0) {
	xmlXPathReturnEmptyString(ctxt);
	return;
    }

    /* We make a custom IO handler to save content into a list of strings */
    handle = xmlSaveToIO(extXutilRawwriteCallback, NULL, &list, NULL,
                 XML_SAVE_FORMAT | XML_SAVE_NO_DECL | XML_SAVE_NO_XHTML);
    if (handle == NULL) {
	xmlXPathReturnEmptyString(ctxt);
	goto bail;
    }

    for (ndx = 0; ndx < nargs; ndx++) {
	xop = objstack[ndx];
	if (xop == NULL)
	    continue;

	if (xop->nodesetval) {
	    xmlNodeSetPtr tab = xop->nodesetval;
	    int i;
	    for (i = 0; i < tab->nodeNr; i++) {
		xmlNodePtr node = tab->nodeTab[i];

		xmlSaveTree(handle, node);
		xmlSaveFlush(handle);
	    }
	}
    }

    xmlSaveClose(handle);	/* This frees is also */

    /* Now we turn the saved data from a linked list into a single string */
    bufsiz = 0;
    SLAXDATALIST_FOREACH(dnp, &list) {
	bufsiz += dnp->dn_len;
    }

    /* If the objects are empty, let's get out now */
    if (bufsiz == 0) {
	xmlXPathReturnEmptyString(ctxt);
	goto bail;
    }

    buf = xmlMalloc(bufsiz + 1);
    if (buf == NULL) {
	xmlXPathReturnEmptyString(ctxt);
	goto bail;
    }
	
    bufsiz = 0;
    SLAXDATALIST_FOREACH(dnp, &list) {
	memcpy(buf + bufsiz, dnp->dn_data, dnp->dn_len);
	bufsiz += dnp->dn_len;
    }
    buf[bufsiz] = '\0';

    valuePush(ctxt, xmlXPathWrapCString(buf));

 bail:
    slaxDataListClean(&list);

    for (ndx = 0; ndx < nargs; ndx++)
	xmlXPathFreeObject(objstack[ndx]);
}

/*
 * Callback from the libxml2 IO mechanism to build the output string
 */
static int
extXutilWriteCallback (void *opaque, const char *fmt, ...)
{
    slax_data_list_t *listp = opaque;
    char buf[BUFSIZ];
    char *cp = buf;
    size_t len;
    va_list vap;

    if (listp == NULL)
	return 0;

    va_start(vap, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, vap);
    if (len >= sizeof(buf)) {
	va_end(vap);
	va_start(vap, fmt);
	if (vasprintf(&cp, fmt, vap) < 0 || cp == NULL)
	    return -1;
    }
    va_end(vap);

    slaxDataListAddLen(listp, cp, len);

    if (cp != buf)
	free(cp);   /* Allocated by vasprintf() */

    return len;
}

static void
extXutilXmlToJson (xmlXPathParserContext *ctxt UNUSED, int nargs UNUSED)
{
    int i;
    xmlXPathObject *xop;
    const char *value, *key;
    slax_data_list_t list;
    slax_data_node_t *dnp;
    char *buf;
    int bufsiz;
    unsigned flags = 0;

    if (nargs < 1 || nargs > 2) {
	xmlXPathSetArityError(ctxt);
	return;
    }

    if (nargs == 2) {
	xop = valuePop(ctxt);
	if (!xop->nodesetval || !xop->nodesetval->nodeNr) {
	    LX_ERR("xml-to-json: invalid second parameter\n");
	    xmlXPathFreeObject(xop);
	    xmlXPathReturnEmptyString(ctxt);
	    return;
	}

	for (i = 0; i < xop->nodesetval->nodeNr; i++) {
	    xmlNodePtr nop, cop;

	    nop = xop->nodesetval->nodeTab[i];
	    if (nop->children == NULL)
		continue;

	    for (cop = nop->children; cop; cop = cop->next) {
		if (cop->type != XML_ELEMENT_NODE)
		    continue;

		key = xmlNodeName(cop);
		if (!key)
		    continue;
		value = xmlNodeValue(cop);
		if (streq(key, ELT_PRETTY)) {
		    flags |= JWF_PRETTY;
		} else if (streq(key, ELT_QUOTES)) {
		    if (streq(value, VAL_OPTIONAL))
			flags |= JWF_OPTIONAL_QUOTES;
		}
	    }
	}

	xmlXPathFreeObject(xop);
    }

    xop = valuePop(ctxt);
    if (!xop->nodesetval || !xop->nodesetval->nodeNr) {
	LX_ERR("xml-to-json: invalid parameter\n");
	xmlXPathFreeObject(xop);
	xmlXPathReturnEmptyString(ctxt);
	return;
    }

    slaxDataListInit(&list);

    for (i = 0; i < xop->nodesetval->nodeNr; i++) {
	xmlNodePtr nop;

	nop = xop->nodesetval->nodeTab[i];
	if (nop->type == XML_DOCUMENT_NODE)
	    nop = nop->children;
	if (nop->type != XML_ELEMENT_NODE)
	    continue;

	slaxJsonWriteNode(extXutilWriteCallback, &list, nop, flags);
    }

    /* Now we turn the saved data from a linked list into a single string */
    bufsiz = 0;
    SLAXDATALIST_FOREACH(dnp, &list) {
	bufsiz += dnp->dn_len;
    }

    /* If the objects are empty, let's get out now */
    if (bufsiz == 0) {
	xmlXPathReturnEmptyString(ctxt);
	goto bail;
    }

    buf = xmlMalloc(bufsiz + 1);
    if (buf == NULL) {
	xmlXPathReturnEmptyString(ctxt);
	goto bail;
    }
	
    bufsiz = 0;
    SLAXDATALIST_FOREACH(dnp, &list) {
	memcpy(buf + bufsiz, dnp->dn_data, dnp->dn_len);
	bufsiz += dnp->dn_len;
    }
    buf[bufsiz] = '\0';

    valuePush(ctxt, xmlXPathWrapCString(buf));

 bail:
    xmlXPathFreeObject(xop);
}

static void
extXutilJsonToXml (xmlXPathParserContext *ctxt UNUSED, int nargs UNUSED)
{
    xmlXPathObjectPtr ret = NULL;
    unsigned flags = 0;
    xmlDocPtr docp = NULL;
    xmlDocPtr container = NULL;
    xmlNodePtr childp;
    int i;
    const char *value, *key;
    char *root_name = NULL;

    if (nargs < 1 || nargs > 2) {
	xmlXPathSetArityError(ctxt);
	return;
    }

    if (nargs == 2) {
	xmlXPathObject *xop = valuePop(ctxt);
	if (!xop->nodesetval || !xop->nodesetval->nodeNr) {
	    LX_ERR("json-to-xml: invalid second parameter\n");
	    xmlXPathFreeObject(xop);
	    xmlXPathReturnEmptyString(ctxt);
	    return;
	}

	for (i = 0; i < xop->nodesetval->nodeNr; i++) {
	    xmlNodePtr nop, cop;

	    nop = xop->nodesetval->nodeTab[i];
	    if (nop->children == NULL)
		continue;

	    for (cop = nop->children; cop; cop = cop->next) {
		if (cop->type != XML_ELEMENT_NODE)
		    continue;

		key = xmlNodeName(cop);
		if (!key)
		    continue;
		value = xmlNodeValue(cop);
		if (streq(key, ELT_TYPES)) {
		    if (streq(value, VAL_NO))
			flags |= SDF_NO_TYPES;
		} else if (streq(key, ELT_ROOT)) {
		    root_name = xmlStrdup2(value);
		} else if (streq(key, ELT_CLEAN_NAMES)) {
		    if (streq(value, VAL_YES))
			flags |= SDF_CLEAN_NAMES;
		}
	    }
	}

	xmlXPathFreeObject(xop);
    }

    char *json = (char *) xmlXPathPopString(ctxt);
    if (json == NULL)
	goto bail;

    docp = slaxJsonDataToXml(json, root_name, flags);
    if (docp == NULL)
	goto bail;
    
    ret = xmlXPathNewNodeSet(NULL);
    if (ret == NULL)
	goto bail;

    /* Fake an RVT to hold the output of the template */
    container = slaxMakeRtf(ctxt);
    if (container == NULL)
	goto bail;

    /*
     * XXX There should be a way to read the xml input directly
     * into the RTF container.  Lacking that, we copy it.
     */
    childp = xmlDocGetRootElement(docp);
    xmlNodePtr newp = xmlDocCopyNode(childp, container, 1);
    if (newp) {
	newp = xmlAddChild((xmlNodePtr) container, newp);
	if (newp)
	    xmlXPathNodeSetAdd(ret->nodesetval, newp);
    }

bail:
    if (root_name != NULL)
	xmlFree(root_name);

    if (ret != NULL)
	valuePush(ctxt, ret);
    else
	valuePush(ctxt, xmlXPathNewNodeSet(NULL));

    xmlFreeAndEasy(json);
    if (docp)
	xmlFreeDoc(docp);
}


/*
 * Implement slax-to-xml:
 *
 *     node slax:slax-to-xml(string);
 * where:
 * - string is a hierarchy of SLAX data
 */
static void
extXutilSlaxToXml (xmlXPathParserContext *ctxt, int nargs)
{
    if (nargs != 1) {
	xmlXPathSetArityError(ctxt);
	return;
    }
   
    char *str = (char *) xmlXPathPopString(ctxt);

    if (str == 0) {
	xmlXPathReturnEmptyString(ctxt);
	return;
    }
    
    xmlDocPtr docp = slaxLoadBuffer("slax-to-xml", str, NULL, TRUE);
    if (docp) {
	xmlNodePtr nodep;

	nodep = docp->children;
	valuePush(ctxt, xmlXPathNewNodeSet(nodep));
    }
}

/*
 * Implement xml-to-slax:
 *
 *     string slax:xml-to-slax(node, version?);
 * where:
 * - node is a hierarchy of XML data
 * - "version" is a version string (e.g. "1.3"), similar to --write-version
 */
static void
extXutilXmlToSlax (xmlXPathParserContext *ctxt UNUSED, int nargs UNUSED)
{
    int i;
    slax_printf_buffer_t pb;
    int done = FALSE;

    bzero(&pb, sizeof(pb));

    if (nargs < 1 || nargs > 2) {
	xmlXPathSetArityError(ctxt);
	return;
    }

    char *version = NULL;

    if (nargs == 2)
	version = (char *) xmlXPathPopString(ctxt);

    xmlXPathObjectPtr xop = valuePop(ctxt);
    if (xop == NULL)
	return;

    if (xop->nodesetval) {
	xmlNodeSetPtr tab = xop->nodesetval;

	for (i = 0; !done && i < tab->nodeNr; i++) {
	    xmlNodePtr nodep = tab->nodeTab[i];

	    if (nodep->type == XML_DOCUMENT_NODE) {
		if (slaxWriteDoc(slaxExtPrintWriter, &pb, (xmlDocPtr) nodep,
				 TRUE, version) == 0)
		    done = TRUE;

	    } else {
		if (slaxWriteNode(slaxExtPrintWriter, &pb, nodep,
				  version) == 0)
		    done = TRUE;
	    }
	}
    }

    xmlFreeAndEasy(version);
    xmlXPathReturnString(ctxt, (xmlChar *) pb.pb_buf);
}

/*
 * Adjust the max call depth, the limit of recursion in libxml2:
 *     expr xutil:max-call-depth(5000);
 */
static void
extXutilMaxCallDepth (xmlXPathParserContext *ctxt, int nargs)
{
    int value;

    if (nargs == 0) {
	xmlXPathReturnNumber(ctxt, xsltGetMaxDepth());
	return;
    }

    value = xmlXPathPopNumber(ctxt);
    if (xmlXPathCheckError(ctxt))
	return;

    if (value > 0) {
#if LIBXSLT_VERSION >= 10127
	xsltTransformContextPtr tctxt;

	/* Set the value for _our_ transform context */
	tctxt = xsltXPathGetTransformContext(ctxt);
	if (tctxt)
	    tctxt->maxTemplateDepth = value;
#endif /* LIBXSLT_VERSION >= 10127 */

	/* Set the value globally */
	xsltSetMaxDepth(value);
    } else {
        LX_ERR("xutil:max-call-depth invalid parameter\n");
        return;
    }

    xmlXPathReturnEmptyString(ctxt);
}

/*
 * EXSLT says:
 *
 *    The set:difference function returns the difference between two
 *    node sets - those nodes that are in the node set passed as the
 *    first argument that are not in the node set passed as the second
 *    argument.
 *
 * But this turns out to be mostly useless, since we don't care about
 * the _specific_ nodes, but the contents of the nodes.  So we make
 * our own set functions, tucked away in the xutil extension library.
 */

static int
extXutilSetsCheckOneValue (xmlNodePtr nop, xmlXPathObjectPtr xop)
{
    slaxLog("extXutilSetsCheckOneValue: %p %p", nop, xop);

    if (nop->type != XML_ELEMENT_NODE)
	return FALSE;

    xmlNodePtr cop = nop->children;
    if (cop == NULL || cop->prev != NULL
	|| cop->next != NULL || cop->content == NULL
	|| cop->type != XML_TEXT_NODE)
	return FALSE;

    const char *cp = (const char *) cop->content;
    char buf[64];

    switch (xop->type) {
    case XPATH_BOOLEAN:
	return streq(cp, xop->floatval ? "true" : "false");

    case XPATH_NUMBER:
	snprintf(buf, sizeof(buf), "%ld", (long) xop->floatval);
	return streq(cp, buf);

    case XPATH_STRING:
	return streq(cp, (const char *) xop->stringval);

    default:
	return FALSE;
    }
}

static int
extXutilEqual (const xmlChar *a1, const xmlChar *a2)
{
    const char *s1 = (const char *) a1;
    const char *s2 = (const char *) a2;
 
    if (s1) {
	if (s2) {
	    if (!streq(s1, s2))
		return FALSE;
	} else
	    return FALSE;	/* s1 && !s2 */

    } else if (s2)
	return FALSE;		/* !s1 && s2 */

    return TRUE;
}

static int
extXutilIsWsNode (xmlNodePtr nop)
{
    if (nop == NULL || nop->type != XML_TEXT_NODE)
	return FALSE;

    /* Detect the case where we're the only text node of an element */
    if (nop->next == NULL) {
	if (nop->prev == NULL)
	    return FALSE;	/* A text node that is the only child */

	xmlElementType type = nop->prev->type;
	if (type != XML_ELEMENT_NODE)
	    return FALSE;	/* Only ns/attr/etc in front of us */
    }

    const char *cp = (const char *) nop->content;
    if (cp == NULL)
	return FALSE;

    for ( ; *cp; cp++)
	if (!isspace((int) *cp))
	    return FALSE;	/* Found a non-whitespace character */

    return TRUE;		/* Meets all our criteria */
}

static xmlNodePtr
extXutilSkipWs (xmlNodePtr nop, int ignore_ws)
{
    if (!ignore_ws)
	return nop;

    while (nop) {
	if (!extXutilIsWsNode(nop))
	    break;

	nop = nop->next;
    }

    return nop;
}

static int
extXutilSetsCheckOneNode (xmlNodePtr n1, xmlNodePtr n2, int ignore_ws)
{
    slaxLog("extXutilSetsCheckOneNode: %p %p %d", n1, n2, ignore_ws);

    if (n1->type != n2->type)
	return FALSE;

    if (!extXutilEqual(n1->name, n2->name))
	return FALSE;

    if (n1->type == XML_TEXT_NODE && !extXutilEqual(n1->content, n2->content))
	return FALSE;

    if ((n1->ns == NULL) != (n2->ns == NULL))
	return FALSE;

    if (n1->type && XML_ELEMENT_NODE) {
	if (n1->ns && !extXutilEqual(n1->ns->href, n2->ns->href))
	    return FALSE;
    
	if ((n1->children == NULL) != (n2->children == NULL))
	    return FALSE;

	if (n1->children) {
	    xmlNodePtr c1 = extXutilSkipWs(n1->children, ignore_ws);
	    xmlNodePtr c2 = extXutilSkipWs(n2->children, ignore_ws);

	    for ( ; c1 && c2; ) {
		if (!extXutilSetsCheckOneNode(c1, c2, ignore_ws))
		    return FALSE;

		c1 = extXutilSkipWs(c1->next, ignore_ws);
		c2 = extXutilSkipWs(c2->next, ignore_ws);
	    }

	    if (c1 != c2)	/* Leftover on one side or the other */
		return FALSE;
	}
    }

    return TRUE;
}

static int
extXutilSetsCheckNode (xmlNodeSet *results, xmlNodePtr nop,
		       xmlXPathObjectPtr *list, int count, int common)
{
    slaxLog("extXutilSetsCheckNode: %p %p %p %d %d",
	       results, nop, list, count, common);

    xmlXPathObjectPtr xop;
    xmlNodeSetPtr tab;
    int i;
    int match = FALSE;

    for (int ndx = 0; ndx < count; ndx++) {
	xop = list[ndx];

	switch (xop->type) {
	case XPATH_NODESET:
	    if (xop->nodesetval == NULL)
		break;

	    tab = xop->nodesetval;
	    for (i = 0; i < tab->nodeNr; i++) {
		match = extXutilSetsCheckOneNode(nop, tab->nodeTab[i], TRUE);
		if (match)
		    break;
	    }

	    break;

	case XPATH_XSLT_TREE:
	    if (xop->nodesetval == NULL)
		break;

	    /* It's a tree, so there should only be one node, but...
	     * We need to look at the children of the tree, not the
	     * root
	     */
	    tab = xop->nodesetval;
	    for (i = 0; i < tab->nodeNr; i++) {
		xmlNode *gop, *cop =  tab->nodeTab[i];
		for (gop = cop->children; gop; gop = gop->next) {
		    match = extXutilSetsCheckOneNode(nop, gop, TRUE);
		    if (match)
			break;
		}
	    }
	    break;

	case XPATH_BOOLEAN:
	case XPATH_NUMBER:
	case XPATH_STRING:
	    match = extXutilSetsCheckOneValue(nop, xop);
	    break;

	default:
	    continue;
	}

	slaxLog("extXutilSetsCheckNode: inner %d %d",
		   common, match);

	if (match)
	    break;
    }

    slaxLog("extXutilSetsCheckNode: %d %d -> %d",
	       common, match, (!common == !match) ? 1 : 0);

    if (!common == !match)
	xmlXPathNodeSetAdd(results, nop);

    return 0;
}

/*
 * common: return the set of nodes that appear in all node sets.
 *
 *    node-set xutil:common(ns1, ns2, ...)
 */
static void
extXutilCheck (xmlXPathParserContext *ctxt, int nargs, int common)
{
    if (nargs < 2) {
	xmlXPathSetArityError(ctxt);
	return;
    }

    xmlXPathObjectPtr objstack[nargs];	/* Stack for objects */
    xmlXPathObjectPtr xop;
    int ndx;

    bzero(objstack, sizeof(objstack));
    int count = 0;
    for (ndx = nargs - 2; ndx >= 0; ndx--) {
	xop = valuePop(ctxt);
	if (xop == NULL)
	    continue;
	objstack[ndx] = xop;
	count += 1;
    }

    xmlNodeSet *results = xmlXPathNodeSetCreate(NULL);
    xmlNodeSetPtr tab;
    int i;

    /* First argument (last popped) is the 'base' we compare against */
    xmlXPathObjectPtr base = valuePop(ctxt);

    switch (base->type) {
    case XPATH_NODESET:
	if (base->nodesetval == NULL)
	    break;

	tab = base->nodesetval;
	for (i = 0; i < tab->nodeNr; i++) {
	    extXutilSetsCheckNode(results, tab->nodeTab[i],
				  objstack, count, common);
	}
	break;

    case XPATH_XSLT_TREE:
	if (base->nodesetval == NULL)
	    break;

	/* It's a tree, so there should only be one node, but...
	 * We need to look at the children of the tree, not the
	 * root
	 */
	tab = base->nodesetval;
	for (i = 0; i < tab->nodeNr; i++) {
	    xmlNode *cop, *nop =  tab->nodeTab[i];
	    for (cop = nop->children; cop; cop = cop->next)
		extXutilSetsCheckNode(results, cop, objstack, count, common);
	}
	break;

    default:
	break;
    }

    /* Wrap up our nodeset and return it */
    valuePush(ctxt, xmlXPathWrapNodeSet(results));
}

/*
 * common: return the set of nodes that appear in all node sets.
 *
 *    node-set xutil:common(ns1, ns2, ...)
 */
static void
extXutilCommon (xmlXPathParserContext *ctxt, int nargs)
{
    extXutilCheck(ctxt, nargs, TRUE);
}

/*
 * distinct: return the set of nodes that appear in the first
 * node sets that do not appear in the other nodesets.
 *
 *    node-set xutil:distinct(base, ns2, ...)
 */
static void
extXutilDistinct (xmlXPathParserContext *ctxt, int nargs)
{
    extXutilCheck(ctxt, nargs, FALSE);
}

slax_function_table_t slaxXutilTable[] = {
    {
	"xml-to-string", extXutilXmlToString,
	"Encodes XML hierarchies as text strings",
	"(xml-data)", XPATH_STRING,
    },
    {
	"string-to-xml", extXutilStringToXml,
	"Decodes data from strings into XML nodes",
	"(string)", XPATH_XSLT_TREE,
    },
    {
	"xml-to-json", extXutilXmlToJson,
	"Encodes XML hierarchies as JSON data",
	"(xml-data)", XPATH_STRING,
    },
    {
	"json-to-xml", extXutilJsonToXml,
	"Decodes data from JSON into XML nodes",
	"(string)", XPATH_XSLT_TREE,
    },
    {
	"max-call-depth", extXutilMaxCallDepth,
	"Sets the maximum call depth for recursion in the XSLT engine",
	"(call-depth)", XPATH_UNDEFINED,
    },
    {
	"xml-to-slax", extXutilXmlToSlax,
	"Converts an XML hierarchy into a string of SLAX syntax",
	"(data, version-string)", XPATH_STRING,
    },
    {
	"slax-to-xml", extXutilSlaxToXml,
	"Converts a string containing a SLAX hierarchy into an XML hierarchy",
	"(string)", XPATH_STRING,
    },
    {
	"common", extXutilCommon,
	"Return the nodes in the first nodeset that appear in any others",
	"(ns1, ns2, ...)", XPATH_NODESET,
    },
    {
	"distinct", extXutilDistinct,
	"Return the nodes that appear only in the first nodeset",
	"(ns1, ns2, ...)", XPATH_NODESET,
    },
    { NULL, NULL, NULL, NULL, XPATH_UNDEFINED }
};

SLAX_DYN_FUNC(slaxDynLibInit)
{
    arg->da_functions = slaxXutilTable; /* Fill in our function table */

    return SLAX_DYN_VERSION;
}
