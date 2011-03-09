/*
 * $Id: slax.h,v 1.6 2007/09/18 09:03:31 builder Exp $
 *
 * Copyright (c) 2006-2010, Juniper Networks, Inc.
 * All rights reserved.
 * See ../Copyright for the status of this software
 */

#ifndef LIBSLAX_SLAX_H
#define LIBSLAX_SLAX_H

#include <libslax/slaxconfig.h>

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

/* ---------------------------------------------------------------------- */

/* Flags for slaxInputCallback_t */
#define SIF_HISTORY	(1<<0)	/* Add input line to history */
#define SIF_SECRET	(1<<1)	/* Secret/password text (do not echo) */

/*
 * IO hooks
 */
typedef char *(*slaxInputCallback_t)(const char *, unsigned flags);
typedef void (*slaxOutputCallback_t)(const char *, ...);
typedef int (*slaxErrorCallback_t)(const char *, va_list vap);

void
slaxIoRegister (slaxInputCallback_t input_callback,
		slaxOutputCallback_t output_callback,
		xmlOutputWriteCallback raw_write,
		slaxErrorCallback_t error_callback);

void slaxIoUseStdio (void);	/* Use the stock std{in,out} */
void slaxTraceToFile (FILE *fp);

int slaxError (const char *fmt, ...);

/* ---------------------------------------------------------------------- */
/*
 * Some fairly simple hooks for the debugger.
 */

/*
 * Register debugger
 */
int
slaxDebugInit (void);

/**
 * Set the top-most stylesheet
 *
 * @stylep the stylesheet aka script
 */
void
slaxDebugSetStylesheet (xsltStylesheetPtr stylep);

/**
 * Set a search path for included and imported files
 *
 * @includes array of search paths
 */
void
slaxDebugSetIncludes (const char **includes);

/**
 * Apply a stylesheet to an input document, returning the results.
 *
 * @scriptname Name of the script, or NULL to prohibit reloading
 * @style stylesheet aka script
 * @docname Name of the input document, or NULL to prohibit reloading
 * @doc input document
 * @params set of parameters
 * @returns output document
 */
void
slaxDebugApplyStylesheet (const char *scriptname, xsltStylesheetPtr style,
			  const char *docname, xmlDocPtr doc,
			  const char **params);

/*
 * Register our extension functions.
 */
int
slaxExtRegisterOther (const char *namespace);

/**
 * Registers the SLAX extensions
 */
void slaxExtRegister (void);

/**
 * Enable logging information internal to the slax library
 */
void
slaxLogEnable (int);

typedef void (*slaxLogCallback_t)(void *opaque, const char *fmt, va_list vap);

/**
 * Enable logging with a callback
 *
 * @func callback function
 * @data opaque data passed to callback
 */
void
slaxLogEnableCallback (slaxLogCallback_t func, void *data);

#endif /* LIBSLAX_SLAX_H */
