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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <assert.h>
#include <stddef.h>

#include "slaxconfig.h"
#include <libslax/slax.h>
#include <libslax/pa_common.h>
#include <libslax/pa_mmap.h>
#include <libslax/pa_istr.h>

/*
 * Allocate the page to which the given atom belongs; mark them all free
 */
void
pa_istr_alloc_setup_page (pa_istr_t *pip, pa_atom_t atom)
{
    unsigned slot = atom >> pip->pi_shift;

    pa_atom_t count = 1 << pip->pi_shift;
    size_t size = count * pip->pi_atom_size;
    pa_matom_t matom = pa_mmap_alloc(pip->pi_mmap, size);

    pa_istr_page_entry_t *addr = pa_mmap_addr(pip->pi_mmap, matom);
    if (addr == NULL)
	return;

    /* Fill in the 'free' value for each atom, pointing to the next */
    unsigned i;
    unsigned mult = pip->pi_atom_size / sizeof(addr[0]);
    pa_atom_t first = slot << pip->pi_shift;
    for (i = 0; i < count - 1; i++) {
	addr[i * mult] = first + i + 1;
    }

    pa_istr_page_set(pip, slot, matom, addr);

    /*
     * For the last entry, we find the next available page and use
     * the first entry for that page.  If there's no page free, we
     * use zero, which will trigger 'out of memory' behavior when
     * that atom is used.
     *
     * We're taking advantage of the knowledge that we're going
     * to be allocing pages in incremental slot order, since that's
     * how we allocate them.  Don't break this behavior.
     */
    unsigned max_page = pip->pi_max_atoms >> pip->pi_shift;
    for (i = slot + 1; i < max_page; i++)
	if (pa_istr_page_get(pip, i) == NULL)
	    break;

    i = (i < max_page) ? i << pip->pi_shift : PA_NULL_ATOM;
    addr[(count - 1) * mult] = i;
}

/*
 * The most brutal of the initializers: the caller has an existing
 * base and info block for our use.  We just take them.
 */
void
pa_istr_init_from_block (pa_istr_t *pip, void *base,
			  pa_istr_info_t *infop)
{
    pip->pi_base = base;
    pip->pi_infop = infop;
}

void
pa_istr_init (pa_mmap_t *pmp, pa_istr_t *pip, pa_shift_t shift,
	       uint16_t atom_size, uint32_t max_atoms)
{
    /* Use our internal info block */
    if (pip->pi_infop == NULL)
	pip->pi_infop = &pip->pi_info_block;

    /* The atom must be able to hold a free node id */
    if (atom_size < sizeof(pa_atom_t))
	atom_size = sizeof(pa_atom_t);

    /* Must be even multiple of sizeof(pa_atom_t) */
    atom_size = pa_roundup32(atom_size, sizeof(pa_atom_t));

    /* Round max_atoms up to the next page size */
    max_atoms = pa_roundup_shift32(max_atoms, shift);

    /* No base is NULL, allocate it, zero it and init the free list */
    if (pip->pi_base == NULL) {
	size_t size = (max_atoms >> shift) * sizeof(uint8_t *);

	pa_atom_t atom = pa_mmap_alloc(pmp, size);
	void *real_base = pa_mmap_addr(pmp, atom);
	if (real_base == NULL)
	    return;

	bzero(real_base, size); /* New page table must be cleared */
	pip->pi_base = real_base;
	pa_istr_base_set(pip, atom, real_base);

	/* Mark number 1 as our first free atom */
	pip->pi_free = 1;
    }

    /* Fill in the rest of fhe fields from the argument list */
    pip->pi_shift = shift;
    pip->pi_atom_size = atom_size;
    pip->pi_max_atoms = max_atoms;
    pip->pi_mmap = pmp;
}

pa_istr_t *
pa_istr_setup (pa_mmap_t *pmp, pa_istr_info_t *pfip, pa_shift_t shift,
		uint16_t atom_size, uint32_t max_atoms)
{
    pa_istr_t *pip = calloc(1, sizeof(*pip));

    if (pip) {
	bzero(pip, sizeof(*pip));
	pip->pi_infop = pfip;
	pa_istr_init(pmp, pip, shift, atom_size, max_atoms);
    }

    return pip;
}    

pa_istr_t *
pa_istr_open (pa_mmap_t *pmp, const char *name, pa_shift_t shift,
		 uint16_t atom_size, uint32_t max_atoms)
{
    pa_istr_info_t *pfip = NULL;

    if (name) {
	pfip = pa_mmap_header(pmp, name, sizeof(*pfip));
	if (pfip == NULL) {
	    pa_warning(0, "pa_istr header not found: %s", name);
	    return NULL;
	}
    }

    return pa_istr_setup(pmp, pfip, shift, atom_size, max_atoms);
}

void
pa_istr_close (pa_istr_t *pip)
{
    pa_mmap_close(pip->pi_mmap);

    free(pip);
}
