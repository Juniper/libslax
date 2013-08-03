/*
 * $Id: slaxloader.c,v 1.6 2008/05/21 02:06:12 phil Exp $
 *
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
#include "jsonwriter.h"

typedef struct jsonTtnameMap_s {
    int st_ttype;		/* Token number */
    const char *st_name;	/* Fancy, human-readable value */
} jsonTtnameMap_t;

static jsonTtnameMap_t jsonTtnameMap[] = {
    { L_CBRACE,			"close brace ('}')" },
    { L_CBRACK,			"close bracket (']')" },
    { L_COLON,			"colon (':')" },
    { L_COMMA,			"comma (',')" },
    { L_OBRACE,			"open brace ('{')" },
    { L_OBRACK,			"open bracket ('[')" },
    { K_FALSE,			"'false'" },
    { K_TRUE,			"'true'" },
    { K_NULL,			"'null'" },
    { T_NUMBER,			"number" },
    { T_STRING,			"string" },
    { T_TOKEN,			"token" },
    { 0, NULL }
};

static jsonTtnameMap_t jsonTokenMap[] = {
    { K_FALSE,			"false" },
    { K_TRUE,			"true" },
    { K_NULL,			"null" },
    { 0, NULL }
};

static int extXutilJsonInited;

static void
extXutilJsonInit (void)
{
    int i, ttype;

    for (i = 0; jsonTtnameMap[i].st_ttype; i++) {
	ttype = extXutilJsonTokenTranslate(jsonTtnameMap[i].st_ttype);
	extXutilJsonTokenNameFancy[ttype] =  jsonTtnameMap[i].st_name;
    }
}

static int
extXutilJsonToken (slax_data_t *sdp, int rc)
{
    static const char digits[] = "0123456789.+-eE";
    unsigned char ch;

    if (rc == T_STRING)
	sdp->sd_start += 1;

    for (;;) {
	for ( ; sdp->sd_cur < sdp->sd_len; sdp->sd_cur++) {
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
		if (strchr(digits, (int) ch) == NULL)
		    return rc;
		break;

	    case T_TOKEN:
		if (!isalnum((int) ch) && ch != '_')
		    return rc;
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
    for (;;) {
	while (sdp->sd_cur < sdp->sd_len
	       && isspace((int) sdp->sd_buf[sdp->sd_cur]))
	    sdp->sd_cur += 1;

	if (sdp->sd_cur < sdp->sd_len)
	    break;

	if (slaxGetInput(sdp, 1))
	    return -1;
    }

    sdp->sd_start = sdp->sd_cur; /* Reset start */

    switch (sdp->sd_buf[sdp->sd_cur++]) {
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
    case '-': case '+':
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
    slax_string_t *ssp = NULL;

    rc = extXutilJsonLexer(sdp);
    if (rc < L_LAST) {
	/* Don't make a string for literal values */
	if (yylvalp)
	    *yylvalp = NULL;

	slaxLog("json: lex: %p %s '%.*s' -> %d/%s %x",
		ssp, extXutilJsonTokenName(rc),
		sdp->sd_cur - sdp->sd_start, sdp->sd_buf + sdp->sd_start,
		rc, (rc > 0) ? extXutilJsonTokenName(rc) : "",
		(ssp && ssp->ss_token) ? ssp->ss_token : 0);

	return rc;
    }

    if (yylvalp) {
	if (rc == T_STRING)
	    sdp->sd_cur -= 1;

	*yylvalp = ssp = (rc > 0) ? slaxStringCreate(sdp, rc) : NULL;

	if (rc == T_STRING)
	    sdp->sd_cur += 1;

	if (rc == T_TOKEN) {
	    int i;

	    for (i = 0; jsonTokenMap[i].st_ttype; i++) {
		if (streq(ssp->ss_token, jsonTokenMap[i].st_name)) {
		    ssp->ss_ttype = rc = jsonTokenMap[i].st_ttype;
		    break;
		}
	    }
	}
    }

    slaxLog("json: lex: %p %s '%.*s' -> %d/%s %x",
	    ssp, extXutilJsonTokenName(rc),
	    sdp->sd_cur - sdp->sd_start, sdp->sd_buf + sdp->sd_start,
	    rc, (rc > 0) ? extXutilJsonTokenName(rc) : "",
	    (ssp && ssp->ss_token) ? ssp->ss_token : 0);

    return rc;
}

/*
 * Return a better class of error message
 * @returns freshly allocated string containing error message
 */
static char *
jsonSyntaxError (slax_data_t *sdp UNUSED, const char *token,
		 int yystate, int yychar,
		 slax_string_t **vstack UNUSED, slax_string_t **vtop UNUSED)
{
    char buf[BUFSIZ], *cp = buf, *ep = buf + sizeof(buf);

    /*
     * If yystate is 1, then we're in our initial state and have
     * not seen any valid input.  We handle this state specifically
     * to give better error messages.
     */
    if (yystate == 1) {
	if (yychar == -1)
	    return xmlStrdup2("unexpected end-of-file found (empty input)");

	SNPRINTF(cp, ep, "invalid JSON data");

	if (token)
	    SNPRINTF(cp, ep, "; %s is not legal", token);

    } else if (yychar == -1) {
	SNPRINTF(cp, ep, "unexpected end-of-file");

    } else {
	char *msg = extXutilJsonExpectingError(token, yystate, yychar);
	if (msg)
	    return msg;

	SNPRINTF(cp, ep, "unexpected input");
	if (token)
	    SNPRINTF(cp, ep, ": %s", token);
    }

    return xmlStrdup2(buf);
}

/**
 * Callback from bison when an error is detected.
 *
 * @param sdp main json data structure
 * @param str error message
 * @param value stack entry from bison's lexical stack
 * @return zero
 */
int
extXutilJsonYyerror (slax_data_t *sdp, const char *str, slax_string_t *value,
		     int yystate, slax_string_t **vstack, slax_string_t **vtop)
{
    static const char leader[] = "syntax error, unexpected";
    static const char leader2[] = "error recovery ignores input";
    const char *token = value ? value->ss_token : NULL;
    char buf[BUFSIZ];

    if (strncmp(str, leader2, sizeof(leader2) - 1) != 0)
	sdp->sd_errors += 1;

    if (token == NULL)
	token = extXutilJsonTokenNameFancy[
				 extXutilJsonTokenTranslate(sdp->sd_last)];
    else {
	int len = strlen(token);
	char *cp = alloca(len + 3); /* Two quotes plus NUL */

	cp[0] = '\'';
	memcpy(cp + 1, token, len);
	cp[len + 1] = '\'';
	cp[len + 2] = '\0';

	token = cp;
    }

    /*
     * See if there's a multi-line string that could be the source
     * of the problem.
     */
    buf[0] = '\0';
    if (vtop && *vtop) {
	slax_string_t *ssp = *vtop;

	if (ssp->ss_ttype == T_STRING) {
	    char *np = strchr(ssp->ss_token, '\n');

	    if (np != NULL) {
		int count = 1;
		char *xp;

		for (xp = np + 1; *xp; xp++)
		    if (*xp == '\n')
			count += 1;

		snprintf(buf, sizeof(buf),
			 "\n%s:%d:   could be related to unterminated string "
			 "on line %d (\"%.*s\\n...\")",
			 sdp->sd_filename, sdp->sd_line, sdp->sd_line - count,
			 (int) (np - ssp->ss_token), ssp->ss_token);
	    }
	}
    }

    /*
     * Two possibilities: generic "syntax error" or some
     * specific error.  If the message has a generic
     * prefix, use our logic instead.  This avoids tossing
     * bison token names (K_VERSION) at the user.
     */
    if (strncmp(str, leader, sizeof(leader) - 1) == 0) {
	char *msg = jsonSyntaxError(sdp, token, yystate, sdp->sd_last,
				    vstack, vtop);

	if (msg) {
	    slaxError("%s:%d: %s%s\n", sdp->sd_filename, sdp->sd_line,
		      msg, buf);
	    xmlFree(msg);
	    return 0;
	}
    }

    slaxError("%s:%d: %s%s%s%s%s\n",
	      sdp->sd_filename, sdp->sd_line, str,
	      token ? " before " : "", token, token ? ": " : "", buf);

    return 0;
}

void
extXutilJsonElementOpen (slax_data_t *sdp, const char *name)
{
    int valid = xmlValidateName((const xmlChar *) name, FALSE);
    const char *element = valid ? ELT_ELEMENT : name;

    slaxElementOpen(sdp, element);
    if (element != name)
	slaxAttribAddLiteral(sdp, ATT_NAME, name);
}

void
extXutilJsonElementValue (slax_data_t *sdp, slax_string_t *value)
{
    xmlNodePtr nodep;
    char *cp = value->ss_token;

    nodep = xmlNewText((const xmlChar *) cp);
    if (nodep)
	slaxAddChildLineNo(sdp->sd_ctxt, sdp->sd_ctxt->node, nodep);
}

static xmlDocPtr
extXutilJsonBuildDoc (slax_data_t *sdp UNUSED, const char *root_name,
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
extXutilJsonAddTypeInfo (slax_data_t *sdp, const char *value)
{
    if (!(sdp->sd_flags & SDF_NO_TYPES)) {
	xmlSetProp(sdp->sd_ctxt->node, (const xmlChar *) ATT_TYPE,
	       (const xmlChar *) value);
    }
}

void
extXutilJsonClearMember (slax_data_t *sdp)
{
    xmlNodePtr nodep = sdp->sd_ctxt->node;

    slaxElementClose(sdp);

    if (nodep->children == NULL) {
	/* Discard empty member */
	xmlUnlinkNode(nodep);
	xmlFreeNode(nodep);
    }
}

xmlDocPtr
extXutilJsonDataToXml (const char *data, const char *root_name, unsigned flags)
{
    slax_data_t sd;
    xmlDocPtr res;

    if (!extXutilJsonInited) {
	extXutilJsonInited = TRUE;
	extXutilJsonInit();
    }

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
    sd.sd_flags = flags;
    sd.sd_ctxt = ctxt;
    ctxt->version = xmlCharStrdup(XML_DEFAULT_VERSION);
    ctxt->userData = &sd;

    /*
     * Fake up an inputStream so the error mechanisms will work
     */
    xmlSetupParserForBuffer(ctxt, (const xmlChar *) "", sd.sd_filename);

    sd.sd_docp = extXutilJsonBuildDoc(&sd, root_name, ctxt);
    if (sd.sd_docp == NULL) {
	slaxDataClean(&sd);
	return NULL;
    }

    sd.sd_docp->URL = (xmlChar *) xmlStrdup((const xmlChar *) "json.input");

    extXutilJsonYyParse(&sd);

    if (sd.sd_errors) {
	slaxError("%s: %d error%s detected during parsing\n",
		sd.sd_filename, sd.sd_errors, (sd.sd_errors == 1) ? "" : "s");

	slaxDataClean(&sd);
	return NULL;
    }

    /* Save docp before slaxDataClean nukes it */
    res = sd.sd_docp;
    sd.sd_docp = NULL;
    slaxDataClean(&sd);

    return res;
}

xmlDocPtr
extXutilJsonFileToXml (const char *fname, const char *root_name,
		       unsigned flags)
{
    slax_data_t sd;
    xmlDocPtr res;

    if (!extXutilJsonInited) {
	extXutilJsonInited = TRUE;
	extXutilJsonInit();
    }

    xmlParserCtxtPtr ctxt = xmlNewParserCtxt();

    if (ctxt == NULL)
	return NULL;

    /*
     * Turn on line number recording in each node
     */
    ctxt->linenumbers = 1;

    bzero(&sd, sizeof(sd));

    strlcpy(sd.sd_filename, fname, sizeof(sd.sd_filename));
    sd.sd_flags = flags;
    sd.sd_ctxt = ctxt;
    ctxt->version = xmlCharStrdup(XML_DEFAULT_VERSION);
    ctxt->userData = &sd;

    sd.sd_file = fopen(fname, "r");
    if (sd.sd_file == NULL) {
	slaxError("%s: cannot open: %s\n", fname, strerror(errno));
	return NULL;
    }

    /*
     * Fake up an inputStream so the error mechanisms will work
     */
    xmlSetupParserForBuffer(ctxt, (const xmlChar *) "", sd.sd_filename);

    sd.sd_docp = extXutilJsonBuildDoc(&sd, root_name, ctxt);
    if (sd.sd_docp == NULL) {
	slaxDataClean(&sd);
	return NULL;
    }

    sd.sd_docp->URL = (xmlChar *) xmlStrdup((const xmlChar *) "json.input");

    extXutilJsonYyParse(&sd);

    if (sd.sd_errors) {
	slaxError("%s: %d error%s detected during parsing\n",
		sd.sd_filename, sd.sd_errors, (sd.sd_errors == 1) ? "" : "s");

	slaxDataClean(&sd);
	return NULL;
    }

    /* Save docp before slaxDataClean nukes it */
    res = sd.sd_docp;
    sd.sd_docp = NULL;
    slaxDataClean(&sd);

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
    extXutilJsonYyDebug = 1;

    while (--argc > 0) {
	const char *fname = *++argv;

	docp = extXutilJsonFileToXml(fname, 0);
	if (docp) {
	    slaxDump(docp);

	    extXutilJsonWriteDoc((slaxWriterFunc_t) fprintf, stdout, docp);

	    xmlFreeDoc(docp);
	}
    }

    slaxDynClean();
    xsltCleanupGlobals();
    xmlCleanupParser();

    return 0;
}
#endif /* UNIT_TEST */
