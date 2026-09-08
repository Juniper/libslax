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
 * User-visible error and warning reporting in C compiler message format:
 *
 *     filename:line: error: message
 *     filename:line: warning: message
 *
 * This format is recognised by emacs compilation-mode, vi quickfix, and
 * similar tools.  Unlike psu_log(), these functions are NOT gated by the
 * logging enable flag — errors and warnings are always emitted.
 *
 * By default output goes to stderr.  Call psu_error_set_callback() to
 * redirect it; the callback receives the fully-formatted line (without a
 * trailing newline) so it can log or display it in any way it likes.
 */

#ifndef LIBPSU_PSUERROR_H
#define LIBPSU_PSUERROR_H

#include <stdarg.h>
#include <libpsu/psucommon.h>

/*
 * Optional callback for redirecting error/warning output.
 *
 * filename  — source file path (may be NULL or empty for unknown location)
 * line      — source line number (0 if unknown)
 * is_warning — non-zero for warnings, zero for errors
 * msg       — fully-formatted message text (no trailing newline)
 */
typedef void (*psu_error_callback_t)(void *opaque,
                                     const char *filename, int line,
                                     int is_warning, const char *msg);

/*
 * Install a callback for error/warning output.  Pass func=NULL to restore
 * the default (write to stderr).
 */
void psu_error_set_callback (psu_error_callback_t func, void *opaque);

/*
 * Emit a compiler-style error message.
 *   filename:line: error: <formatted text>
 */
PSU_PRINTFLIKE(3, 4)
void psu_error (const char *filename, int line, const char *fmt, ...);

/*
 * Variadic form of psu_error.
 */
void psu_errorv (const char *filename, int line, const char *fmt, va_list vap);

/*
 * Emit a compiler-style warning message.
 *   filename:line: warning: <formatted text>
 */
PSU_PRINTFLIKE(3, 4)
void psu_warning (const char *filename, int line, const char *fmt, ...);

/*
 * Variadic form of psu_warning.
 */
void psu_warningv (const char *filename, int line, const char *fmt, va_list vap);

#endif /* LIBPSU_PSUERROR_H */
