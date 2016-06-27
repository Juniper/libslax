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

#define PA_MMAP_ATOM_SHIFT	12
#define PA_MMAP_ATOM_SIZE	(1ULL << PA_MMAP_ATOM_SHIFT)

#define PA_MMAP_HEADER_NAME_LEN	64 /* Max length of header name string */

struct pa_mmap_info_s;
typedef struct pa_mmap_info_s pa_mmap_info_t; /* Opaque type */

typedef uint32_t pa_mmap_flags_t; /* Flag values */
/* Flags for pa_mmap_flags_t */
#define PMF_READ_ONLY	(1<<0)	/* Open read-only */

/* Record of mmap'd segments */
typedef struct pa_mmap_record_s {
    struct pa_mmap_record_s *pmr_next;
    void *pmr_addr;
    size_t pmr_len;
} pa_mmap_record_t;

/*
 * This structure defines the in-memory information needed for
 * a mmap'd segment.  pm_addr == pm_infop, just a untyped.
 */
typedef struct pa_mmap_s {
    int pm_fd;			/* File descriptor, if used */
    pa_mmap_flags_t pm_flags;	/* Flags */
    int pm_mmap_flags;		/* mmap flags parameter */
    int pm_mmap_prot;		/* mmap prot parameter */
    void *pm_addr;		/* Base memory address */
    size_t pm_len;		/* Current mapped len */
    pa_mmap_info_t *pm_infop;	/* Mmap segment header */
    pa_mmap_record_t *pm_record; /* Record of mmap'd segments */
} pa_mmap_t;

static inline void *
pa_mmap_addr (pa_mmap_t *pmp, pa_matom_t atom)
{
    return (atom == PA_NULL_ATOM) ? NULL
	: pa_pointer(pmp->pm_addr, atom, PA_MMAP_ATOM_SHIFT);
}

pa_matom_t
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
pa_mmap_header (pa_mmap_t *pmp, const char *name,
		uint16_t type, uint16_t flags, size_t size);

void *
pa_mmap_next_header (pa_mmap_t *pmp, void *header);

#endif /* LIBSLAX_PA_MMAP_H */
