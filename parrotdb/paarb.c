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

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <assert.h>

#include <parrotdb/pacommon.h>
#include <parrotdb/pammap.h>
#include <parrotdb/palog2.h>
#include <parrotdb/paarb.h>
#include <libpsu/psualloc.h>
#include <libpsu/psulog.h>

static inline unsigned
pa_arb_slot (size_t size)
{
    if (size < (1 << PA_ARB_ATOM_SHIFT))
	return 0;

    return pa_log2(size - 1) - PA_ARB_ATOM_SHIFT;
}

static inline uint32_t
pa_arb_chunks_per_page (pa_arb_t *prp UNUSED, unsigned slot)
{
    uint32_t val = 1 << PA_ARB_OFFSET_SHIFT;
    val >>= slot;
    return val ?: 1;
}

/*
 * Convert from slot number to size; this is power of two, where
 * the first slot is an atom (PA_ARB_ATOM_SIZE).
 */
static inline size_t
pa_arb_slot_to_size (pa_arb_t *prp UNUSED, unsigned slot)
{
    size_t val = 1 << (slot + PA_ARB_ATOM_SHIFT);

    return (val < PA_MMAP_ATOM_SIZE) ? PA_MMAP_ATOM_SIZE : val;
}

static inline pa_arb_atom_t
pa_arb_build_atom (pa_arb_t *prp UNUSED, pa_mmap_atom_t matom,
		      pa_arb_slot_t slot, pa_arb_chunk_t chunk)
{
    if (pa_mmap_is_null(matom))
	return pa_arb_null_atom();

    /* High bits are the mmap atom */
    pa_atom_t raw = pa_mmap_atom_of(matom) << PA_ARB_OFFSET_SHIFT;

    /* Low bits are the arb atom */
    uint32_t off = (1 << slot) * chunk;
    raw |= off;

    return pa_arb_atom(raw);
}

static void
pa_arb_make_page (pa_arb_t *prp, unsigned slot)
{
    size_t real_size = pa_arb_slot_to_size(prp, slot);
    pa_mmap_atom_t matom = pa_mmap_alloc(prp->pr_mmap, real_size);
    if (pa_mmap_is_null(matom))
	return;

    pa_arb_header_t *prhp;
    unsigned chunks_per_page = pa_arb_chunks_per_page(prp, slot);
    unsigned i, imax = chunks_per_page;

    /*
     * Whiffle thru the page, setting up the chunks
     */
    pa_arb_atom_t atom = pa_arb_build_atom(prp, matom, slot, 0);
    pa_arb_atom_t saved_atom = atom;

    for (i = 0; i < imax; i++) {
	prhp = pa_arb_header(prp, atom);
	prhp->prh_magic = PRH_MAGIC_SMALL_FREE;
	prhp->prh_slot = slot;
	prhp->prh_chunk = i;

	if (i == imax - 1)
	    atom = pa_arb_null_atom(); /* End of the block */
	else atom = pa_arb_build_atom(prp, matom, slot, i + 1);

	prhp->prh_next_free[0] = atom;
    }

    prp->pr_infop->pri_free[slot] = saved_atom;
}

/*
 * Allocate memory from a paged array malloc pool.  We find the best slot
 * in the page table, based on side rounded up to power-of-two.  Then
 * take at item off the free list for that size.  If the free list is empty,
 * we'll allocate more memory for that size, putting items on the free list.
 */
pa_arb_atom_t
pa_arb_alloc (pa_arb_t *prp, size_t size)
{
    pa_arb_header_t *prhp;
    size_t full_size = size + sizeof(pa_arb_header_t);
    unsigned slot = pa_arb_slot(full_size);

    pa_arb_atom_t atom = pa_arb_null_atom();

    if (slot <= PA_ARB_MAX_POW2) {
	/* "Small"-style allocation */
	atom = prp->pr_infop->pri_free[slot];
	if (pa_arb_is_null(atom)) {
	    /*
	     * We're out of chunks at this size, so we go allocate some
	     * memory, so we can try again to pull off a new chunk.
	     */
	    pa_arb_make_page(prp, slot);
	    atom = prp->pr_infop->pri_free[slot];
	    if (pa_arb_is_null(atom))
		return pa_arb_null_atom();
	}

	prhp = pa_arb_header(prp, atom);

	/* Pull out the next free atom and record in our slot */
	prp->pr_infop->pri_free[slot] = prhp->prh_next_free[0];

	/* Mark the atom as in-use */
	prhp->prh_magic = PRH_MAGIC_SMALL_INUSE;

    } else if (full_size < PA_ARB_MAX_LARGE) {
	full_size = pa_roundup32(full_size, PA_MMAP_ATOM_SIZE);
	pa_mmap_atom_t matom = pa_mmap_alloc(prp->pr_mmap, full_size);
	if (pa_mmap_is_null(matom))
	    return pa_arb_null_atom();

	/*
	 * Since our atom numbers are just shifted matoms, we just
	 * need to zero fill the low bits.
	 */
	atom = pa_arb_atom(pa_mmap_atom_of(matom) << PA_ARB_OFFSET_SHIFT);

	prhp = pa_arb_header(prp, atom);
	prhp->prh_magic = PRH_MAGIC_LARGE_INUSE;

	/*
	 * For the size, we only keep the bits we need, dropping the
	 * low bits, which are zero anyway.  This allows us to fit in
	 * the pa_arb_header.
	 */
	prhp->prh_size = full_size >> PA_MMAP_ATOM_SHIFT;

    } else {
	pa_warning(0, "pa_arb: allocation size limit exceeded: %lu", size);
    }

    return atom;
}

static void
pa_arb_free_atom_addr (pa_arb_t *prp, pa_arb_atom_t atom, void *addr)
{
    pa_arb_header_t *prhp = addr;
    size_t full_size;
    unsigned slot;

    prhp -= 1;	     /* Back up to header, which preceeds user data */
    
    switch (prhp->prh_magic) {
    case PRH_MAGIC_SMALL_INUSE:
	/* "Small"-style allocation */
	slot = prhp->prh_slot;

	prhp->prh_next_free[0] = prp->pr_infop->pri_free[slot];
	prp->pr_infop->pri_free[slot] = atom;
	prhp->prh_magic = PRH_MAGIC_SMALL_FREE;
	break;

    case PRH_MAGIC_SMALL_FREE:
	pa_warning(0, "attempt to double free atom %#x (%p)",
		   pa_arb_atom_of(atom), addr);
	break;

    case PRH_MAGIC_LARGE_INUSE:
	full_size = prhp->prh_size << PA_MMAP_ATOM_SHIFT;
	pa_mmap_free(prp->pr_mmap,
		     pa_mmap_atom(pa_arb_atom_of(atom) >> PA_ARB_OFFSET_SHIFT),
		     full_size);
	break;

    case PRH_MAGIC_LARGE_FREE:
	pa_warning(0, "attempt to double free atom %#x (%p)",
		   pa_arb_atom_of(atom), addr);
	break;

    default:
	pa_warning(0, "bad magic number atom %#x (%p): %#x",
		   pa_arb_atom_of(atom), addr, prhp->prh_magic);
    }
}

void
pa_arb_free_atom (pa_arb_t *prp, pa_arb_atom_t atom)
{
    if (pa_arb_is_null(atom))
	return;

    void *addr = pa_arb_atom_addr(prp, atom);
    if (addr == NULL)		/* Should not occur */
	return;

    pa_arb_free_atom_addr(prp, atom, addr);
}

void
pa_arb_init (pa_mmap_t *pmp, pa_arb_t *prp)
{
    prp->pr_mmap = pmp;
}

pa_arb_t *
pa_arb_setup (pa_mmap_t *pmp, pa_arb_info_t *prip)
{
    pa_arb_t *prp = psu_calloc(sizeof(*prp));

    if (prp) {
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
	prip = pa_mmap_header(pmp, name, PA_TYPE_ARB, 0, sizeof(*prip));
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
    psu_free(prp);
}

void
pa_arb_dump (pa_arb_t *prp)
{
    pa_arb_slot_t slot;
    pa_arb_atom_t atom, saved_atom;
    pa_arb_header_t *prhp;
    unsigned count;

    psu_log("begin dumping pa_arb_t");

    for (slot = 0; slot <= PA_ARB_MAX_POW2; slot++) {
	saved_atom = atom = prp->pr_infop->pri_free[slot];
	if (pa_arb_is_null(atom))
	    continue;

	for (count = 0, prhp = pa_arb_header(prp, atom); prhp != NULL;
	     prhp = pa_arb_header(prp, atom)) {
	    if (prhp->prh_magic == PRH_MAGIC_SMALL_FREE) {
		count += 1;
		atom = prhp->prh_next_free[0];
	    } else
		break;
	}
	atom = saved_atom;

	psu_log("  slot:%u %#x (%u)", slot, pa_arb_atom_of(atom), count);
	for (prhp = pa_arb_header(prp, atom); prhp != NULL;
	     prhp = pa_arb_header(prp, atom)) {
	    if (prhp->prh_magic == PRH_MAGIC_SMALL_FREE) {
		psu_log("    %#x:%p slot:%u chunk:%u next %#x",
			pa_arb_atom_of(atom), prhp, prhp->prh_slot,
			prhp->prh_chunk,
			pa_arb_atom_of(prhp->prh_next_free[0]));

		atom = prhp->prh_next_free[0];

	    } else {
		psu_log("    %#x:%p bad magic number (%#x)",
			pa_arb_atom_of(atom), prhp, prhp->prh_magic);
		break;
	    }
	}
    }
    psu_log("end dumping pa_arb_t");
}
