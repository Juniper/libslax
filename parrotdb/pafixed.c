/*
 * Copyright (c) 2016-2017, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer <phil@>, April 2016
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <assert.h>
#include <stddef.h>

#include <parrotdb/pacommon.h>
#include <parrotdb/paconfig.h>
#include <parrotdb/pammap.h>
#include <parrotdb/pafixed.h>
#include <libpsu/psualloc.h>

/*
 * Allocate the page to which the given atom belongs; mark them all free
 */
void
pa_fixed_alloc_setup_page (pa_fixed_t *pfp, pa_fixed_atom_t atom)
{
    pa_page_t page = atom.pfa_atom >> pfp->pf_shift;
    pa_atom_t count = 1 << pfp->pf_shift;
    size_t size = count * pfp->pf_atom_size;

    pa_mmap_atom_t matom = pa_mmap_alloc(pfp->pf_mmap, size);
    pa_fixed_atom_t *addr = pa_mmap_addr(pfp->pf_mmap, matom);
    if (addr == NULL)
	return;

    /* Fill in the 'free' value for each atom, pointing to the next */
    unsigned i;
    unsigned mult = pfp->pf_atom_size / sizeof(addr[0]);
    pa_atom_t first = page << pfp->pf_shift;
    for (i = 0; i < count - 1; i++) {
	addr[i * mult].pfa_atom = first + i + 1;
    }

    pa_fixed_page_set(pfp, page, matom, addr);

    /*
     * For the last entry, we find the next available page and use
     * the first entry for that page.  If there's no page free, we
     * use zero, which will trigger 'out of memory' behavior when
     * that atom is used.
     *
     * We're taking advantage of the knowledge that we're going
     * to be allocing pages in incremental page order, since that's
     * how we allocate them.  Don't break this behavior.
     */
    pa_page_t max_page = pfp->pf_max_atoms >> pfp->pf_shift;
    for (i = page + 1; i < max_page; i++)
	if (pa_fixed_page_get(pfp, i) == NULL)
	    break;

    i = (i < max_page) ? i << pfp->pf_shift : PA_NULL_ATOM;
    addr[(count - 1) * mult].pfa_atom = i;
}

void
pa_fixed_element_setup_page (pa_fixed_t *pfp, pa_fixed_atom_t atom)
{
    pa_page_t page = atom.pfa_atom >> pfp->pf_shift;
    pa_atom_t count = 1 << pfp->pf_shift;
    size_t size = count * pfp->pf_atom_size;

    pa_mmap_atom_t matom = pa_mmap_alloc(pfp->pf_mmap, size);
    pa_fixed_atom_t *addr = pa_mmap_addr(pfp->pf_mmap, matom);
    if (addr == NULL)
	return;

    /* Set the page in the page array */
    pa_fixed_page_set(pfp, page, matom, addr);

    /* If needed, initialize the new memory to zero */
    if (pfp->pf_flags & PFF_INIT_ZERO)
	bzero(addr, size);
}

/*
 * The most brutal of the initializers: the caller has an existing
 * base and info block for our use.  We just take them.
 */
void
pa_fixed_init_from_block (pa_fixed_t *pfp, void *base,
			  pa_fixed_info_t *infop)
{
    pfp->pf_base = base;
    pfp->pf_infop = infop;
}

void
pa_fixed_init (pa_mmap_t *pmp, pa_fixed_t *pfp, const char *name,
	       pa_shift_t shift, uint16_t atom_size, uint32_t max_atoms)
{
    /* Overload the value with config values */
    shift = pa_config_value32(name, "shift", shift);
    atom_size = pa_config_value32_min(name, "atom-size", atom_size);
    max_atoms = pa_config_value32(name, "max-atoms", max_atoms);

    /* The atom must be able to hold a free node id */
    if (atom_size < sizeof(pa_atom_t))
	atom_size = sizeof(pa_atom_t);

    /* Must be even multiple of sizeof(pa_atom_t) */
    atom_size = pa_roundup32(atom_size, sizeof(pa_atom_t));

    /* Round max_atoms up to the next page size */
    max_atoms = pa_roundup_shift32(max_atoms, shift);

    /* No base is NULL, allocate it, zero it and init the free list */
    if (pfp->pf_base == NULL) {
	size_t size = (max_atoms >> shift) * sizeof(uint8_t *);

	pa_mmap_atom_t atom = pa_mmap_alloc(pmp, size);
	void *real_base = pa_mmap_addr(pmp, atom);
	if (real_base == NULL)
	    return;

	bzero(real_base, size); /* New page table must be cleared */

	/* Record the base atom and pointer */
	pfp->pf_infop->pfi_base = atom;
	pfp->pf_base = real_base;

	/* Mark number 1 as our first free atom */
	pfp->pf_free = pa_fixed_atom(1);
    }

    /* Fill in the rest of fhe fields from the argument list */
    pfp->pf_shift = shift;
    pfp->pf_atom_size = atom_size;
    pfp->pf_max_atoms = max_atoms;
    pfp->pf_mmap = pmp;
}

pa_fixed_t *
pa_fixed_setup (pa_mmap_t *pmp, pa_fixed_info_t *pfip, const char *name,
		pa_shift_t shift, uint16_t atom_size, uint32_t max_atoms)
{
    pa_fixed_t *pfp = psu_calloc(sizeof(*pfp));

    if (pfp) {
	pfp->pf_infop = pfip;
	pa_fixed_init(pmp, pfp, name, shift, atom_size, max_atoms);
    }

    return pfp;
}

pa_fixed_t *
pa_fixed_open (pa_mmap_t *pmp, const char *name, pa_shift_t shift,
		 uint16_t atom_size, uint32_t max_atoms)
{
    pa_fixed_info_t *pfip = NULL;

    if (name) {
	pfip = pa_mmap_header(pmp, name, PA_TYPE_FIXED, 0, sizeof(*pfip));
	if (pfip == NULL) {
	    pa_warning(0, "pa_fixed header not found: %s", name);
	    return NULL;
	}
    }

    return pa_fixed_setup(pmp, pfip, name, shift, atom_size, max_atoms);
}

void
pa_fixed_close (pa_fixed_t *pfp)
{
    psu_free(pfp);
}
