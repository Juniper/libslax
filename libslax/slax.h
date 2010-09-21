/*
 * $Id: slax.h,v 1.6 2007/09/18 09:03:31 builder Exp $
 *
 * Copyright (c) 2006-2010, Juniper Networks, Inc.
 * All rights reserved.
 * See ../Copyright for the status of this software
 */

/*
 * Turn on the SLAX XSLT document parsing hook.  This must be
 * called before SLAX files can be parsed.
 */
void slaxEnable(int enable);
#define SLAX_DISABLE	0	/* SLAX is not available */
#define SLAX_ENABLE	1	/* SLAX is auto-detected */
#define SLAX_FORCE	2	/* SLAX is forced on */
#define SLAX_CLEANUP	3	/* Clean up and shutdown */

#ifndef UNUSED
#define UNUSED __attribute__ ((__unused__))
#endif

typedef int (*slaxWriterFunc_t)(void *data, const char *fmt, ...);

/*
 * Write an XSLT document in SLAX format using the specified
 * fprintf-like callback function.
 */
int
slaxWriteDoc(slaxWriterFunc_t func, void *data, xmlDocPtr docp,
	     int partial, const char *);

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
slaxLoadFile(const char *, FILE *, xmlDictPtr, int);

/*
 * Prefer text expressions be stored in <xsl:text> elements
 * THIS FUNCTION IS DEPRECATED.
 */
void
slaxSetTextAsElement (int enable);

/*
 * Dump a formatted version of the XSL tree to a file
 */
void
slaxDumpToFd (int fd, xmlDocPtr docp, int);

/*
 * Dump a formatted version of the XSL tree to stdout
 */
void
slaxDump (xmlDocPtr docp);

void
slaxSetIndent (int indent);

void
slaxSetSpacesAroundAttributeEquals (int spaces);

/* ----------------------------------------------------------------------
 * Tracing
 */

typedef void (*slaxTraceCallback_t)(void *, xmlNodePtr, const char *fmt, ...);

/**
 * Enable tracing with a callback
 *
 * @func callback function
 * @data opaque data passed to callback
 */
void
slaxTraceEnable (slaxTraceCallback_t func, void *data);

/* ----------------------------------------------------------------------
 * Functions that work on generic strings
 */

#ifndef HAVE_STREQ
/**
 * @brief
 * Given two strings, return true if they are the same.
 * 
 * Tests the first characters for equality before calling strcmp.
 *
 * @param[in] red, blue
 *     The strings to be compared
 *
 * @return
 *     Nonzero (true) if the strings are the same. 
 */
static inline int
streq (const char *red, const char *blue)
{
    return (red && blue && *red == *blue && strcmp(red + 1, blue + 1) == 0);
}
#endif /* HAVE_STREQ */

#ifndef HAVE_CONST_DROP

/*
 * See const_* method below
 */
typedef union {
    void        *cg_vp;
    const void  *cg_cvp;
} const_remove_glue_t;

/*
 * NOTE:
 *     This is EVIL.  The ONLY time you cast from a const is when calling some
 *     legacy function that does not require const:
 *          - you KNOW does not modify the data
 *          - you can't change
 *     That is why this is provided.  In that situation, the legacy API should
 *     have been written to accept const argument.  If you can change the 
 *     function you are calling to accept const, do THAT and DO NOT use this
 *     HACK.
 */
static inline void *
const_drop (const void *ptr)
{
    const_remove_glue_t cg;
    cg.cg_cvp = ptr;
    return cg.cg_vp;
}

#endif /* HAVE_CONST_DROP */

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

/*
 * Some fairly simple hooks for the debugger.
 */
typedef char *(*slaxDebugInputCallback_t)(const char *);
typedef void (*slaxDebugOutputCallback_t)(const char *);

void
slaxDebugSetStylesheet (xsltStylesheetPtr stylep);

int
slaxDebugRegister (slaxDebugInputCallback_t input_callback,
		   slaxDebugOutputCallback_t output_callback);

void
slaxDebugSetIncludes (const char **includes);
