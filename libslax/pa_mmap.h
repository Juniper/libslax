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

#ifndef LIBSLAX_PA_MMAP_H
#define LIBSLAX_PA_MMAP_H

/*
 * Support for memory allocation over mmap()'d sections of memory.
 * Since paged arrays use only offset, this is mostly trivial.  In
 * particular, we don't provide a means of freeing memory.  We just
 * grow the memory segment and gives pages out of that delta between
 * the top of the memory segment and the top of allocated memory.
 *
 * On top of this facility, there are a number of distinct memory
 * allocators, each with different parameters and behaviors, and
 * _they_ can handle freeing memory within the allocator, if desired.
 */

struct pa_mmap_s;
typedef struct pa_mmap_s pa_mmap_t; /* Opaque type */

typedef uint32_t pa_mmap_flags_t; /* Flag values */
/* Flags for pa_mmap_flags_t */
#define PMF_READ_ONLY	(1<<0)	/* Open read-only */

pa_atom_t
pa_mmap_alloc (pa_mmap_t *pmp, size_t size);

void
pa_mmap_free (pa_mmap_t *pmp, pa_atom_t atom, unsigned size);

pa_mmap_t *
pa_mmap_open (const char *filename, pa_mmap_flags_t flags, unsigned mode);

void
pa_mmap_close (pa_mmap_t *pmp);

void *
pa_mmap_addr (pa_mmap_t *pmp, pa_atom_t atom);

void *
pa_mmap_header (pa_mmap_t *pmp, const char *name, size_t size);

#endif /* LIBSLAX_PA_MMAP_H */
