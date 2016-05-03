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

#ifndef LIBSLAX_PA_LOG2_H
#define LIBSLAX_PA_LOG2_H

#if defined(__i386__) || defined(__amd64__)
static inline u_int32_t
pa_bsrl32 (u_int32_t mask)
{
    u_int32_t   result;

    __asm __volatile("bsrl %1,%0" : "=r" (result) : "rm" (mask));

    return result;
}
#endif /* defined(__i386__) || defined(__amd64__) */

/*
 * pa_log2 - return log2 of size
 */
static inline u_int32_t
pa_log2 (u_int32_t size)
{
    u_int32_t idx, mask = size;

#if defined(__i386__) || defined(__amd64__)
    idx = pa_bsrl32(mask) + 1;
#else
    for (idx = 0; mask; idx++)
        mask >>= 1;
#endif

    return idx;
}

#endif /* LIBSLAX_PA_LOG2_H */
