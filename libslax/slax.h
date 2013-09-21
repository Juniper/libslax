/*
 * Copyright (c) 2006-2013, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

#ifndef LIBSLAX_SLAX_H
#define LIBSLAX_SLAX_H

#include <libslax/slaxconfig.h>

#define SLAX_VERSION "1.2"
#define SLAX_VERSION_NUMBER 12

/* Forward declarations for libxml2/libxslt structures */
struct _xmlDoc;
struct _xmlDict;
struct _xmlNode;
struct _xmlXPathObject;
struct _xmlParserCtxt;
struct _xsltTransformContext;
struct _xsltStylesheet;

/**
 * Turn on the SLAX XSLT document parsing hook.  This must be
 * called before SLAX files can be parsed.
 * @param enable [in] new setting for enabling slax
 */
void slaxEnable(int enable);
#define SLAX_DISABLE	0	/**< SLAX is not available */
#define SLAX_ENABLE	1	/**< SLAX is auto-detected */
#define SLAX_FORCE	2	/**< SLAX is forced on */
#define SLAX_CLEANUP	3	/**< Clean up and shutdown */

#ifndef UNUSED
#define UNUSED __attribute__ ((__unused__))
#endif

/**
 * @typedef A callback used to write data to a destination, typically
 * referenced by the opaque `data` parameter.
 * @param data [in] opaque value passed thru to function
 * @param fmt [in] printf-style format argument
 */
typedef int (*slaxWriterFunc_t)(void *data, const char *fmt, ...);

int
slaxWriteDoc(slaxWriterFunc_t func, void *data, struct _xmlDoc *docp,
	     int partial, const char *vers);

/*
 * Read a SLAX stylesheet from an open file descriptor.
 * Written as a clone of libxml2's xmlCtxtReadFd().
 */
struct _xmlDoc *
slaxCtxtReadFd(struct _xmlParserCtxt *ctxt, int fd,
	       const char *URL, const char *encoding, int options);

/*
 * Read a SLAX file from an open file pointer
 */
struct _xmlDoc *
slaxLoadFile(const char *, FILE *, struct _xmlDict *, int);

/**
 * Read a SLAX file from a memory buffer
 *
 * @param filename Name of the file (or "-")
 * @param input Input data
 * @param dict libxml2 dictionary
 * @param partial TRUE if parsing partial SLAX contents
 * @return xml document pointer
 */
xmlDocPtr
slaxLoadBuffer (const char *filename, char *input,
		struct _xmlDict *dict, int partial);

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
slaxDumpToFd (int fd, struct _xmlDoc *docp, int);

/*
 * Dump a formatted version of the XSL tree to stdout
 */
void
slaxDump (struct _xmlDoc *docp);

void
slaxSetIndent (int indent);

void
slaxSetSpacesAroundAttributeEquals (int spaces);

/* ----------------------------------------------------------------------
 * Tracing
 */

typedef void (*slaxTraceCallback_t)(void *, struct _xmlNode *,
				    const char *fmt, ...);

/**
 * Enable tracing with a callback
 *
 * @func callback function
 * @data opaque data passed to callback
 */
void
slaxTraceEnable (slaxTraceCallback_t func, void *data);

/* ----------------------------------------------------------------------
 * Progress messages
 */

typedef void (*slaxProgressCallback_t)(void *, const char *fmt, ...);

/**
 * Enable progress messages with a callback
 *
 * @func callback function
 * @data opaque data passed to callback
 */
void
slaxProgressEnable (slaxProgressCallback_t func, void *data);

/**
 * Determine if progress messages are written to user or trace file.
 */
int
slaxEmitProgressMessages (int);

/* ---------------------------------------------------------------------- */

/* Flags for slaxInputCallback_t */
#define SIF_HISTORY	(1<<0)	/* Add input line to history */
#define SIF_SECRET	(1<<1)	/* Secret/password text (do not echo) */
#define SIF_NO_TTY	(1<<2)	/* Avoid the real terminal tty (use stdin) */

/*
 * IO hooks
 */
typedef char *(*slaxInputCallback_t)(const char *, unsigned flags);
typedef void (*slaxOutputCallback_t)(const char *, ...);
typedef int (*slaxErrorCallback_t)(const char *, va_list vap);

#if defined(XMLCALL) /* Need xml headers for xmlOutputWriteCallback */
void
slaxIoRegister (slaxInputCallback_t input_callback,
		slaxOutputCallback_t output_callback,
		xmlOutputWriteCallback raw_write,
		slaxErrorCallback_t error_callback);
#endif /* XMLCALL */

void slaxIoUseStdio (unsigned flags);	/* Use the stock std{in,out} */
void slaxTraceToFile (FILE *fp);

/**
 * Use the input callback to get data
 * @prompt the prompt to be displayed
 */
char *
slaxInput (const char *prompt, unsigned flags);

/**
 * Use the callback to output a string
 * @fmt printf-style format string
 */
void
#ifdef HAVE_PRINTFLIKE
__printflike(1, 2)
#endif /* HAVE_PRINTFLIKE */
slaxOutput (const char *fmt, ...);

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
slaxDebugSetStylesheet (struct _xsltStylesheet *stylep);

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
struct _xmlDoc *
slaxDebugApplyStylesheet (const char *scriptname,
			  struct _xsltStylesheet *style,
			  const char *docname, struct _xmlDoc *doc,
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

/**
 * Enable logging to a file
 *
 * @filename name of log file
 */
void
slaxLogToFile (FILE *fp);

/**
 * Initial the randomizer
 */
void
slaxInitRandomizer (void);

typedef void (*slaxRestartFunc)(void);
void slaxRestartListAdd(slaxRestartFunc func);
void slaxRestartListCall(void);

void
slaxSetPreserveFlag (struct _xsltTransformContext *tctxt,
		     struct _xmlXPathObject *ret);

/*
 * Add a directory to the list of directories searched for import
 * and include files
 */
void
slaxIncludeAdd (const char *dir);

/*
 * Add a set of directories to the list of directories searched for import
 * and include files.  The argument should contain a set of directory
 * names, seperated by colons ("/dir/one:/dir/two:/dir/three").
 */
void
slaxIncludeAddPath (const char *dir);

/*
 * Find and open a file, filling in buf with the name of the file
 */
FILE *
slaxFindIncludeFile (const char *url, char *buf, size_t bufsiz);

/*
 * Add a directory to the list of directories searched for dynamic
 * extension libraries
 */
void
slaxDynAdd (const char *dir);

void
slaxMemDump (const char *title, const char *data,
	     size_t len, const char *tag, int indent);

char *
slaxBase64Encode (const char *buf, size_t blen, size_t *olenp);
char *
slaxBase64Decode (const char *buf, size_t blen, size_t *olenp);

static inline int
slaxFilenameIsStd (const char *filename)
{
    return (filename == NULL || (filename[0] == '-' && filename[1] == '\0'));
}

void
slaxSetExitCode (int code);
int
slaxGetExitCode (void);

#endif /* LIBSLAX_SLAX_H */
