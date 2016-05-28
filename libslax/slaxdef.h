/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer <phil@>, May 2016
 */

#ifndef LIBSLAX_SLAXDEF_H
#define LIBSLAX_SLAXDEF_H

typedef unsigned slax_boolean_t;

#ifndef UNUSED
#define UNUSED __attribute__ ((__unused__))
#endif

#ifdef HAVE_PRINTFLIKE
#define SLAX_PRINTFLIKE(_a, _b) __printflike(_a, _b)
#else
#define SLAX_PRINTFLIKE(_a, _b)
#endif /* HAVE_PRINTFLIKE */

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/*
 * Simple trace function that tosses messages to stderr if slaxDebug
 * has been set to non-zero.
 */
void
slaxLog (const char *fmt, ...);

/*
 * Simple trace function that tosses messages to stderr if slaxDebug
 * has been set to non-zero.  This one is specific to bison, which
 * makes multiple calls to emit a single line, which means we can't
 * include our implicit newline.
 */
void
slaxLog2 (void *, const char *fmt, ...);

#endif /* LIBSLAX_SLAXDEF_H */
