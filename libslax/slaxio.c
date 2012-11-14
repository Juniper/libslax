/*
 * $Id$
 *
 * Copyright (c) 2010-2011, Juniper Networks, Inc.
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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/queue.h>

#include <libxml/xmlsave.h>
#include <libxml/xmlIO.h>
#include <libxslt/variables.h>
#include <libxslt/transform.h>

#include "config.h"
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

int slaxLogIsEnabled;
static slaxLogCallback_t slaxLogCallback;
static void *slaxLogCallbackData;
static FILE *slaxIoTty;

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
    return vfprintf(stderr, fmt, vap);
}

void
slaxIoUseStdio (unsigned flags)
{
    FILE *inst = slaxIoTty;

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
 * Enable logging information internal to the slax library
 */
void
slaxLogEnable (int enable)
{
    slaxLogIsEnabled = enable;
}

/**
 * Enable logging with a callback
 *
 * @func callback function
 * @data opaque data passed to callback
 */
void
slaxLogEnableCallback (slaxLogCallback_t func, void *data)
{
    slaxLogCallback = func;
    slaxLogCallbackData = data;
}

/**
 * Simple trace function that tosses messages to stderr if slaxLogIsEnabled
 * has been set to non-zero.
 *
 * @param fmt format string plus variadic arguments
 */
void
slaxLog (const char *fmt, ...)
{
    va_list vap;

    if (!slaxLogIsEnabled)
	return;

    va_start(vap, fmt);

    if (slaxLogCallback) {
	slaxLogCallback(slaxLogCallbackData, fmt, vap);
    } else {
	vfprintf(stderr, fmt, vap);
	fprintf(stderr, "\n");
	fflush(stderr);
    }

    va_end(vap);
}

/**
 * Simple trace function that tosses messages to stderr if slaxLogIsEnabled
 * has been set to non-zero.
 *
 * @param fmt format string plus variadic arguments
 */
void
slaxLog2 (void *ignore UNUSED, const char *fmt, ...)
{
    va_list vap;

    if (!slaxLogIsEnabled)
	return;

    va_start(vap, fmt);

    vfprintf(stderr, fmt, vap);
    fflush(stderr);

    va_end(vap);
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

/*
 * slaxMemDump(): dump memory contents in hex/ascii
0         1         2         3         4         5         6         7
0123456789012345678901234567890123456789012345678901234567890123456789012345
XX XX XX XX  XX XX XX XX - XX XX XX XX  XX XX XX XX abcdefghijklmnop
 */
void
slaxMemDump (const char *title, const char *data,
         size_t len, const char *tag, int indent)
{
    enum { MAX_PER_LINE = 16 };
    char buf[ 80 ];
    char text[ 80 ];
    char *bp, *tp;
    size_t i;
#if 0
    static const int ends[ MAX_PER_LINE ] = { 2, 5, 8, 11, 15, 18, 21, 24,
                                              29, 32, 35, 38, 42, 45, 48, 51 };
#endif

    slaxLog("%*s[%s] @ %p (%lx/%lu)", indent + 1, tag,
            title, data, (unsigned long) len, (unsigned long) len);

    while (len > 0) {
        bp = buf;
        tp = text;

        for (i = 0; i < MAX_PER_LINE && i < len; i++) {
            if (i && (i % 4) == 0) *bp++ = ' ';
            if (i == 8) {
                *bp++ = '-';
                *bp++ = ' ';
            }
            sprintf(bp, "%02x ", (unsigned char) *data);
            bp += strlen(bp);
            *tp++ = (isprint((int) *data) && *data >= ' ') ? *data : '.';
            data += 1;
        }

        *tp = 0;
        *bp = 0;
        slaxLog("%*s%-54s%s", indent + 1, tag, buf, text);
        len -= i;
    }
}

static char encoder[] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
    'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
    'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
    'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
    'w', 'x', 'y', 'z', '0', '1', '2', '3',
    '4', '5', '6', '7', '8', '9', '+', '/'
};

static char decoder[256];

char *
slaxBase64Encode (const char *buf, size_t blen, size_t *olenp)
{
    int olen = (size_t) ((blen  + 2) / 3) * 4;
    char *data = xmlMalloc(olen + 1);
    const char *cp, *ep;
    char *out;
    uint32_t bits;

    if (data == NULL)
	return NULL;

    out = data;
    cp = buf;
    ep = buf + blen;
    while (cp < ep) {
	bits = *cp++ << 16;
	bits += cp < ep ? *cp++ << 8 : 0;
	bits += cp < ep ? *cp++ : 0;

        *out++ = encoder[(bits >> 3 * 6) & 0x3F];
        *out++ = encoder[(bits >> 2 * 6) & 0x3F];
        *out++ = encoder[(bits >> 1 * 6) & 0x3F];
        *out++ = encoder[(bits >> 0 * 6) & 0x3F];
    }

    if (blen % 3 > 0) {
	out[-1] = '=';
	if (blen % 3 == 1)
	    out[-2] = '=';
    }

    *out = '\0';
    *olenp = out - data;
    return data;
}

char *
slaxBase64Decode (const char *buf, size_t blen, size_t *olenp)
{
    const char *cp, *ep;
    uint32_t bits;
    int i;
    char *out, *data, *stop;

    if (blen % 4 != 0) {
	if ((blen - 1) % 4 == 0
		&& (buf[blen - 1] == '\n' || buf[blen - 1] == '\0'))
	    blen -= 1;
	else
	    return NULL;
    }

    if (decoder['A'] == 0) {
	for (i = 0, cp = decoder; i < 0x40; i++)
	    decoder[(uint) encoder[i]] = i;
    }

    int olen = (blen / 4) * 3;

    cp = buf;
    ep = buf + blen;

    if (buf[blen - 1] == '=') {
	olen -= 1;
	ep -= 1;
    }
    if (buf[blen - 2] == '=') {
	olen -= 1;
	ep -= 1;
    }

    out = data = xmlMalloc(olen + 1);
    if (data == NULL)
	return NULL;
    stop = data + olen;

    while (cp < ep) {
	if (*cp == '\n' || *cp == '\r' || *cp == ' ' || *cp == '\t') {
	    cp += 1;
	    continue;
	}

	bits = decoder[(uint) *cp++] << 18;
	bits += cp < ep ? decoder[(uint) *cp++] << 12 : 0;
	bits += cp < ep ? decoder[(uint) *cp++] << 6 : 0;
	bits += cp < ep ? decoder[(uint) *cp++] : 0;

	*out++ = (bits >> 16) & 0xFF;
	if (out < stop)
	    *out++ = (bits >> 8) & 0xFF;
	if (out < stop)
	    *out++ = bits & 0xFF;
    }

    *out = '\0';
    *olenp = out - data;
    return data;
}

/*
 * Our own replacement for asprintf (ifndef HAVE_ASPRINTF).  See
 * the definition in slaxutil.h.
 */
int
slaxAsprintf (char **ret, const char *format, ...)
{
    char buf[128], *bp;		/* Smallish buffer */
    int rc;
    va_list vap;

    va_start(vap, format);
    rc = vsnprintf(buf, sizeof(buf), format, vap);
    if (rc < (int) sizeof(buf)) {
	*ret = bp = xmlStrdup2(buf);

    } else {
	*ret = bp = xmlMalloc(rc + 1);
	if (bp == NULL)
	    rc = -1;
	else
	    vsnprintf(bp, rc + 1, format, vap);
    }

    va_end(vap);
    return rc;
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
