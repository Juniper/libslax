/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer <phil@>, April 2016
 *
 * A "paged array" varient that allows arbitrary memory size allocations.
 */

#ifndef LIBSLAX_PA_MALLOC_H
#define LIBSLAX_PA_MALLOC_H

/**
 * A trivial malloc library built on top of paged arrays.  The result
 * is a library that can be addressed by small integers, but allows
 * random allocations, rather than the fixed ones of paged arrays.
 * When small allocations are needed, pages are divided into chunks.
 * A simple array holds linked lists of free pages, indexed by power
 * of two, allowing easy access to suitable sized chunks.
 *
 * For larger allocations, allocations (rounded up to page sizes)
 * are made directly from the underlaying allocator, with a header
 * that identifies them as such.  Freed blocks are placed in linked
 * list sorted by number of pages.  No defragmentation is attempted.
 */

typedef struct pa_malloc_header_s {
    uint16_t pmh_magic;	/* Magic constant so we know we are us */
    uint8_t pmh_slot;	/* Slot number of in this allocation */
    uint8_t pmh_chunk;	/* Chunk number with in the page */
    pa_atom_t pmh_next_free[0]; /* If free, next free atom in list */
} pa_malloc_header_t;

#define PMH_MAGIC_SMALL	0x5EA1 /* Indicates "small"-style allocation */
#define PMH_MAGIC_LARGE	0xB161 /* Indicates "large"-style allocation */

/** The largest "small" allocation */
#define PA_MALSM_ATOM_SHIFT	4 /* 1<<4 == 16, size of atom */
#define PA_MALSM_ATOM_SIZE	(1 << PA_MALSM_ATOM_SIZE)
#define PA_MALSM_PAGE_SHIFT	12 /* 1<<12 == 4k atoms per page */
#define PA_MALSM_PAGE_SIZE	(1 << PA_MALSM_SHIFT)
#define PA_MALSM_BASE_SHIFT	12 /* 1<<12 == 4k pages */
#define PA_MALSM_BASE_SIZE	(1 << PA_MALSM_BASE_SHIFT)

#define PA_MALSM_MAX_ATOMS (1 << (PA_MALSM_PAGE_SHIFT + PA_MALSM_BASE_SHIFT))

/* This is the largest power of two that can be handled by "small" */
#define PA_MALSM_MAX_POW2 (PA_MALSM_ATOM_SHIFT + PA_MALSM_PAGE_SHIFT)

/*
 * Each page is divided into chunks, which are continquous atoms,
 * with a single atom being the smallest chunk we can allocate.
 * Each page has a set of bits in the malloc info that tell us which
 * chunks of a page are free
 *
 */
typedef uint16_t pa_alloc_bits_t;

typedef struct pa_page_info_s {
    pa_alloc_bits_t ppi_free_bits; /* Which chunks are free */
    uint16_t ppi_pow2;		/* What pow of 2 is this chunk */
} pa_page_info_t;

/*
 * pa_malloc_info_t is the persistent information on the malloc store, in
 * contrast with pa_malloc_t which is transient.  
 */
typedef struct pa_malloc_info_s {
    pa_page_info_t pa_page_info[PA_MALSM_BASE_SIZE]; /* Per-page info */
    pa_atom_t pa_free[PA_MALSM_MAX_POW2]; /* The free list */
} pa_malloc_info_t;

typedef struct pa_malloc_s {
    paged_array_t pm_pa;	/* The underlaying paged array */
    pa_malloc_info_t pm_info;	/* Our info structure, if needed */
    pa_malloc_info_t *pm_infop;	/* A pointer to our info structure */
} pa_malloc_t;

pa_atom_t
pa_malloc (pa_malloc_t *pmp, size_t size);

void
pa_free (pa_malloc_t *pmp, void *ptr);

#endif /* LIBSLAX_PA_MALLOC_H */
