/*
 * $Id: slax.h,v 1.6 2007/09/18 09:03:31 builder Exp $
 *
 * Copyright (c) 2006-2008, Juniper Networks, Inc.
 * All rights reserved.
 * See ./Copyright for the status of this software
 */

/*
 * Turn on the SLAX XSLT document parsing hook.  This must be
 * called before SLAX files can be parsed.
 */
void slaxEnable(int enable);
#define SLAX_DISABLE	0	/* SLAX is not available */
#define SLAX_ENABLE	1	/* SLAX is auto-detected */
#define SLAX_FORCE	2	/* SLAX is forced on */

typedef int (*slaxWriterFunc_t)(void *data, const char *fmt, ...);

/*
 * Write an XSLT document in SLAX format using the specified
 * fprintf-like callback function.
 */
int
slaxWriteDoc(slaxWriterFunc_t func, void *data, xmlDocPtr docp);

/*
 * Read a SLAX stylesheet from an open file descriptor.
 * Written as a clone of libxml2's xmlCtxtReadFd().
 */
xmlDocPtr
slaxCtxtReadFd(xmlParserCtxtPtr ctxt, int fd,
	       const char *URL, const char *encoding, int options);

/*
 * Read a SLAX file from an open file pointer
 */
xmlDocPtr
slaxLoadFile(const char *filename, FILE *file, xmlDictPtr dict);

/*
 * Prefer text expressions be stored in <xsl:text> elements
 */
void
slaxSetTextAsElement (int enable);

