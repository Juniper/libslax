/*
 * $Id$
 *
 * Copyright (c) 2000-2006, 2011, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * (Originally part of libjuniper/strextra.c)
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

#include <libpsu/psucommon.h>
#include <libpsu/psustring.h>

/*
 * strdupf(): sprintf + strdup: two great tastes in one!
 */
char *
strdupf (const char *fmt, ...)
{
    va_list vap;
    char buf[BUFSIZ];

    va_start(vap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, vap);
    va_end(vap);

    return strdup(buf);
}

#ifndef HAVE_STRNDUP
/*
 * strndup(): strdup() meets strncat(); return a duplicate string
 * of upto count characters of str. Always NUL terminate.
 */
char *
strndup (const char *str, size_t count)
{
    if (str == NULL) return NULL;
    else {
	size_t slen = strlen(str);
	size_t len = (count < slen) ? count : slen;
	char *cp = (char *) malloc(len + 1);

	if (cp) {
	    if (str) memcpy(cp, str, len);
	    cp[ len ] = 0;
	}
	return cp;
    }
}
#endif /* HAVE_STRNDUP */
