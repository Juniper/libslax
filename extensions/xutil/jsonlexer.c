/*
 * $Id: slaxloader.c,v 1.6 2008/05/21 02:06:12 phil Exp $
 *
 * Copyright (c) 2006-2011, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * slaxlexer.c -- lex slax input into lexical tokens
 *
 * This file contains the lexer for slax.
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

#include "jsonlexer.h"
#include "jsonparser.h"

static int
extXutilJsonToken (slax_data_t *sdp, int rc)
{
    unsigned char ch;

    sdp->sd_cur += 1;		/* Move over the character that got us here */

    for (;;) {
	while (sdp->sd_cur < sdp->sd_len) {
	    ch = sdp->sd_buf[sdp->sd_cur];

	    switch (rc) {
	    case T_STRING:
		if (ch == '"') {
		    sdp->sd_cur += 1;
		    return rc;

		} else if (ch == '\\') {
		    sdp->sd_cur += 1;
		}
		break;

	    case T_NUMBER:
		break;

	    case T_TOKEN:
		break;
	    }
	}

	if (slaxGetInput(sdp, 1))
	    return -1;		/* End of file or error */
    }

    return rc;
}

static int
extXutilJsonLexer (slax_data_t *sdp)
{
    sdp->sd_start = sdp->sd_cur;
    if (sdp->sd_cur == sdp->sd_len)
	if (slaxGetInput(sdp, 1))
	    return -1;		/* End of file or error */

    /* Skip leading whitespace */
    while (sdp->sd_cur < sdp->sd_len
	   && isspace((int) sdp->sd_buf[sdp->sd_cur]))
	sdp->sd_cur += 1;

    sdp->sd_start = sdp->sd_cur; /* Reset start */

    switch (sdp->sd_buf[sdp->sd_cur]) {
    case '}': return L_CBRACE;
    case ']': return L_CBRACK;
    case ',': return L_COMMA;
    case ':': return L_COLON;
    case '{': return L_OBRACE;
    case '[': return L_OBRACK;

    case '"':
	return extXutilJsonToken(sdp, T_STRING);

    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
	return extXutilJsonToken(sdp, T_NUMBER);

    default:
	return extXutilJsonToken(sdp, T_TOKEN);
    }

    return -1;
}

int
extXutilJsonYylex (slax_data_t *sdp, YYSTYPE *yylvalp)
{
    int rc;
    slax_string_t *ssp;

    rc = extXutilJsonLexer(sdp);
    if (rc < L_LAST) {
	if (yylvalp)
	    *yylvalp = NULL;
	return rc;
    }

    if (yylvalp)
	*yylvalp = ssp = (rc > 0) ? slaxStringCreate(sdp, rc) : NULL;

    return rc;
}

static xmlDocPtr
extXutilJsonBuildDoc (slax_data_t *sdp UNUSED, xmlParserCtxtPtr ctxt)
{
    xmlDocPtr docp;
    xmlNodePtr nodep;

    docp = xmlNewDoc((const xmlChar *) XML_DEFAULT_VERSION);
    if (docp == NULL)
	return NULL;

    docp->standalone = 1;

    nodep = xmlNewNode(NULL, (const xmlChar *) ELT_JSON);
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

xmlDocPtr
extXutilJsonToXml (char *data, unsigned flags UNUSED)
{
    slax_data_t sd;
    xmlDocPtr res;
    int rc;
    xmlParserCtxtPtr ctxt = xmlNewParserCtxt();

    if (ctxt == NULL)
	return NULL;

    /*
     * Turn on line number recording in each node
     */
    ctxt->linenumbers = 1;

    bzero(&sd, sizeof(sd));

    sd.sd_buf = data;
    sd.sd_len = strlen(data);
    strncpy(sd.sd_filename, "json", sizeof(sd.sd_filename));
    sd.sd_ctxt = ctxt;
    ctxt->version = xmlCharStrdup(XML_DEFAULT_VERSION);
    ctxt->userData = &sd;

    /*
     * Fake up an inputStream so the error mechanisms will work
     */
    xmlSetupParserForBuffer(ctxt, (const xmlChar *) "", sd.sd_filename);

    sd.sd_docp = extXutilJsonBuildDoc(&sd, ctxt);
    if (sd.sd_docp == NULL) {
	slaxDataCleanup(&sd);
	return NULL;
    }

    sd.sd_docp->URL = (xmlChar *) xmlStrdup((const xmlChar *) "json.input");

    rc = extXutilJsonYyParse(&sd);

    if (sd.sd_errors) {
	slaxError("%s: %d error%s detected during parsing\n",
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

#ifdef UNIT_TEST
int
main (int argc UNUSED, char **argv)
{
    xmlDocPtr docp;

    docp = extXutilJsonToXml(argv[1], 0);
    return 0;
}
#endif /* UNIT_TEST */
