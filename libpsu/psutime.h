/*
 * Copyright (c) 2010-2013, 2017, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Common definitions for libpsu
 */

#ifndef LIBPSU_PSUTIME_H
#define LIBPSU_PSUTIME_H

#include <time.h>
#include <sys/time.h>

#include <libpsu/psucommon.h>

/**
 * @typedef psu_time_usecs_t
 * A type for holding micro seconds.  This is for comparisons, so the fact
 * that we leak seconds off the top is not a concern.
 */
typedef unsigned long psu_time_usecs_t;

/* Useful time-related constants */
#define NSEC_PER_SEC 1000000000ull
#define NSEC_PER_MSEC 1000000ull
#define NSEC_PER_USEC 1000
#define USEC_PER_SEC 1000000ull
#define USEC_PER_MSEC 1000
#define MSEC_PER_SEC 1000
#define SEC_PER_MIN 60

/**
 * Return the number of microseconds in a timeval
 *
 * @param[in] tvp Time as a timeval
 * @return Number of microseconds
 */
static inline psu_time_usecs_t
psu_timeval_to_usecs (struct timeval *tvp)
{
    return tvp->tv_sec * USEC_PER_SEC + tvp->tv_usec;
}

/**
 * Return an ISO-style formatted time string, as an ISO compatible
 * replacement for ctime(3).  NOTE: takes time_t by pointer, not
 * value.
 *
 * @param[in] t Time value to be formatted
 * @param[in] buf Destination buffer
 * @param[in] bufsiz Number of bytes available in the destination buffer
 */
static inline char *
psu_iso_time (const time_t *t, char *buf, size_t bufsiz)
{
    if (strftime(buf, bufsiz, "%Y-%m-%d %T %Z", localtime(t)) == 0)
        return NULL;

    return buf;
}

#endif /* LIBPSU_PSUTIME_H */
