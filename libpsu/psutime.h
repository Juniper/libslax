/*
 * Copyright (c) 2010-2025, Juniper Networks, Inc.
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
typedef unsigned long long psu_time_usecs_t;

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
 * Return the current time in microseconds, using a monotonic clock
 * source so the value cannot jump backward or forward when the
 * system clock is adjusted (e.g. by NTP).  This makes it suitable
 * for measuring elapsed time; it is not a wall-clock/calendar time.
 */
#if defined(CLOCK_MONOTONIC)
#define PSU_CLOCK CLOCK_MONOTONIC
#elif defined(CLOCK_HIGHRES)
#define PSU_CLOCK CLOCK_HIGHRES
#else
#define PSU_CLOCK CLOCK_REALTIME
#endif

static inline psu_time_usecs_t
psu_get_time (void)
{
    struct timespec tv;

    if (clock_gettime(PSU_CLOCK, &tv) < 0)
	return 0;

    return tv.tv_sec * USEC_PER_SEC + tv.tv_nsec / NSEC_PER_USEC;
}

/*
 * Number of back-to-back calls PSU_CALIBRATE_OVERHEAD makes to the
 * timer function being calibrated.
 */
#define PSU_CALIBRATE_ITERATIONS 999

/**
 * Measure the per-call overhead of a zero-argument timer function, in
 * that timer's own units, by calling it back-to-back many times and
 * averaging the elapsed span over the run.  A profiler that takes the
 * difference of two readings of _timer_fn bracketing a hot path can
 * subtract this value from the diff to compensate for the cost that
 * the act of measurement itself adds.
 *
 * @param[in] _timer_fn Zero-argument function to calibrate
 * @param[out] _result Variable to receive the per-call overhead
 */
#define PSU_CALIBRATE_OVERHEAD(_timer_fn, _result)                    \
    do {                                                              \
	__typeof__((_timer_fn)()) _psu_start, _psu_end;                \
	unsigned _psu_i;                                               \
                                                                        \
	_psu_start = (_timer_fn)();                                    \
	for (_psu_i = 0; _psu_i < PSU_CALIBRATE_ITERATIONS; _psu_i++)  \
	    (_timer_fn)();                                             \
	_psu_end = (_timer_fn)();                                      \
                                                                        \
	(_result) = (_psu_end - _psu_start)                            \
	    / (PSU_CALIBRATE_ITERATIONS + 1);                          \
    } while (0)

/**
 * Convenience wrapper that calibrates psu_get_time() itself.
 *
 * @return the overhead of a single psu_get_time() call, in microseconds
 */
static inline psu_time_usecs_t
psu_calibrate_overhead (void)
{
    psu_time_usecs_t result;

    PSU_CALIBRATE_OVERHEAD(psu_get_time, result);

    return result;
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
