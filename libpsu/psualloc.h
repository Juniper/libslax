/*
 * Copyright (c) 2015-2017, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * A memory allocation construct, built on realloc/free
 */

#ifndef LIBPSU_PSUALLOC_H
#define LIBPSU_PSUALLOC_H

#include <string.h>
#include <stdlib.h>

typedef void *(*psu_realloc_func_t)(void *, size_t);
typedef void (*psu_free_func_t)(void *);

extern psu_realloc_func_t psu_realloc;
extern psu_free_func_t psu_free;

/**
 * malloc-like memory allocator of memory
 * @param[in] size Number of bytes to allocate
 * @return a pointer to newly allocated memory, or NULL
 */
static inline void *
psu_malloc (size_t size)
{
    return psu_realloc(NULL, size);
}

/**
 * calloc-like memory allocator of zeroed memory
 * @param[in] size Number of bytes to allocate
 * @return a pointer to newly allocated and zeroed memory, or NULL
 */
static inline void *
psu_calloc (size_t size)
{
    void *ptr = psu_realloc(NULL, size);
    if (ptr != NULL)
	memset(ptr, 0, size);
    return ptr;
}

/**
 * Local reallocf(3) function.  Handles the case of leaking memory
 * when realloc fails.  Instead of pushing it on the caller, we handle
 * it ourselves.
 */
static inline void *
psu_reallocf (void *ptr, size_t size)
{
    void *newptr = psu_realloc(ptr, size);

    if (ptr != NULL && newptr == NULL)
	free(ptr);

    return newptr;
}

void
psu_set_allocator (psu_realloc_func_t realloc_func, psu_free_func_t free_func);

#ifndef HAVE_ALLOCADUP
/*
 * Helper function for ALLOCADUP
 */
static inline char *
allocadupx (char *to, const char *from)
{
    if (to) /* Allow alloca to return NULL, which it won't */
	strcpy(to, from);
    return to;
}

/**
 * @fn ALLOCADUP(const char *str)
 * Returns a copy of a string, allocated on the stack. Think of it as
 * strdup + alloca.  Cause it is.
 *
 * @param[in] str String to be duplicated
 * @return String, copied to the stack
 */
#define ALLOCADUP(s) allocadupx((char *) alloca(strlen(s) + 1), s)

/**
 * @fn ALLOCADUPX(const char *str)
 * Returns a copy of a string, allocated on the stack. Think of it as
 * strdup + alloca.  Cause it is.  If the input is NULL, return NULL.
 *
 * @param[in] str String to be duplicated, or NULL
 * @return String, copied to the stack, or NULL
 */
#define ALLOCADUPX(s) \
    ((s) ? allocadupx((char *) alloca(strlen(s) + 1), s) : NULL)

#endif /* HAVE_ALLOCADUP */

#endif /* LIBPSU_PSUALLOC_H */
