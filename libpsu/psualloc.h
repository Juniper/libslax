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

typedef void *(*psu_realloc_func_t)(void *, size_t);
typedef void (*psu_free_func_t)(void *);

extern psu_realloc_func_t psu_realloc;
extern psu_free_func_t psu_free;

void
psu_set_allocator (psu_realloc_func_t realloc_func, psu_free_func_t free_func);

#endif /* LIBPSU_PSUALLOC_H */
