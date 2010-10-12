/*
 * $Id$
 *
 * Copyright (c) 2010, Juniper Networks, Inc.
 * All rights reserved.
 *
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/queue.h>

#include <libxml/xmlsave.h>
#include <libxml/xmlIO.h>
#include <libxslt/variables.h>
#include <libxslt/transform.h>

#include "slaxinternals.h"
#include <libslax/slax.h>
#include "config.h"

/* Callback functions for input and output */
static slaxInputCallback_t slaxInputCallback;
static slaxOutputCallback_t slaxOutputCallback;
static xmlOutputWriteCallback slaxWriteCallback;

/**
 * Use the input callback to get data
 * @prompt the prompt to be displayed
 */
char *
slaxInput (const char *prompt, unsigned flags)
{
    char *res;

    /* slaxTrace("slaxInput: -> [%s]", prompt); */
    res = slaxInputCallback ? slaxInputCallback(prompt, flags) : NULL;
    /* slaxTrace("slaxInput: <- [%s]", res ?: "null"); */

    return res;
}

/**
 * Use the callback to output a string
 * @fmt printf-style format string
 */
void
slaxOutput (const char *fmt, ...)
{
    if (slaxOutputCallback) {
	char buf[BUFSIZ];
	va_list vap;

	va_start(vap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, vap);
	va_end(vap);

	/* slaxTrace("slaxOutput: [%s]", buf); */
	slaxOutputCallback("%s\n", buf);
    }
}

/**
 * Output a node
 */
void
slaxOutputNode (xmlNodePtr node)
{
    xmlSaveCtxtPtr handle;

    handle = xmlSaveToIO(slaxWriteCallback, NULL, NULL, NULL,
		 XML_SAVE_FORMAT | XML_SAVE_NO_DECL | XML_SAVE_NO_XHTML);
    if (handle) {
        xmlSaveTree(handle, node);
        xmlSaveFlush(handle);
	slaxWriteCallback(NULL, "\n", 1);
	xmlSaveClose(handle);
    }
}

#if 0
static void
slaxOutputElement (xmlNodePtr node, int indent, const char *prefix)
{
    char buf[BUFSIZ], *cp = buf, *ep = buf + bufsiz;

    cp += snprintf(cp, ep - cp, "%.*s%s<%s", indent, "", prefix, node->name);
    
}
#endif

/**
 * Print the given nodeset. First we print the nodeset in a temp file.
 * Then read that file and send the the line to mgd one by one.
 */
void
slaxOutputNodeset (xmlNodeSetPtr nodeset)
{
    int i;

    for (i = 0; i < nodeset->nodeNr; i++)
	slaxOutputNode(nodeset->nodeTab[i]);
}

/*
 * Register debugger
 */
void
slaxIoRegister (slaxInputCallback_t input_callback,
		slaxOutputCallback_t output_callback,
		xmlOutputWriteCallback raw_write)
{
    slaxInputCallback = input_callback;
    slaxOutputCallback = output_callback;
    slaxWriteCallback = raw_write;
}
