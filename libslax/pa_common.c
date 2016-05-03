/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer <phil@>, April 2016
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <assert.h>
#include <stdarg.h>
#include <stddef.h>

#include "slaxconfig.h"
#include <libslax/slax.h>
#include <libslax/pa_common.h>

/*
 * Cheesy breakpoint for memory allocation failure
 */
void
pa_alloc_failed (const char *msg UNUSED)
{
    return;			/* Just a place to breakpoint */
}

/*
 * Generate a warning
 */
void
pa_warning (int errnum, const char *fmt, ...)
{
    va_list vap;

    va_start(vap, fmt);

    vfprintf(stderr, fmt, vap);
    fprintf(stderr, "%s%s\n", (errnum > 0) ? ": " : "",
	    (errnum > 0) ? strerror(errnum) : "");

    va_end(vap);
}
