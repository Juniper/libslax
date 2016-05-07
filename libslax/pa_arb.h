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

#ifndef LIBSLAX_PA_ARB_H
#define LIBSLAX_PA_ARB_H

/**
 * A trivial malloc-like arbitrary-sized memory allocation library
 * built on top of the mmap allocator.  The result is a library that
 * can be addressed by "atoms", but allows random allocations, rather
 * than the fixed ones of paged arrays (pa_fixed).  When small
 * allocations are needed, pages are divided into chunks.  A simple
 * array holds linked lists of free pages, indexed by power of two,
 * allowing easy access to suitable sized chunks.  We have an array
 * indexed by slots, where each slot is a power of two.  We divide
 * matoms returned by pa_mmap into "chunks", where each page holds a
 * specific size of chunks.  When we hit a particular size, we stop
 * subdividing and just hold full atoms of those powers of two sizes.
 * To allocate, we unchain the next item on the free list and return
 * it.  To free, we just add it to the chain.
 *
 * For larger allocations, allocations (rounded up to page sizes)
 * are made directly from the underlaying allocator, with a header
 * that identifies them as such.  Freed blocks are placed in linked
 * list sorted by number of pages.  No defragmentation is attempted (yet).
 */

typedef struct pa_arb_header_s {
    uint16_t prh_magic;		/* Magic constant so we know we are us */
    uint8_t prh_slot;	        /* Slot number of in this allocation */
    uint8_t prh_chunk;		/* Chunk number with in the page */
    pa_atom_t prh_atom;		/* This atom's number */
    pa_atom_t prh_next_free[0]; /* If free, next free atom in list */
} pa_arb_header_t;

#define PRH_MAGIC_SMALL	0x5EA1 /* Indicates "small"-style allocation */
#define PRH_MAGIC_LARGE	0xB161 /* Indicates "large"-style allocation */

/** Constants for "small" allocations */
#define PA_ARB_ATOM_SHIFT	4 /* 1<<4 == 16, size of atom */
#define PA_ARB_ATOM_SIZE	(1 << PA_ARB_ATOM_SIZE)
#define PA_ARB_PAGE_SHIFT	12 /* 1<<12 == 4k atoms per page */
#define PA_ARB_PAGE_SIZE	(1 << PA_ARB_PAGE_SHIFT)

/* This is the largest power of two that can be handled by "small" */
#define PA_ARB_MAX_POW2 PA_ARB_PAGE_SHIFT

#if 0
/*
 * Each page is divided into chunks, which are continquous atoms,
 * with a single atom being the smallest chunk we can allocate.
 * Each page has a set of bits in the malloc info that tell us which
 * chunks of a page are free.
 * (not implemented yet)
 */
typedef uint16_t pa_alloc_bits_t;
    pa_alloc_bits_t ppi_free_bits; /* Which chunks are free */

typedef struct pa_page_info_s {
    uint16_t ppi_magic;		/* Magic number */
    uint16_t ppi_pow2;		/* What pow of 2 is this chunk */
} pa_page_info_t;
#endif /* 0 */

/*
 * pa_arb_info_t is the persistent information on the malloc store, in
 * contrast with pa_arb_t which is transient.  
 */
typedef struct pa_arb_info_s {
    pa_atom_t pri_free[PA_ARB_MAX_POW2]; /* The free list */
} pa_arb_info_t;

typedef struct pa_arb_s {
    pa_mmap_t *pr_mmap;		/* Underlaying memory file */
    pa_arb_info_t pr_info;	/* Our info structure, if needed */
    pa_arb_info_t *pr_infop;	/* A pointer to our info structure */
} pa_arb_t;

static inline void *
pa_arb_atom_addr (pa_arb_t *prp, pa_atom_t atom)
{
    return pa_mmap_addr(prp->pr_mmap, atom);
}

pa_atom_t
pa_arb_alloc_atom (pa_arb_t *prp, size_t size);

void
pa_arb_free_atom (pa_arb_t *prp, void *ptr);

void
pa_arb_init (pa_mmap_t *pmp, pa_arb_t *prp);

pa_arb_t *
pa_arb_setup (pa_mmap_t *pmp, pa_arb_info_t *prip);

pa_arb_t *
pa_arb_open (pa_mmap_t *pmp, const char *name);

void
pa_arb_close (pa_arb_t *prp);

#endif /* LIBSLAX_PA_ARB_H */
