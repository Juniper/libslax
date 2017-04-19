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

#ifndef LIBPSU_PSULOG_H
#define LIBPSU_PSULOG_H

#include <libpsu/psucommon.h>

/**
 * Enable logging.
 *
 * @param[in] val Logging is enable if non-zero, disable if zero.
 */
void
psu_log_enable (int val);

/**
 * @typedef
 * @param[in] opaque Opaque data as given to psu_log_enable_callback
 * @param[in] fmt printf-style format string
 * @param[in] vap Variadic argument list
 */
typedef void (*psu_log_callback_t)(void *opaque, const char *fmt, va_list vap);

/**
 * Enable logging with a callback
 *
 * @param[in] func Callback function
 * @param[in] data Opaque data passed to callback
 */
void
psu_log_enable_callback (psu_log_callback_t func, void *data);

/**
 * Enable logging to a file
 *
 * @return fp Handle of log file
 */
void
psu_log_to_file (FILE *fp);

/**
 * Indicate of logging is enabled.  This allows the caller to opt out
 * of expensive logging logic.
 *
 * @return non-zero if logging is enabled
 */
int
psu_log_is_enabled (void);

/**
 * Dump a section of memory, with hex and ascii representations.
 *
 * @param[in] title Header printed above dump
 * @param[in] data Memory area to be dumped
 * @param[in] len Length in bytes of data
 * @param[in] tag Tag to be printed on each line
 * @param[in] indent Number of spaces to indent each line
 */
void
psu_mem_dump (const char *title, const char *data,
	      size_t len, const char *tag, int indent);

/**
 * Simple trace function that tosses messages to stderr if slaxLogIsEnabled
 * has been set to non-zero.
 *
 * @param[in] fmt Printf-style format string
 */
PSU_PRINTFLIKE(1, 2)
void
psu_log (const char *fmt, ...);

/**
 * Simple trace function that tosses messages to stderr if slaxLogIsEnabled
 * has been set to non-zero.
 *
 * @param fmt format string plus variadic arguments
 */
PSU_PRINTFLIKE(2, 3)
void
psu_log2 (void *ignore UNUSED, const char *fmt, ...);

/**
 * Write formatted log data to log file
 *
 * @param[in] fmt Printf-style format string
 * @param[in] newline Boolean indicating if a trailing newling is needed
 * @param[in] vap Variadic arguments
 */
void
psu_logv (const char *fmt, int newline, va_list vap);

/* Avoid massive s/slaxLog/psu_log/ changes; too commmon (for now) */
#define slaxLog psu_log
#define slaxLog2 psu_log2
#define slaxLogEnable psu_log_enable
#define slaxLogv psu_logv
#define slaxLogToFile psu_log_to_file
#define slaxLogIsEnabled psu_log_is_enabled()
#define slaxLogEnableCallback psu_log_enable_callback
#define slaxMemDump psu_mem_dump

#endif /* LIBPSU_PSULOG_H */
