/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer, August 2026
 *
 * User-visible error and warning reporting in C compiler message format.
 */

#include <stdio.h>
#include <stdarg.h>

#include <libpsu/psucommon.h>
#include <libpsu/psuthread.h>
#include <libpsu/psuerror.h>

static THREAD_GLOBAL(psu_error_callback_t) psu_error_callback;
static THREAD_GLOBAL(void *) psu_error_callback_data;

void
psu_error_set_callback (psu_error_callback_t func, void *opaque)
{
    psu_error_callback = func;
    psu_error_callback_data = opaque;
}

/*
 * Common emitter: format the message, prepend location and severity label,
 * then hand off to the callback or write directly to stderr.
 */
static void
psu_emit (const char *filename, int line, int is_warning,
          const char *fmt, va_list vap)
{
    char msgbuf[2048];
    vsnprintf(msgbuf, sizeof(msgbuf), fmt, vap);

    if (psu_error_callback) {
        psu_error_callback(psu_error_callback_data,
                           filename, line, is_warning, msgbuf);
        return;
    }

    const char *severity = is_warning ? "warning" : "error";
    if (filename && filename[0] != '\0' && line > 0)
        fprintf(stderr, "%s:%d: %s: %s\n", filename, line, severity, msgbuf);
    else if (filename && filename[0] != '\0')
        fprintf(stderr, "%s: %s: %s\n", filename, severity, msgbuf);
    else
        fprintf(stderr, "%s: %s\n", severity, msgbuf);
}

void
psu_errorv (const char *filename, int line, const char *fmt, va_list vap)
{
    psu_emit(filename, line, 0, fmt, vap);
}

void
psu_error (const char *filename, int line, const char *fmt, ...)
{
    va_list vap;
    va_start(vap, fmt);
    psu_emit(filename, line, 0, fmt, vap);
    va_end(vap);
}

void
psu_warningv (const char *filename, int line, const char *fmt, va_list vap)
{
    psu_emit(filename, line, 1, fmt, vap);
}

void
psu_warning (const char *filename, int line, const char *fmt, ...)
{
    va_list vap;
    va_start(vap, fmt);
    psu_emit(filename, line, 1, fmt, vap);
    va_end(vap);
}
