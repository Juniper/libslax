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
	    xmlAddChild((xmlNodePtr) container, newp);
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
	xmlAddChild((xmlNodePtr) container, newp);
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
    { NULL, NULL, NULL, NULL, XPATH_UNDEFINED }
};

SLAX_DYN_FUNC(slaxDynLibInit)
{
    arg->da_functions = slaxXutilTable; /* Fill in our function table */

    return SLAX_DYN_VERSION;
}
