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
#include <libpsu/psualloc.h>

/*
 * Our own replacement for asprintf (ifndef HAVE_ASPRINTF).  See
 * the definition in psucommon.h.
 */
int
psu_asprintf (char **ret, const char *format, ...)
{
    char buf[128], *bp;		/* Smallish buffer */
    int rc;
    va_list vap;

    va_start(vap, format);
    rc = vsnprintf(buf, sizeof(buf), format, vap);
    if (rc < 0) {    /* Should not occur; vsnprintf can't return -1; but ... */
	*ret = NULL;
	return rc;
    }

    /*
     * We now know how much space is needed, and if it's small, we've
     * already got the formatted output; if not, reformat it with the
     * new larger buffer.
     */
    *ret = bp = psu_realloc(NULL, rc + 1);
    if (bp == NULL)
	rc = -1;
    else if (rc < (int) sizeof(buf))
	memcpy(bp, buf, rc + 1);
    else
	vsnprintf(bp, rc + 1, format, vap);

    va_end(vap);
    return rc;
}
