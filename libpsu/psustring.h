/*
 * Copyright (c) 2000-2008, 2011, 2017, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

#ifndef LIBPSU_PSUSTRING_H
#define LIBPSU_PSUSTRING_H

#include <stdlib.h>

/**
 * @brief
 * Produces output into a dynamically-allocated character string buffer
 * according to a format specification string and an appropriate number
 * of arguments.
 *
 * @param[in] fmt
 *     Format string (see sprintf(3) for a description of the format)
 * @param[in] ...
 *     Arguments sufficient to satisfy the format string
 *
 * @return 
 *     A pointer to the resultant string.
 */
char *strdupf (const char *fmt, ...) PSU_PRINTFLIKE(1, 2);

#ifndef HAVE_STRNDUP
/**
 * @brief
 * Allocates sufficient memory for a copy of a string, up to the maximum
 * number of characters specified.
 *
 * @param[in] str
 *     String to duplicate
 * @param[in] count
 *     Maximum number of characters to copy
 *
 * @return 
 *     A duplicate string of up to @a count characters of @a str.
 */
char *strndup (const char *str, size_t count);
#endif /* HAVE_STRNDUP */

/**
 * @brief
 * Safe form of snprintf(3) that returns the number of characters written,
 * rather than the number of characters that would have been written.
 *
 * @param[out] out
 *     Pointer to the output buffer
 * @param[in]  outsize
 *     Size of the output buffer, in bytes
 * @param[in]  fmt
 *     Format string (see sprintf(3) for a description of the format)
 * @param[in] ...
 *     Arguments sufficient to satisfy the format string
 *
 * @return 
 *     The number of characters written to the output buffer.
 */
size_t snprintf_safe (char *out, size_t outsize, const char *fmt, ...)
    PSU_PRINTFLIKE(3, 4);

/*
 * memdup(): allocates sufficient memory for a copy of the
 * buffer buf, does the copy, and returns a pointer to it.  The pointer may
 * subsequently be used as an argument to the function free(3).
 */
static inline void *
memdup (const void *buf, size_t size)
{
    void *vp = malloc(size);
    if (vp) memcpy(vp, buf, size);
    return vp;
}

#ifndef HAVE_STRNSTR
static inline char *
strnstr (char *s1,  const char *s2, size_t n)
{
    char first = *s2++;
    size_t s2len;
    char *cp, *np;

    if (first == '\0')	      /* Empty string means immediate match */
	return s1;

    s2len = strlen(s2); /* Does not count first */
    for (cp = s1; *cp; cp = np + 1) {
	np = strchr(cp, first);
	if (np == NULL)
	    return NULL;
	if (s2len == 0)		/* s2 is only one character long */
	    return np;
	if (n - (np - s1) < s2len)
	    return NULL;
	if (strncmp(np + 1, s2, s2len) == 0)
	    return np;
    }

    return NULL;
}
#endif /* HAVE_STRNSTR */

#ifndef HAVE_STRLCPY
static inline size_t
strlcpy (char *dst, const char *src, size_t left)
{
    const char *save = src;

    if (left == 0)
	return strlen(src);

    while (--left != 0)
	if ((*dst++ = *src++) == '\0')
	    break;

    if (left == 0) {
	*dst = '\0';
	while (*src++)
	    continue;
    }

    return src - save - 1;
}
#endif /* HAVE_STRLCPY */

#endif /* LIBPSU_PSUSTRING_H */
