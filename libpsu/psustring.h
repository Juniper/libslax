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
/*
 * strlcpy, for those that don't have it
 */
static inline size_t
strlcpy (char *dst, const char *src, size_t sz)
{
    size_t len = strlen(src);

    if (sz > len)
        sz = len;
    memmove(dst, src, sz);
    dst[sz] = '\0';

    return len;
}
#endif /* HAVE_STRLCPY */

#ifndef HAVE_STRLCAT
/*
 * strlcat for those lacking it
 */
static inline size_t
strlcat (char *dst, const char *src, size_t size)
{
    char *cp;
    size_t left = size;

    for (cp = dst; left-- != 0 && *cp != '\0'; cp++)
        continue;

    size_t used = cp - dst;
    left = size - used;

    if (left-- == 0)    /* -1 here covers the trailing NUL */
        return used + strlen(src);

    for (; *src != '\0'; src++, used++) {
        if (left == 0)
            continue;

        *cp++ = *src;
	left -= 1;
    }

    *cp = '\0';

    return used;
}
#endif /* HAVE_STRLCAT */

#endif /* LIBPSU_PSUSTRING_H */
