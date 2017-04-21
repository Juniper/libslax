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

#include <libpsu/psucommon.h>
#include <libpsu/psualloc.h>

psu_realloc_func_t psu_realloc = realloc;
psu_free_func_t psu_free = free;

/**
 * Set the allocator functions
 * @param[in] realloc_func realloc(3)-compatible function
 * @param[in] free_func free(3)-compatible function
 */
void
psu_set_allocator (psu_realloc_func_t realloc_func, psu_free_func_t free_func)
{
    psu_realloc = realloc_func;
    psu_free = free_func;
}
