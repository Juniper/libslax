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
#include <errno.h>
#include <sys/stat.h>
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
 * Set the exit code for the process:
 *     expr os:exit-code(15);
 */
static void
extOsExitCode (xmlXPathParserContext *ctxt, int nargs)
{
    int value;

    if (nargs != 1) {
	xmlXPathReturnNumber(ctxt, xsltGetMaxDepth());
	return;
    }

    value = xmlXPathPopNumber(ctxt);
    if (xmlXPathCheckError(ctxt))
	return;

    slaxSetExitCode(value);

    xmlXPathReturnEmptyString(ctxt);
}

static void
extOsMkdir (xmlXPathParserContext *ctxt, int nargs)
{
    char *name, *save, *cp;
    int rc, i;
    const char *value, *key;
    mode_t mode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
    int build_path = FALSE;

    if (nargs < 1 || nargs > 2) {
	xmlXPathReturnNumber(ctxt, xsltGetMaxDepth());
	return;
    }

    if (nargs == 2) {
	xmlXPathObject *xop = valuePop(ctxt);
	if (!xop->nodesetval || !xop->nodesetval->nodeNr) {
	    LX_ERR("os:mkdir invalid second parameter\n");
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

		if (streq(key, "mode")) {
		    mode_t x = strtol(value, NULL, 8);
		    if (x)
			mode = x;
		} else if (streq(key, "path")) {
		    build_path = TRUE;
		}
	    }
	}

	xmlXPathFreeObject(xop);
    }

    save = name = (char *) xmlXPathPopString(ctxt);
    if (name == NULL || xmlXPathCheckError(ctxt))
	return;

    cp = NULL;
    while (*name) {
	if (build_path) {
	    cp = strchr(name + 1, '/');
	    if (cp)
		*cp = '\0';
	}

	rc = mkdir(save, mode);
	if (rc && errno != EEXIST && cp == NULL)
	    slaxLog("os:mkdir for '%s' fails: %s",
		    name, strerror(errno));
	if (cp == NULL)
	    break;

	*cp = '/';
	name = cp + 1;
    }

    xmlFreeAndEasy(save);

    xmlXPathReturnEmptyString(ctxt);
}

slax_function_table_t slaxOsTable[] = {
    {
	"exit-code", extOsExitCode,
	"Specify an exit code for the process",
	"(number)", XPATH_UNDEFINED,
    },
    {
	"mkdir", extOsMkdir,
	"Create a directory",
	"(path)", XPATH_UNDEFINED,
    },
#if 0
    {
	"copy", extOsCopy,
	"Copy files",
	"(filespec, file-or-directory)", XPATH_NUMBER,
    },
    {
	"remove", extOsRemove
	"Remove files",
	"(filespec)", XPATH_UNDEFINED,
    },
    {
	"rmdir", extOsRmdir,
	"Remove a directory",
	"(path)", XPATH_UNDEFINED,
    },
    {
	"stat", extOsStat,
	"Return stat information about a file",
	"(file-spec)", XPATH_XSLT_TREE,
    },
    {
	"chmod", extOsChmod,
	"Change the mode of a file",
	"(file-spec, permissions)", XPATH_UNDEFINED,
    },
    {
	"chown", extOsChown,
	"Change ownership of a file",
	"(file-spec, ownership)", XPATH_UNDEFINED,
    },
#endif

    { NULL, NULL, NULL, NULL, XPATH_UNDEFINED }
};

SLAX_DYN_FUNC(slaxDynLibInit)
{
    arg->da_functions = slaxOsTable; /* Fill in our function table */

    return SLAX_DYN_VERSION;
}
