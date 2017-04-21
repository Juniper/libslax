/*
 * Copyright (c) 2013, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * jsonlexer.c -- lex json input into lexical tokens
 *
 * This file contains the lexer for json.
 *
 * See json.org and RFC 4627 for details, but note that these
 * two sources violently disagree with each other and with
 * common usage.
 *
 * json -> object | array
 * object -> '{' member-list-or-empty '}'
 * member-list-or-empty -> empty | member-list
 * member-list -> pair | pair ',' member-list
 * pair -> name ':' value
 * value -> string | number | object | array | 'true' | 'false' | 'null'
 * array -> '[' element-list-or-empty ']'
 * element-list-or-empty -> empty | element-list
 * element-list -> value | value ',' element-list
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/xmlsave.h>

#include <libxslt/documents.h>

#include <libslax/slax.h>
#include <libpsu/psustring.h>
#include "slaxinternals.h"
#include "slaxparser.h"
#include "jsonlexer.h"
#include "jsonwriter.h"

static int slaxJsonDoTagging;

int
slaxJsonTagging (int tagging)
{
    int was = slaxJsonDoTagging;
    slaxJsonDoTagging = tagging;
    return was;
}

void
slaxJsonElementOpen (slax_data_t *sdp, const char *name)
{
    int valid = xmlValidateName((const xmlChar *) name, FALSE);
    const char *element = valid ? ELT_ELEMENT : name;

    slaxElementOpen(sdp, element);
    if (element != name)
	slaxAttribAddLiteral(sdp, ATT_NAME, name);
}

static int
extXutilValidStartChar (int ch)
{
    if (ch == '_' || isalpha(ch))
	return TRUE;
    return FALSE;
}

static int
extXutilValidChar (int ch)
{
    if (ch == '_' || ch == '.' || isalnum(ch))
	return TRUE;
    return FALSE;
}

void
slaxJsonElementOpenName (slax_data_t *sdp, char *name)
{
    char *s = name;

    if (sdp->sd_flags & SDF_CLEAN_NAMES) {
	if (!extXutilValidStartChar((int) *s))
	    *s = '_';

	for (s = name; *s; s++) {
	    if (!extXutilValidChar((int) *s))
		*s = '_';
	}
    }

    slaxJsonElementOpen(sdp, name);
}

void
slaxJsonElementValue (slax_data_t *sdp, slax_string_t *value)
{
    xmlNodePtr nodep;
    char *cp = value->ss_token;

    nodep = xmlNewText((const xmlChar *) cp);
    if (nodep)
	slaxAddChild(sdp, NULL, nodep);
}

static xmlDocPtr
slaxJsonBuildDoc (slax_data_t *sdp UNUSED, const char *root_name,
		      xmlParserCtxtPtr ctxt)
{
    xmlDocPtr docp;
    xmlNodePtr nodep;

    docp = xmlNewDoc((const xmlChar *) XML_DEFAULT_VERSION);
    if (docp == NULL)
	return NULL;

    docp->standalone = 1;
    if (ctxt->dict)
	docp->dict = ctxt->dict;
    else {
	docp->dict = xmlDictCreate();
	if (ctxt->dict == NULL)
	    ctxt->dict = docp->dict;
    }

    if (root_name == NULL)
	root_name = ELT_JSON;

    nodep = xmlNewDocNode(docp, NULL, (const xmlChar *) root_name, NULL);
    if (nodep) {
	xmlDocSetRootElement(docp, nodep);
	nodePush(ctxt, nodep);
    }

    if (ctxt->dict) {
	docp->dict = ctxt->dict;
	xmlDictReference(docp->dict);
    }

    return docp;
}

void
slaxJsonAddTypeInfo (slax_data_t *sdp, const char *value)
{
    if (!(sdp->sd_flags & SDF_NO_TYPES)) {
	xmlSetProp(sdp->sd_ctxt->node, (const xmlChar *) ATT_TYPE,
	       (const xmlChar *) value);
    }
}

void
slaxJsonClearMember (slax_data_t *sdp)
{
    xmlNodePtr nodep = sdp->sd_ctxt->node;

    slaxElementClose(sdp);

    if (nodep->children == NULL) {
	/* Discard empty member */
	xmlUnlinkNode(nodep);
	xmlFreeNode(nodep);
    }

    /*
     * If we're not using the <member> element, then we need to turn
     * array memory into direct children.  Move our children to predecessors
     * and remove the parent.
     */
    if (sdp->sd_flags & SDF_JSON_NO_MEMBERS) {
	xmlNodePtr parent = sdp->sd_ctxt->node, childp, nextp;

	for (childp = parent->children; childp; childp = nextp) {
	    nextp = childp->next;
	    /* XXX Underimplemented */
	}

	slaxElementClose(sdp);
    }
}

int
slaxJsonIsTaggedNode (xmlNodePtr nodep)
{
    const char *json = NULL;
    xmlAttrPtr attrp;

    for (attrp = nodep->properties; attrp; attrp = attrp->next) {
	if (attrp->children && attrp->children->content) {
	    char *content = (char *) attrp->children->content;

	    if (streq((const char *) attrp->name, ATT_JSON)) {
		json = content;
	    } else if (streq((const char *) attrp->name, ATT_TYPE)) {
		/* nothing; skip */
	    } else if (streq((const char *) attrp->name, ATT_NAME)) {
		/* nothing; skip */
	    } else {
		return FALSE;
	    }
	}
    }

    return (json != NULL);
}

int
slaxJsonIsTagged (slax_data_t *sdp)
{
    return slaxJsonIsTaggedNode(sdp->sd_ctxt->node);
}

void
slaxJsonTag (slax_data_t *sdp)
{
    if (!slaxJsonDoTagging)
	return;

    if (slaxJsonIsTagged(sdp))
	return;

    slaxAttribAddLiteral(sdp, ATT_JSON, ATT_JSON);
}

void
slaxJsonTagContent (slax_data_t *sdp, int content)
{
    const char *tag = NULL;

    if (!slaxJsonIsTagged(sdp))
	return;

    switch (content) {
    case K_FALSE:
	tag = VAL_FALSE;
	break;

    case M_JSON:
	tag = VAL_JSON;
	break;

    case M_XPATH:
	tag = VAL_XPATH;
    }

    if (tag)
	slaxAttribAddLiteral(sdp, ATT_TYPE, tag);
}

xmlDocPtr
slaxJsonDataToXml (const char *data, const char *root_name, unsigned flags)
{
    slax_data_t sd;
    xmlDocPtr res;

    slaxSetupLexer();

    xmlParserCtxtPtr ctxt = xmlNewParserCtxt();

    if (ctxt == NULL)
	return NULL;

    /*
     * Turn on line number recording in each node
     */
    ctxt->linenumbers = 1;

    bzero(&sd, sizeof(sd));

    sd.sd_buf = xmlStrdup2(data);
    sd.sd_len = strlen(data);
    strncpy(sd.sd_filename, "json", sizeof(sd.sd_filename));
    sd.sd_flags = flags | SDF_JSON_KEYWORDS;
    sd.sd_ctxt = ctxt;
    sd.sd_parse = sd.sd_ttype = M_JSON;

    ctxt->version = xmlCharStrdup(XML_DEFAULT_VERSION);
    ctxt->userData = &sd;

    /*
     * Fake up an inputStream so the error mechanisms will work
     */
    xmlSetupParserForBuffer(ctxt, (const xmlChar *) "", sd.sd_filename);

    sd.sd_docp = slaxJsonBuildDoc(&sd, root_name, ctxt);
    if (sd.sd_docp == NULL) {
	slaxDataCleanup(&sd);
	return NULL;
    }

    sd.sd_docp->URL = (xmlChar *) xmlStrdup((const xmlChar *) "json.input");

    slaxParse(&sd);

    if (sd.sd_errors) {
	slaxError("%s: %d error%s detected during parsing",
		sd.sd_filename, sd.sd_errors, (sd.sd_errors == 1) ? "" : "s");

	slaxDataCleanup(&sd);
	return NULL;
    }

    /* Save docp before slaxDataCleanup nukes it */
    res = sd.sd_docp;
    sd.sd_docp = NULL;
    slaxDataCleanup(&sd);

    return res;
}

xmlDocPtr
slaxJsonFileToXml (const char *fname, const char *root_name,
		       unsigned flags)
{
    slax_data_t sd;
    xmlDocPtr res;

    slaxSetupLexer();

    xmlParserCtxtPtr ctxt = xmlNewParserCtxt();

    if (ctxt == NULL)
	return NULL;

    /*
     * Turn on line number recording in each node
     */
    ctxt->linenumbers = 1;

    bzero(&sd, sizeof(sd));

    strlcpy(sd.sd_filename, fname, sizeof(sd.sd_filename));
    sd.sd_flags = flags | SDF_JSON_KEYWORDS;
    sd.sd_ctxt = ctxt;
    sd.sd_parse = sd.sd_ttype = M_JSON;

    ctxt->version = xmlCharStrdup(XML_DEFAULT_VERSION);
    ctxt->userData = &sd;

    sd.sd_file = fopen(fname, "r");
    if (sd.sd_file == NULL) {
	slaxError("%s: cannot open: %s", fname, strerror(errno));
	return NULL;
    }

    /*
     * Fake up an inputStream so the error mechanisms will work
     */
    xmlSetupParserForBuffer(ctxt, (const xmlChar *) "", sd.sd_filename);

    sd.sd_docp = slaxJsonBuildDoc(&sd, root_name, ctxt);
    if (sd.sd_docp == NULL) {
	slaxDataCleanup(&sd);
	return NULL;
    }

    sd.sd_docp->URL = (xmlChar *) xmlStrdup((const xmlChar *) "json.input");

    slaxParse(&sd);

    if (sd.sd_errors) {
	slaxError("%s: %d error%s detected during parsing",
		sd.sd_filename, sd.sd_errors, (sd.sd_errors == 1) ? "" : "s");

	slaxDataCleanup(&sd);
	return NULL;
    }

    /* Save docp before slaxDataCleanup nukes it */
    res = sd.sd_docp;
    sd.sd_docp = NULL;
    slaxDataCleanup(&sd);

    fclose(sd.sd_file);

    return res;
}

#ifdef UNIT_TEST
int
main (int argc UNUSED, char **argv)
{
    xmlDocPtr docp;

    xmlInitParser();
    xsltInit();

    slaxLogEnable(TRUE);
    slaxIoUseStdio(0);

    while (--argc > 0) {
	const char *fname = *++argv;

	docp = slaxJsonFileToXml(fname, 0);
	if (docp) {
	    slaxDump(docp);

	    slaxJsonWriteDoc((slaxWriterFunc_t) fprintf, stdout, docp);

	    xmlFreeDoc(docp);
	}
    }

    slaxDynClean();
    xsltCleanupGlobals();
    xmlCleanupParser();

    return 0;
}
#endif /* UNIT_TEST */
