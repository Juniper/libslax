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

#ifndef LIBPSU_PSUCOMMON_H
#define LIBPSU_PSUCOMMON_H

#include <stdio.h>
#include <string.h>

#include <libslax/slaxconfig.h>
#include <libpsu/psucommon.h>

typedef unsigned psu_boolean_t;	/* Simple boolean type */
typedef unsigned char psu_byte_t; /* Simple byte (for addressing memory) */

#ifndef UNUSED
#define UNUSED __attribute__ ((__unused__))
#endif

#ifdef HAVE_PRINTFLIKE
#define PSU_PRINTFLIKE(_a, _b) __printflike(_a, _b)
#else
#define PSU_PRINTFLIKE(_a, _b)
#endif /* HAVE_PRINTFLIKE */

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* Number of elements in a static array */
#define PSU_NUM_ELTS(_arr) (sizeof(_arr) / sizeof(_arr[0]))

/* Bit operations; required since we made xi_boolean_t 8 bits (or less) */
#define PSU_BIT_CLEAR(_flags, _bit) do { (_flags) &= ~(_bit); } while (0)
#define PSU_BIT_SET(_flags, _bit) do { (_flags) |= (_bit); } while (0)
#define PSU_BIT_TEST(_flags, _bit) (((_flags) & (_bit)) ? TRUE : FALSE)

#ifndef HAVE_STREQ
/**
 * Given two strings, return true if they are the same.  Tests the
 * first characters for equality before calling strcmp.
 *
 * @param[in] red,blue The strings to be compared
 * @return Nonzero (true) if the strings are the same
 */
static inline int
streq (const char *red, const char *blue)
{
    return (red && blue && *red == *blue && strcmp(red + 1, blue + 1) == 0);
}
#endif /* HAVE_STREQ */

#ifndef HAVE_CONST_DROP
/*
 * NOTE: This is EVIL.  The ONLY time you cast from a const is when
 * calling some legacy function that does not allow const and:
 * - you KNOW it does not modify the data
 * - you can't change it
 * That is why this is provided.  In that situation, the legacy API
 * should have been written to accept const argument.  If you can
 * change the function you are calling to accept const, do THAT and DO
 * NOT use this HACK.
 */
typedef union {
    void *cg_vp;
    const void *cg_cvp;
} psu_const_remove_glue_t;

/**
 * Return a (non-writable) non-const pointer from a const pointer.  This
 * is used to allow const warnings, while supporting older non-const-aware
 * APIs.
 * @param[in] ptr const pointer to data
 * @return non-const value of ptr
 */
static inline void *
const_drop (const void *ptr)
{
    psu_const_remove_glue_t cg;
    cg.cg_cvp = ptr;
    return cg.cg_vp;
}
#endif /* HAVE_CONST_DROP */

#ifndef HAVE_SAFE_SNPRINTF
/**
 * @fn SNPRINTF(char *start, char *end, const char *fmt, ...)
 * A "safe" snprintf that never eats more than it can handle.  Using
 * a pointer to the start and end of a buffer, SNPRINTF ensures that
 * the buffer is never overrun.
 *
 * @param[in,out] start Start of the buffer
 * @param[in] end End of the buffer
 * @param[in] fmt Format string (and arguments), printf-style
 */
#define SNPRINTF(_start, _end, _fmt...) \
    do { \
	(_start) += snprintf((_start), (_end) - (_start), _fmt); \
	if ((_start) > (_end)) \
	    (_start) = (_end); \
    } while (0)
#endif /* HAVE_SAFE_SNPRINTF */

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

#ifndef HAVE_STRERROR
/*
 * Yes, there are systems without strerror.  Tough luck, eh?
 */
static inline const char *
strerror (int num)
{
    return "unknown error";
}
#endif /* HAVE_STRERROR */

#ifndef HAVE_ASPRINTF
/**
 * An asprintf replacement for systems missing it.  Yes, there are
 * such.  Worse, some have a <stdio.h> that may have already declared
 * asprintf() as non-static.  We use a quick #define to get around
 * that.
 */
#define asprintf psu_asprintf
#endif /* HAVE_ASPRINTF */

int psu_asprintf (char **ret, const char *format, ...);

/*
 * The plan is to come up with something faster, but so
 * far, the plan is underimplemented.
 */
static inline void *
psu_memchr (void *vsrc, int ch, size_t len)
{
    return memchr(vsrc, ch, len);
}

#endif /* LIBPSU_PSUCOMMON_H */
