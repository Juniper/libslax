/*
 * Copyright (c) 2014, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

#ifndef LIBSLAX_SLAXEXT_H
#define LIBSLAX_SLAXEXT_H

typedef struct slax_printf_buffer_s {
    char *pb_buf;		/* Start of the buffer */
    int pb_bufsiz;		/* Size of the buffer */
    char *pb_cur;		/* Current insertion point */
    char *pb_end;		/* End of the buffer (buf + bufsiz) */
} slax_printf_buffer_t;

void
slaxExtPrintAppend (slax_printf_buffer_t *pbp, const xmlChar *chr, int len);

/*
 * Append a simple string to the print buffer.
 */
static inline void
slaxExtPrintAppendString (slax_printf_buffer_t *pbp, const char *str)
{
    size_t len = strlen(str);

    slaxExtPrintAppend(pbp, (const xmlChar *) str, len);
}

/*
 * Print content into a print buffer, resizing the buffer as needed
 */
int
slaxExtPrintfToBuf (slax_printf_buffer_t *pbp, const char *fmt, va_list vap);

int
slaxExtPrintWriter (void *data, const char *fmt, ...);

static inline void
slaxExtPrintfClean (slax_printf_buffer_t *pbp)
{
    xmlFreeAndEasy(pbp->pb_buf);
}

void
slaxExtRemoveNonXmlChars (char *input);

void
slaxExtTraceCallback (const char *str);

void
slaxExtProgressCallback (const char *str);

int
slaxExtTimeDiff (const struct timeval *new, const struct timeval *old,
                 struct timeval *diff);

int
slaxExtTimeCompare (const struct timeval *tv, double limit);

int
slaxExtDecodePriority (const char *priority);

char *
slaxExtPrintIt (const xmlChar *fmtstr, int argc, xmlChar **argv);

#endif /* LIBSLAX_SLAXEXT_H */

