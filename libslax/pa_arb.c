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

#if 0
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <assert.h>

#include "slaxconfig.h"
#include <libslax/slax.h>
#include <libslax/paged_array.h>
#include <libslax/pa_log2.h>
#include <libslax/pa_malloc.h>

/*
 * Allocate the given number of pages from the underlaying allocator
 */
static inline void *
pa_mallac_page_raw (pa_malloc_t *pmp, unsigned num_pages)
{
    page_array_t *pap = &pmp->pm_pa;
    size_t size = num_pages << (PA_MALSM_ATOM_SHIFT + PA_MALSM_PAGE_SHIFT);

    return pap->pa_alloc_fn(pap->pa_mem_opaque, size);
}

static inline unsigned
pa_malloc_slot (size_t size)
{
    return (size < (1 << PA_MALSM_ATOM_SHIFT))
	? 0 : (pa_log2(i - 1) - PA_MALSM_ATOM_SHIFT);
}

static inline unsigned
pa_malloc_chunks_per_page (pa_malloc_t *pmp, unsigned slot)
{
    return 1 << (PA_MALSM_PAGE_SIZE - (slot + PA_MALSM_ATOM_SHIFT));
}

/*
 * Convert from slot number to size; this is power of two, where
 * the first slot is an atom (PA_MALSM_ATOM_SIZE).
 */
static inline size_t
pa_malloc_slot_to_size (pa_malloc_t *pmp UNUSED, unsigned slot)
{
    return 1 << (slot + PA_MALSM_ATOM_SHIFT);
}

static void
pa_malloc_make_page (pa_malloc_t *pmp, unsigned slot)
{
    size_t real_size = pa_malloc_slot_to_size(pmp, slot);
    void *newp = pa_malloc_page_raw(pmp, 1); /* Allocate one page */

    if (newp == NULL)
	return;

    pa_atom_t *atomp = newp;
    pa_malloc_header_t *pmhp;
    unsigned chunks_per_page = pa_malloc_chunks_per_page(pmp, slot);
    unsigned atoms_per_chunk = chunks_per_page >> PA_MALSM_ATOM_SHIFT;
    unsigned i, imax;

    /*
     * Whiffle thru the page, setting up the
     */
    for (i = 0; i < imax; i++) {
	pmhp = (pa_malloc_header_t) &atomp[i * atoms_per_chunk];
	pmhp->pmh_magic = PMH_MAGIC_SMALL;
	pmhp->pmh_slot = slot;
	pmhp->pmh_chunk = i;
	pmhp->pmh_next_free = (i == imax - 1) ? 
    }
}

/*
 * Allocate memory from a paged array malloc pool.  We find the best slot
 * in the page table, based on side rounded up to power-of-two.  Then
 * take at item off the free list for that size.  If the free list is empty,
 * we'll allocate more memory for that size, putting items on the free list.
 */
pa_atom_t
pa_malloc (pa_malloc_t *pmp, size_t size)
{
    pa_malloc_header_t *pmhp;
    size_t full_size = size + sizeof(pa_malloc_header_t);
    unsigned slot = pa_log2(full_size);

    pa_atom_t atom = PA_NULL_ATOM;

    if (slot < PA_MALSM_MAX_POW2) {
	/* "Small"-style allocation */
	atom = pmp->pm_infop->pa_free[slot];
	if (atom == PA_NULL_ATOM) {
	    pa_malloc_page(pmp, slot);
	    atom = pmp->pm_infop->pa_free[slot];
	    if (atom == PA_NULL_ATOM)
		return PA_NULL_ATOM;
	}

	pmhp = pa_atom_addr(&pmp->pm_pa, atom);
	pmp->pm_infop->pa_free[slot] = pmhp->pmh_next_free;

	pmhp->pmh_magic = PMH_MAGIC_SMALL;
	pmhp->pmh_size = slot;

    } else {
	assert("pa_malloc: large size not supported (yet)");
    }

    return atom;
}

void
pa_free (pa_malloc_t *pmp, void *ptr)
{
}

void
pa_malloc_init (pa_malloc_t *pmp,
	 pa_base_set_func_t base_set_fn,
	 pa_page_get_func_t page_get_fn, pa_page_set_func_t page_set_fn, 
	 pa_alloc_func_t alloc_fn, pa_free_func_t free_fn, void *mem_opaque)
{
    if (alloc_fn == NULL)
	alloc_fn = pa_default_alloc;

    pa_init(&pmp->pm_pa, NULL, PA_MALSM_PAGE_SHIFT, PA_MALSM_ATOM_SIZE,
	    PA_MALSM_MAX_ATOMS,
		base_set_fn, page_get_fn, page_set_fn,
		alloc_fn, free_fn, mem_opaque);
}

pa_malloc_t *
pa_malloc_create (pa_base_set_func_t base_set_fn,
	   pa_page_get_func_t page_get_fn, pa_page_set_func_t page_set_fn, 
	   pa_alloc_func_t alloc_fn, pa_free_func_t free_fn, void *mem_opaque)
{
    if (alloc_fn == NULL)
	alloc_fn = pa_default_alloc;

    pa_malloc_t *pmp = alloc_fn(mem_opaque, sizeof(*pmp));

    if (pmp) {
	bzero(pmp, sizeof(*pmp));
	pa_malloc_init(pmp,
		base_set_fn, page_get_fn, page_set_fn,
		alloc_fn, free_fn, mem_opaque);
    }

    return pmp;
}

#endif
