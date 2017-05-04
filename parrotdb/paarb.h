/*
 * Copyright (c) 2016-2017, Juniper Networks, Inc.
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

#ifndef PARROTDB_PAARB_H
#define PARROTDB_PAARB_H

#include <stdint.h>
#include <sys/param.h>

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
 * We use the low bits of the atom value to identify the chunk's
 * offset, and the minimal chunk size is 16 bytes (PA_ARB_ATOM_SIZE).
 * That gives us a max database size of 64GB when pa_arb is in use.
 *
 * Be aware that you will likely forget most numbers are in atoms,
 * not bytes, e.g. slot 9 is not 1<<9 (256), it's 1<<9<<4 (8192).
 * And yes, this comment is for "future me".
 *
 * For larger allocations, allocations (rounded up to page sizes) are
 * made directly from the underlaying allocator, with a header that
 * identifies them as such.  Freed blocks are free by the underlaying
 * allocator, at the cost of us recording their size.
 */

typedef uint8_t pa_arb_chunk_t;
typedef uint8_t pa_arb_slot_t;

/* Define our wrapper type */
PA_ATOM_TYPE(pa_arb_atom_t, pa_arb_atom_s, pra_atom,
	     pa_arb_is_null, pa_arb_atom, pa_arb_atom_of, pa_arb_null_atom);

typedef struct pa_arb_header_s {
    uint16_t prh_magic;		/* Magic constant so we know we are us */
    union {
	struct {
	    pa_arb_slot_t prh_slot; /* Slot number of in this allocation */
	    pa_arb_chunk_t prh_chunk; /* Chunk number with in the page */
	};
	uint16_t prh_size;	/* Size, in 4k  */
    };
    pa_arb_atom_t prh_next_free[0]; /* If free, next free atom in list */
} pa_arb_header_t;

#define PRH_MAGIC_SMALL_INUSE	0x5ea1 /* "Small"-style allocation; in use */
#define PRH_MAGIC_SMALL_FREE	0x5eb2 /* "Small"-style; on free list */
#define PRH_MAGIC_LARGE_INUSE	0xb161 /* "Large"-style allocation; in use */
#define PRH_MAGIC_LARGE_FREE	0xb172 /* "Large"-style; on free list */

/** Constants for "small" allocations */
#define PA_ARB_ATOM_SHIFT	4 /* 1<<4 == 16, size of atom */
#define PA_ARB_ATOM_SIZE	(1 << PA_ARB_ATOM_SIZE)
#define PA_ARB_PAGE_SHIFT	12 /* 1<<12 == 4k atoms per page */
#define PA_ARB_PAGE_SIZE	(1 << PA_ARB_PAGE_SHIFT)

#define PA_ARB_OFFSET_SHIFT (PA_MMAP_ATOM_SHIFT - PA_ARB_ATOM_SHIFT)

#define PA_ARB_CHUNK_SHIFT	8 /* Low bits used to identify chunks */
#define PA_ARB_CHUNK_SIZE	(1 << PA_ARB_CHUNK_SHIFT)

/* This is the largest power of two that can be handled by "small" */
#define PA_ARB_MAX_POW2 	PA_ARB_PAGE_SHIFT
#define PA_ARB_MAX_LARGE	(1 << (PA_MMAP_ATOM_SHIFT + PA_NBBY * 2))

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
    pa_arb_atom_t pri_free[PA_ARB_MAX_POW2 + 1]; /* The free list */
} pa_arb_info_t;

typedef struct pa_arb_s {
    pa_mmap_t *pr_mmap;		/* Underlaying memory file */
    pa_arb_info_t pr_info;	/* Our info structure, if needed */
    pa_arb_info_t *pr_infop;	/* A pointer to our info structure */
} pa_arb_t;

static inline void *
pa_arb_matom_addr (pa_arb_t *prp, pa_mmap_atom_t matom)
{
    return pa_mmap_addr(prp->pr_mmap, matom);
}

/*
 * Turn a pa_arb atom into an address.  The upper bits are a normal
 * matom, and the low bits are an offset within that matom.
 */
static inline pa_arb_header_t *
pa_arb_header (pa_arb_t *prp, pa_arb_atom_t atom)
{
    /* Lower bits are the arb atom */
    uint32_t off = pa_arb_atom_of(atom) & ((1 << PA_ARB_OFFSET_SHIFT) - 1);

    /* Upper bits are the mmap atom */
    pa_mmap_atom_t matom;
    matom = pa_mmap_atom(pa_arb_atom_of(atom) >> PA_ARB_OFFSET_SHIFT);

    psu_byte_t *addr = pa_arb_matom_addr(prp, matom);
    if (addr)
	addr += off << PA_ARB_ATOM_SHIFT;

    void *vaddr = addr;
    return vaddr;
}

/*
 * Turn a pa_arb atom into the address of the first usable byte,
 * which directly follows the pa_arb_header_t.
 */
static inline void *
pa_arb_atom_addr (pa_arb_t *prp, pa_arb_atom_t atom)
{
    pa_arb_header_t *prhp = pa_arb_header(prp, atom);
    return prhp ? &prhp[1] : NULL;
}

pa_arb_atom_t
pa_arb_alloc (pa_arb_t *prp, size_t size);

/*
 * Shorthand for strdup()-like functionality
 */
static inline pa_arb_atom_t
pa_arb_alloc_string (pa_arb_t *prp, const char *value)
{
    size_t len = strlen(value) + 1;
    pa_arb_atom_t atom = pa_arb_alloc(prp, len + 1);
    if (pa_arb_is_null(atom))
	return pa_arb_null_atom();

    char *dest = pa_arb_atom_addr(prp, atom);
    if (dest == NULL)		/* Should not occur */
	return pa_arb_null_atom();

    memcpy(dest, value, len);
    return atom;
}

void
pa_arb_free_atom (pa_arb_t *prp, pa_arb_atom_t atom);

void
pa_arb_init (pa_mmap_t *pmp, pa_arb_t *prp);

pa_arb_t *
pa_arb_setup (pa_mmap_t *pmp, pa_arb_info_t *prip);

pa_arb_t *
pa_arb_open (pa_mmap_t *pmp, const char *name);

void
pa_arb_close (pa_arb_t *prp);

void
pa_arb_dump (pa_arb_t *prp);

#endif /* PARROTDB_PAARB_H */
