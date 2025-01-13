/*
 * Copyright (c) 2010-2013, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <paths.h>
#include <sys/types.h>
#include <sys/time.h>
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

#if defined(HAVE_READLINE) || defined(HAVE_LIBEDIT)
#if 0
/*
 * The readline header files contain function prototypes that
 * won't compile with our warning level.  We take the "lesser
 * of two evils" approach and fake a prototype here instead
 * of turning down our warning levels.
 *
 * (...Hmmmm.... when this was a one-line extern for readline, I
 * didn't feel guilty, but it's getting longer....)
 */
#include <readline/readline.h>
#include <readline/history.h>
#else /* 0 */
extern char *readline (const char *);
extern void add_history (const char *);
extern FILE *rl_instream;
extern FILE *rl_outstream;
#endif /* 0 */
#endif /* defined(HAVE_READLINE) || defined(HAVE_LIBEDIT) */

/* Callback functions for input and output */
static slaxInputCallback_t slaxInputCallback;
static slaxOutputCallback_t slaxOutputCallback;
static xmlOutputWriteCallback slaxWriteCallback;
static slaxErrorCallback_t slaxErrorCallback;

static FILE *slaxIoTty;

static FILE *slaxIoFile;

/**
 * Use the input callback to get data
 * @prompt the prompt to be displayed
 */
char *
slaxInput (const char *prompt, unsigned flags)
{
    char *res, *cp;
    int count, len;

    /* slaxLog("slaxInput: -> [%s]", prompt); */
    res = slaxInputCallback ? slaxInputCallback(prompt, flags) : NULL;
    /* slaxLog("slaxInput: <- [%s]", res ?: "null"); */

    for (cp = res, count = 0, len = 0; cp && *cp; cp++, len++)
	if (iscntrl((int) *cp))
	    count += 1;

    if (count) {
	/*
	 * If we have control characters in the string, then we need
	 * to escape them into another (new) buffer.
	 */
	char *buf, *bp;

	buf = xmlMalloc(len + count * SLAX_UTF8_CNTRL_BYTES + 1);
	if (buf == NULL)
	    return res;

	for (cp = res, bp = buf; cp && *cp; cp++) {
	    if (iscntrl((int) *cp)) {
		unsigned word = SLAX_UTF8_CNTRL_BASE + (unsigned char) *cp;

		/*
		 * Mystical UTF-8 encoding rules.  I skipped the
		 * eye of newt, but the bat guano is essential.
		 */
		*bp++ = 0xe0 | (word >> 12);
		*bp++ = 0x80 | ((word >> 6) & 0x3f);
		*bp++ = 0x80 | (word & 0x3f);
	    } else
		*bp++ = *cp;
	}

	*bp = '\0';		/* Terminate the string */

	xmlFree(res);
	return buf;
    }

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
	char *cp = buf;
	size_t len;
	va_list vap;

	va_start(vap, fmt);
	len = vsnprintf(buf, sizeof(buf), fmt, vap);
	if (len >= sizeof(buf)) {
	    va_end(vap);
	    va_start(vap, fmt);
	    if (vasprintf(&cp, fmt, vap) < 0 || cp == NULL)
		return;
	}
	va_end(vap);

	slaxOutputCallback("%s\n", cp);

	if (cp != buf)
	    free(cp);	/* Allocated by vasprintf() */
    }
}

/**
 * Output a node
 */
void
slaxOutputNode (xmlNodePtr node)
{
    xmlSaveCtxtPtr handle;

    if (slaxWriteCallback == NULL)
	return;

    handle = xmlSaveToIO(slaxWriteCallback, NULL, NULL, NULL,
		 XML_SAVE_FORMAT | XML_SAVE_NO_DECL | XML_SAVE_NO_XHTML);
    if (handle) {
        xmlSaveTree(handle, node);
        xmlSaveFlush(handle);
	slaxWriteCallback(NULL, "\n", 1);
	xmlSaveClose(handle);
    }
}

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
 * Print an error message
 */
int
slaxError (const char *fmt, ...)
{
    va_list vap;
    int rc;

    if (slaxErrorCallback == NULL)
	return -1;

    va_start(vap, fmt);
    rc = slaxErrorCallback(fmt, vap);
    va_end(vap);
    return rc;
}

/*
 * Register debugger
 */
void
slaxIoRegister (slaxInputCallback_t input_callback,
		slaxOutputCallback_t output_callback,
		xmlOutputWriteCallback raw_write,
		slaxErrorCallback_t error_callback)
{
    slaxInputCallback = input_callback;
    slaxOutputCallback = output_callback;
    slaxWriteCallback = raw_write;
    slaxErrorCallback = error_callback;
}

static char *
slaxIoStdioInputCallback (const char *prompt, unsigned flags UNUSED)
{
    char *cp;

    if (flags & SIF_SECRET) {
	cp = getpass(prompt);
	return cp ? (char *) xmlStrdup((xmlChar *) cp) : NULL;

    } else {
#if defined(HAVE_READLINE) || defined(HAVE_LIBEDIT)
	char *res;

	/*
	 * The readline library doesn't seem to like multi-line
	 * prompts.  So we avoid them.
	 */
	cp = strrchr(prompt, '\n');
	if (cp) {
	    fprintf(stderr, "%.*s", (int) (cp - prompt + 1), prompt);
	    prompt = cp + 1;
	}

	/*
	 * readline() will return a malloc'd buffer but we need to
	 * swap it for memory that's acquired via xmlMalloc().
	 */
	cp = readline(prompt);
	if (cp == NULL)
	    return NULL;

	/* Add the command to the shell history (if it's not blank) */
	if ((flags & SIF_HISTORY) && *cp)
	    add_history(cp);

	res = (char *) xmlStrdup((xmlChar *) cp);
	free(cp);
	return res;
    
#else /* HAVE_READLINE || HAVE_LIBEDIT */
	char buf[BUFSIZ];
	int len;

	fputs(prompt, stderr);
	fflush(stderr);

	buf[0] = '\0';
	if (fgets(buf, sizeof(buf), slaxIoTty) == NULL)
	    return NULL;

	len = strlen(buf);
	if (len > 1 && buf[len - 1] == '\n')
	    buf[len - 1] = '\0';

	return (char *) xmlStrdup((xmlChar *) buf);
#endif /* HAVE_READLINE || HAVE_LIBEDIT */
    }
}

static void
slaxIoStdioOutputCallback (const char *fmt, ...)
{
    va_list vap;

    va_start(vap, fmt);
    vfprintf(stderr, fmt, vap);
    fflush(stderr);
    va_end(vap);
}

static int
slaxIoStdioRawwriteCallback (void *opaque UNUSED, const char *buf, int len)
{
    return write(fileno(stderr), buf, len);
}

static int
slaxIoStdioErrorCallback (const char *fmt, va_list vap)
{
    size_t len = strlen(fmt);
    char *cp = alloca(len + 2);
    memcpy(cp, fmt, len);
    cp[len] = '\n';
    cp[len + 1] = '\0';

    return vfprintf(stderr, cp, vap);
}

void
slaxIoUseStdio (unsigned flags)
{
    FILE *inst UNUSED = slaxIoTty;

#if defined(HAVE_READLINE) || defined(HAVE_LIBEDIT)
    inst = rl_instream;
#endif /* defined(HAVE_READLINE) || defined(HAVE_LIBEDIT) */

#if defined(_PATH_TTY)
    /* Non-fatal failure; falls back to stdin */
    if (!(flags & SIF_NO_TTY) && inst == NULL)
	slaxIoTty = fopen(_PATH_TTY, "r+");
#endif /* _PATH_TTY */

#if defined(HAVE_READLINE) || defined(HAVE_LIBEDIT)
    rl_instream = rl_outstream = slaxIoTty;
#endif /* defined(HAVE_READLINE) || defined(HAVE_LIBEDIT) */

    slaxIoRegister(slaxIoStdioInputCallback, slaxIoStdioOutputCallback,
		   slaxIoStdioRawwriteCallback, slaxIoStdioErrorCallback);
}

/*
 * Called from slaxOutput to write to the currently opened file, or stderr
 * if the file isn't open (SNO).
 */
static void
slaxIoFileOutputCallback (const char *fmt, ...)
{
    va_list vap;
    FILE *fp = slaxIoFile ?: stderr;

    va_start(vap, fmt);
    vfprintf(fp, fmt, vap);
    fflush(fp);
    va_end(vap);
}

/*
 * Start writing slaxOutput() content to the specified file.
 */
int
slaxIoWriteOutputToFileStart (const char *filename)
{
    FILE *fp;

    if (filename == NULL || streq(filename, "-"))
	fp = stderr;
    else
	fp = fopen(filename, "w");

    if (fp == NULL)
	return -1;

    slaxIoFile = fp;
    slaxOutputCallback = slaxIoFileOutputCallback;

    return 0;
}

/*
 * Stop writing slaxOutput() content to our file; reset to stderr
 */
int
slaxIoWriteOutputToFileStop (void)
{
    FILE *fp = slaxIoFile;

    if (fp != stderr)
	fclose(fp);

    slaxIoFile = NULL;
    slaxOutputCallback = slaxIoStdioOutputCallback;

    return 0;
}

static void
slaxProcTrace (void *vfp, xmlNodePtr nodep, const char *fmt, ...)
{
    FILE *fp = vfp;
    va_list vap;

    va_start(vap, fmt);

#if !defined(NO_TRACE_CLOCK)
    {
	struct timeval cur_time;
	char *time_buffer;
	
	gettimeofday(&cur_time, NULL);
	time_buffer = ctime((time_t *)&cur_time.tv_sec);

	fprintf(fp, "%.15s: ", time_buffer + 4);  /* "Mmm dd hh:mm:ss" */
    }
#endif

    if (nodep) {
	xmlSaveCtxt *handle;

	fprintf(fp, "XML Content (%d)\n", nodep->type);
	fflush(fp);
	handle = xmlSaveToFd(fileno(fp), NULL,
			     XML_SAVE_FORMAT | XML_SAVE_NO_DECL);
	if (handle) {
	    xmlSaveTree(handle, nodep);
	    xmlSaveFlush(handle);
	    xmlSaveClose(handle);
	}

    } else {
	vfprintf(fp, fmt, vap);
    }

    fprintf(fp, "\n");
    fflush(fp);
    va_end(vap);
}

void
slaxTraceToFile (FILE *fp)
{
    slaxTraceEnable(slaxProcTrace, fp);
}

/**
 * Dump a formatted version of the XSL tree to a file
 *
 * @param fd file descriptor open for output
 * @param docp document pointer
 * @param partial Should we emit partial (snippet) output?
 */
void
slaxDumpToFd (int fd, xmlDocPtr docp, int partial)
{
    xmlSaveCtxtPtr handle;

    handle = xmlSaveToFd(fd, "UTF-8", XML_SAVE_FORMAT);

    if (!partial)
	xmlSaveDoc(handle, docp);
    else {
	xmlNodePtr nodep = xmlDocGetRootElement(docp);
	if (nodep)
	    nodep = nodep->children;

	for ( ; nodep; nodep = nodep->next) {
	    if (nodep->type == XML_ELEMENT_NODE) {
		xmlSaveTree(handle, nodep);
		xmlSaveFlush(handle);
		int rc = write(fd, "\n", 1);
		if (rc < 0)
		    break;
	    }
	}
    }

    xmlSaveClose(handle);
}

/*
 * Dump a formatted version of the XSL tree to stdout
 */
void
slaxDump (xmlDocPtr docp)
{
    slaxDumpToFd(1, docp, 0);
}

static int slaxExitCode;

void
slaxSetExitCode (int code)
{
    slaxExitCode = code;
}

int
slaxGetExitCode (void)
{
    return slaxExitCode;
}

typedef struct slax_error_data_s {
    int sed_mode;		/* Mode (SLAX_ERROR_*) */
    xmlNodePtr sed_nodep;	/* Node to record into */
} slax_error_data_t;

static slax_error_data_t slax_error_data;

static void
slaxGenericError (void *opaque, const char *fmt, ...)
{
    slax_error_data_t *sedp = opaque;
    va_list vap;

    va_start(vap, fmt);

    switch (sedp->sed_mode) {
    case SLAX_ERROR_RECORD:
	if (sedp->sed_nodep) {
	    char buf[BUFSIZ];
	    int bufsiz = sizeof(buf);
	    char *bp = buf;
	    int rc = vsnprintf(buf, bufsiz, fmt, vap);
	    if (rc >= bufsiz) {
		bp = alloca(rc + 1);
		vsnprintf(bp, rc + 1, fmt, vap);
	    }
	    if (rc > 0) {
		xmlNodePtr tp = xmlNewText((const xmlChar *) bp);
		tp = slaxAddChild(sedp->sed_nodep, tp);
	    }
	}
	break;
    case SLAX_ERROR_LOG:
	if (slaxLogIsEnabled)
	    slaxLogv(fmt, FALSE, vap);
	break;
    }

    va_end(vap);
}

/*
 * Return the value corresponding to the given name
 */
int
slaxErrorValue (const char *name)
{
    if (streq(name, "ignore"))
	return SLAX_ERROR_IGNORE;
    if (streq(name, "record"))
	return SLAX_ERROR_RECORD;
    if (streq(name, "log"))
	return SLAX_ERROR_LOG;
    return SLAX_ERROR_DEFAULT;
}

/*
 * Catch errors and do something specific with them
 */
int
slaxCatchErrors (int mode, xmlNodePtr recorder)
{
    if (mode == SLAX_ERROR_RECORD && recorder == NULL) {
	slaxLog("slaxCatchErrors: can't catch with no target node");
	return -1;
    }

    slax_error_data.sed_mode = mode;
    slax_error_data.sed_nodep = recorder;

    xmlSetGenericErrorFunc(&slax_error_data, slaxGenericError);

    return 0;
}
