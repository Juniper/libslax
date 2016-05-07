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

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <assert.h>

#include "slaxconfig.h"
#include <libslax/slax.h>
#include <libslax/pa_common.h>
#include <libslax/pa_mmap.h>
#include <libslax/pa_log2.h>
#include <libslax/pa_arb.h>

static inline unsigned
pa_arb_slot (size_t size)
{
    return (size < (1 << PA_ARB_ATOM_SHIFT))
	? 0 : (pa_log2(size - 1) - PA_ARB_ATOM_SHIFT);
}

static inline unsigned
pa_arb_chunks_per_page (pa_arb_t *prp UNUSED, unsigned slot)
{
    return 1 << (PA_ARB_PAGE_SIZE - (slot + PA_ARB_ATOM_SHIFT));
}

/*
 * Convert from slot number to size; this is power of two, where
 * the first slot is an atom (PA_ARB_ATOM_SIZE).
 */
static inline size_t
pa_arb_slot_to_size (pa_arb_t *prp UNUSED, unsigned slot)
{
    return 1 << (slot + PA_ARB_ATOM_SHIFT);
}

static void
pa_arb_make_page (pa_arb_t *prp, unsigned slot)
{
    size_t real_size = pa_arb_slot_to_size(prp, slot);
    pa_matom_t matom = pa_mmap_alloc(prp->pr_mmap, real_size);
    if (matom == PA_NULL_ATOM)
	return;

    uint8_t *newp = pa_mmap_addr(prp->pr_mmap, matom);
    pa_arb_header_t *prhp;
    unsigned chunks_per_page = pa_arb_chunks_per_page(prp, slot);
    unsigned atoms_per_chunk = chunks_per_page >> PA_ARB_ATOM_SHIFT;
    unsigned i, imax = chunks_per_page;

    /*
     * Whiffle thru the page, setting up the chunks
     */
    for (i = 0; i < imax; i++) {
	prhp = (pa_arb_header_t *) (void *) (newp + i * atoms_per_chunk);
	prhp->prh_magic = PRH_MAGIC_SMALL;
	prhp->prh_slot = slot;
	prhp->prh_chunk = i;
	prhp->prh_next_free[0] = (i != imax - 1) ? i + 1 : PA_NULL_ATOM;
    }
}

/*
 * Allocate memory from a paged array malloc pool.  We find the best slot
 * in the page table, based on side rounded up to power-of-two.  Then
 * take at item off the free list for that size.  If the free list is empty,
 * we'll allocate more memory for that size, putting items on the free list.
 */
pa_atom_t
pa_arb_alloc_atom (pa_arb_t *prp, size_t size)
{
    pa_arb_header_t *prhp;
    size_t full_size = size + sizeof(pa_arb_header_t);
    unsigned slot = pa_arb_slot(full_size);

    pa_atom_t atom = PA_NULL_ATOM;

    if (slot < PA_ARB_MAX_POW2) {
	/* "Small"-style allocation */
	atom = prp->pr_infop->pri_free[slot];
	if (atom == PA_NULL_ATOM) {
	    pa_arb_make_page(prp, slot);
	    atom = prp->pr_infop->pri_free[slot];
	    if (atom == PA_NULL_ATOM)
		return PA_NULL_ATOM;
	}

	prhp = pa_mmap_addr(prp->pr_mmap, atom);
	prp->pr_infop->pri_free[slot] = prhp->prh_next_free[0];

	prhp->prh_magic = PRH_MAGIC_SMALL;
	prhp->prh_slot = slot;
	prhp->prh_atom = atom;	/* Not _sure_ I need this, but... */

    } else {
	assert("pa_malloc: large size not supported (yet)");
    }

    return atom;
}

void
pa_arb_free_atom (pa_arb_t *prp, void *ptr)
{
    pa_arb_header_t *prhp = ptr;

    prhp -= 1;

    if (prhp->prh_magic != PRH_MAGIC_SMALL) {
	pa_warning(0, "pa_arg: magic number wrong: %p (%u)",
		   ptr, prhp->prh_magic);
	return;
    }

    unsigned slot = prhp->prh_slot;
    pa_atom_t atom = prhp->prh_atom;

    if (slot < PA_ARB_MAX_POW2) {
	/* "Small"-style allocation */
	prhp->prh_next_free[0] = prp->pr_infop->pri_free[slot];
	prp->pr_infop->pri_free[slot] = atom;
    } else {
	assert("pa_malloc: large size not supported (yet)");
    }
}

void
pa_arb_init (pa_mmap_t *pmp UNUSED, pa_arb_t *prp UNUSED)
{
    /* nothing */;
}

pa_arb_t *
pa_arb_setup (pa_mmap_t *pmp, pa_arb_info_t *prip)
{
    pa_arb_t *prp = calloc(1, sizeof(*prp));

    if (prp) {
	bzero(prp, sizeof(*prp));
	prp->pr_infop = prip;
	pa_arb_init(pmp, prp);
    }

    return prp;
}    

pa_arb_t *
pa_arb_open (pa_mmap_t *pmp, const char *name)
{
    pa_arb_info_t *prip = NULL;

    if (name) {
	prip = pa_mmap_header(pmp, name, sizeof(*prip));
	if (prip == NULL) {
	    pa_warning(0, "pa_arb header not found: %s", name);
	    return NULL;
	}
    }

    return pa_arb_setup(pmp, prip);
}


void
pa_arb_close (pa_arb_t *prp)
{
    pa_mmap_close(prp->pr_mmap);

    free(prp);
}

