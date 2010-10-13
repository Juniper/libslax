/*
 * $Id$
 *
 * Copyright (c) 2010, Juniper Networks, Inc.
 * All rights reserved.
 * See ../Copyright for the status of this software
 *
 * This file includes hooks and additions to the libxml2 and libxslt APIs.
 */

#include <libxslt/xsltutils.h>	/* For xsltHandleDebuggerCallback, etc */

#define NUM_ARRAY(x)    (sizeof(x)/sizeof(x[0]))

/* ----------------------------------------------------------------------
 * Functions that aren't prototyped in libxml2 headers
 */

/**
 * nodePush:
 * @ctxt:  an XML parser context
 * @value:  the element node
 *
 * Pushes a new element node on top of the node stack
 *
 * Returns 0 in case of error, the index in the stack otherwise
 */
int
nodePush(xmlParserCtxtPtr ctxt, xmlNodePtr value);

/**
 * nodePop:
 * @ctxt: an XML parser context
 *
 * Pops the top element node from the node stack
 *
 * Returns the node just removed
 */
xmlNodePtr
nodePop(xmlParserCtxtPtr ctxt);

/**
 * Call xsltSetDebuggerCallbacks() with the properly typed argument.
 * I'm not sure of the reason but the arguments are untyped, giving it
 * a significant "ick" factor.  We preserve our dignity by hiding behind
 * an inline.
 * @see http://mail.gnome.org/archives/xslt/2010-September/msg00013.html
 */
static inline void
xsltSetDebuggerCallbacksHelper (xsltHandleDebuggerCallback handler,
				xsltAddCallCallback add_frame,
				xsltDropCallCallback drop_frame)
{
    void *cb[] = { handler, add_frame, drop_frame };

    xsltSetDebuggerCallbacks(NUM_ARRAY(cb), &cb);
}

/*
 * Free a chunk of memory
 */
static inline void
xmlFreeAndEasy (void *ptr)
{
    if (ptr)
	xmlFree(ptr);
}
