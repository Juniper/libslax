/*
 * $Id$
 *
 * Copyright (c) 2013, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * slaxprotoscript.c -- handle protoscripts
 */

#include "slaxinternals.h"
#include <libslax/slax.h>
#include "slaxparser.h"
#include <ctype.h>
#include <sys/queue.h>
#include <errno.h>

#include <libxslt/extensions.h>
#include <libxslt/documents.h>
#include <libxslt/transform.h>
#include <libexslt/exslt.h>
#include <libslax/slaxdata.h>
#include <libslax/slaxdatadir.h>

int slaxEnabledProtoscript; /* Protoscript enable (SLAX_*) */
static int slaxDebugProtoscript;   /* Enter debugger for protoscript */
static char *slaxProtoBase;	   /* Base protoscript name  */
static char *slaxProtoBaseDefault; /* Default base protoscript name  */

static slax_data_dirlist_t slaxProtoDirs; /* List of directories to search */

/**
 * Allow protoscript processing
 * @param enable [in] new setting for enabling protoscript
 * @param base [in] base protoscript name
 * @param base_default [in] default base protoscript name (if no match found)
 */
void
slaxEnableProtoscript (int enable, const char *base, const char *base_default)
{
    slaxEnabledProtoscript = enable;

    if (enable) {
	slaxProtoscriptAdd(SLAX_PROTOSCRIPTDIR);
    } else {
	slaxProtoscriptClean();
    }

    if (slaxProtoBase) {
	xmlFree(slaxProtoBase);
	slaxProtoBase = NULL;
    }
    if (base)
	slaxProtoBase = xmlStrdup2(base);

    if (slaxProtoBaseDefault) {
	xmlFree(slaxProtoBaseDefault);
	slaxProtoBaseDefault = NULL;
    }
    if (base_default)
	slaxProtoBaseDefault = xmlStrdup2(base_default);
}

/*
 * Add a directory to the list of directories searched for protoscripts
 */
void
slaxProtoscriptAdd (const char *dir)
{
    slaxDataDirAdd(&slaxProtoDirs, dir);
}

/*
 * Add a set of directories to the list of directories searched for files
 */
void
slaxProtoscriptAddPath (const char *dir)
{
    slaxDataDirAddPath(&slaxProtoDirs, dir);
}

void
slaxProtoscriptClean (void)
{
    slaxDataDirClean(&slaxProtoDirs);
}

int
slaxProtoscriptCheck (const char *protofilename, xmlDocPtr input)
{
    xmlNodePtr root = xmlDocGetRootElement(input);
    xmlNodePtr nodep;

    for (nodep = root->children; nodep; nodep = nodep->next) {
	if (nodep->type != XML_ELEMENT_NODE)
	    continue;
	if (nodep->ns == NULL || nodep->ns->href == NULL
	        || !streq((const char *) nodep->ns->href, XSL_URI)) {
	    slaxLog("protoscript: check matches: '%s'", protofilename ?: "");
	    return TRUE;
	}
    }

    return FALSE;
}

static FILE *
slaxProtoscriptFind (const char *scriptname, char *full_name, int full_size)
{
    char const *extentions[] = { "slax", "xsl", "xslt", "", NULL };
    char const **cpp;
    FILE *fp;
    slax_data_node_t *dnp;

    SLAXDATALIST_FOREACH(dnp, &slaxProtoDirs.sdd_list) {
	char *dir = dnp->dn_data;

	for (cpp = extentions; *cpp; cpp++) {
	    snprintf(full_name, full_size, "%s/%s%s%s",
		     dir, scriptname, **cpp ? "." : "", *cpp);
	    fp = fopen(full_name, "r+");
	    if (fp)
		return fp;
	}
    }

    return NULL;
}

xmlDocPtr
slaxProtoscriptRun (const char *protofilename, xmlDocPtr indoc UNUSED)
{
    xmlDocPtr scriptdoc, res;
    xsltStylesheetPtr script;
    char *base = slaxProtoBase;
    char scriptname[MAXPATHLEN];
    FILE *fp = NULL;

    slaxLog("protoscript: input is '%s'", protofilename ?: "");

    if (base) {
	fp = slaxProtoscriptFind(base, scriptname, sizeof(scriptname));
    } else {
	xmlNodePtr root = xmlDocGetRootElement(indoc);
	xmlNodePtr nodep;

	slaxDataDirLog(&slaxProtoDirs, "protoscript: search path: ");

	for (nodep = root->children; nodep; nodep = nodep->next) {
	    if (nodep->type != XML_ELEMENT_NODE)
		continue;
	    if (nodep->ns == NULL || nodep->ns->href == NULL
	            || !streq((const char *) nodep->ns->href, XSL_URI)) {
		fp = slaxProtoscriptFind((const char *) nodep->name,
					 scriptname, sizeof(scriptname));
		if (fp)
		    break;
	    }
	}

	if (fp == NULL)
	    fp = slaxProtoscriptFind(slaxProtoBaseDefault,
				     scriptname, sizeof(scriptname));
	if (fp == NULL) {
	    slaxError("Could not find protoscript for '%s'\n", protofilename);
	    return NULL;
	}
    }

    scriptdoc = slaxLoadFile(scriptname, fp, NULL, 0);
    fclose(fp);
    if (scriptdoc == NULL) {
	slaxError("protoscript: cannot parse: '%s'\n", scriptname);
	return NULL;
    }

    script = xsltParseStylesheetDoc(scriptdoc);
    if (script == NULL || script->errors != 0) {
	slaxError("%d errors parsing script: '%s'\n",
	     script ? script->errors : 1, scriptname);
	return NULL;
    }

    if (slaxDebugProtoscript) {
	slaxDebugInit();
	slaxDebugSetStylesheet(script);
	res = slaxDebugApplyStylesheet(scriptname, script, "input",
				       indoc, NULL);
    } else {
	res = xsltApplyStylesheet(script, indoc, NULL);
    }

    return res;
}
