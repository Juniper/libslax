/*
 * Copyright (c) 2010-2013, 2017, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Logging functions for libpsu
 */

#include <stdarg.h>

#include <libpsu/psucommon.h>
#include <libpsu/psuthread.h>
#include <libpsu/psulog.h>

static THREAD_GLOBAL(int) psu_log_is_enabledp;
static THREAD_GLOBAL(psu_log_callback_t) psu_log_callback;
static THREAD_GLOBAL(void *) psu_log_callback_data;
static THREAD_GLOBAL(FILE *) psu_log_fp;

/**
 * Indicate if logging is enabled
 * @return non-zero if logging is enabled; zero if disabled
 */
int
psu_log_is_enabled (void)
{
    return psu_log_is_enabledp;
}

/**
 * Enable logging
 * @param[in] enable non-zero to enable logging; zero to disable it
 */
void
psu_log_enable (int enable)
{
    psu_log_is_enabledp = enable;
}

/**
 * Enable logging with a callback
 * @param[in] func Callback function to write logging data
 * @param[in] data Opaque data passed to callback function
 */
void
psu_log_enable_callback (psu_log_callback_t func, void *data)
{
    psu_log_callback = func;
    psu_log_callback_data = data;
}

/**
 * Enable logging to a file
 * @param[in] fp File pointer to which to write data
 */
void
psu_log_to_file (FILE *fp)
{
    psu_log_fp = fp;
}

/**
 * Write formatted log data to log file
 *
 * @param[in] fmt Printf-style format string
 * @param[in] newline Boolean indicating if a trailing newling is needed
 * @param[in] vap Variadic arguments
 */
void
psu_logv (const char *fmt, int newline, va_list vap)
{
    if (psu_log_callback) {
	psu_log_callback(psu_log_callback_data, fmt, vap);
    } else {
	vfprintf(psu_log_fp ?: stderr, fmt, vap);
	if (newline)
	    fprintf(psu_log_fp ?: stderr, "\n");
	fflush(psu_log_fp ?: stderr);
    }
}

/**
 * Simple trace function that tosses messages to stderr if slaxLogIsEnabled
 * has been set to non-zero.
 *
 * @param[in] fmt Printf-style format string
 */
void
psu_log (const char *fmt, ...)
{
    va_list vap;

    if (!psu_log_is_enabledp)
	return;

    va_start(vap, fmt);
    psu_logv(fmt, TRUE, vap);
    va_end(vap);
}

/**
 * Simple trace function that tosses messages to stderr if slaxLogIsEnabled
 * has been set to non-zero.
 *
 * @param fmt format string plus variadic arguments
 */
void
psu_log2 (void *ignore UNUSED, const char *fmt, ...)
{
    va_list vap;

    if (!psu_log_is_enabledp)
	return;

    va_start(vap, fmt);
    psu_logv(fmt, FALSE, vap);
    va_end(vap);
}

