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

#include "config.h"

#include <libxslt/extensions.h>
#include <libslax/slaxdata.h>
#include <libslax/slaxdyn.h>
#include <libslax/slaxio.h>
#include <libslax/xmlsoft.h>
#include <libslax/slaxutil.h>

#define XML_FULL_NS "http://xml.libslax.org/xml"

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

    valuePush(ctxt, xmlXPathWrapCString(buf));

 bail:
    slaxDataListClean(&list);

    for (ndx = 0; ndx < nargs; ndx++)
	xmlXPathFreeObject(objstack[ndx]);
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

    if (value > 0)
	xsltSetMaxDepth(value);

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
	"Decodes XML data from strings into nodes",
	"(string)", XPATH_XSLT_TREE,
    },
    {
	"max-call-depth", extXutilMaxCallDepth,
	"Sets the maximum call depth for recursion in the XSLT engine",
	"(call-depth)", XPATH_UNDEFINED,
    },
    { NULL, NULL, NULL, NULL, XPATH_UNDEFINED }
};

SLAX_DYN_FUNC(slaxDynLibInit)
{
    arg->da_functions = slaxXutilTable; /* Fill in our function table */

    return SLAX_DYN_VERSION;
}
