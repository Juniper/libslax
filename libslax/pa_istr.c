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
#include <libslax/pa_config.h>
#include <libslax/pa_mmap.h>
#include <libslax/pa_fixed.h>
#include <libslax/pa_istr.h>


/*
 * We need to allocate "len" bytes of space, and return an atom
 * representing it.  Since we're not holding any information for
 * freeing that space, we can just pull off the next "n" atoms
 * and return them.  The inline version handles this for us, so
 * if we're here, there's not enough free atom to cover "len".
 * There are two cases: (a) len needs multiple pages, or (b) we
 * just need a single page.  Either way, we allocate a number of
 * pages, record the leftovers, and return the first atom.
 */
pa_atom_t
pa_istr_nstring_alloc (pa_istr_t *pip, const char *string, size_t len)
{
    unsigned max_page = pip->pi_max_atoms >> pip->pi_shift;
    unsigned slot;
    for (slot = 1; slot < max_page; slot++) /* Skip the first slot */
	if (pa_istr_page_get(pip, slot) == NULL)
	    break;

    if (slot >= max_page)	/* If we're out of slots, we're done */
	return PA_NULL_ATOM;

    unsigned len_atoms = pa_items_shift32(len + 1, pip->pi_atom_shift);

    pa_atom_t count = 1 << pip->pi_shift; /* Atoms per page */
    size_t bytes_per_page = count << pip->pi_atom_shift;

    size_t size = pa_roundup32(len + 1, bytes_per_page);

    /* Make sure we're a full page for the underlaying allocator */
    if (pip->pi_shift > PA_MMAP_ATOM_SHIFT)
	size = pa_roundup_shift32(size, PA_MMAP_ATOM_SHIFT);

    /* Number of atoms needed to cover the allocation */
    unsigned num_atoms = size >> pip->pi_atom_shift;

    pa_matom_t matom = pa_mmap_alloc(pip->pi_mmap, size);
    if (matom == PA_NULL_ATOM)
	return PA_NULL_ATOM;

    /* Fill in the page table */
    pa_istr_page_set(pip, slot, matom);

    pa_atom_t atom = slot << pip->pi_shift; /* Turns matoms to atoms */

    if (size <= bytes_per_page && len_atoms < num_atoms) {
	pip->pi_left = count - len_atoms;
	pip->pi_free = atom + len_atoms;
    }

    char *data = pa_istr_atom_addr(pip, atom);
    if (data) {
	memcpy(data, string, len);
	data[len] = '\0';
    }

    return pa_istr_atom_to_index(pip, atom);
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
pa_istr_init (pa_mmap_t *pmp, pa_istr_t *pip, const char *name,
	      pa_shift_t shift, uint16_t atom_shift, uint32_t max_atoms)
{
    shift = pa_config_value32(name, "shift", shift);
    atom_shift = pa_config_value32(name, "atom-shift", atom_shift);
    max_atoms = pa_config_value32(name, "max-atoms", max_atoms);

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

	/* Mark us empty */
	pip->pi_free = PA_NULL_ATOM;
    }

    /* Fill in the rest of fhe fields from the argument list */
    pip->pi_shift = shift;
    pip->pi_atom_shift = atom_shift;
    pip->pi_max_atoms = max_atoms;
    pip->pi_mmap = pmp;
}

pa_istr_t *
pa_istr_setup (pa_mmap_t *pmp, pa_istr_info_t *piip, const char *name,
	       pa_shift_t shift, uint16_t atom_shift, uint32_t max_atoms)
{
    pa_istr_t *pip = calloc(1, sizeof(*pip));
    char namebuf[PA_MMAP_HEADER_NAME_LEN];

    if (pip) {
	bzero(pip, sizeof(*pip));
	pip->pi_infop = piip;
	pip->pi_datap = &piip->pii_data;
	
	pa_config_name(namebuf, sizeof(namebuf), name, "raw");
	pa_istr_init(pmp, pip, namebuf, shift, atom_shift, max_atoms);

	/*
	 * Now we build the index, used to turn our externally visible
	 * index numbers into atoms in our underlaying data store.
	 */
	pa_config_name(namebuf, sizeof(namebuf), name, "index");
	pip->pi_index = pa_fixed_setup(pmp, &piip->pii_index, namebuf,
				       shift, sizeof(pa_atom_t), max_atoms);
	if (pip->pi_index == NULL) {
	    free(pip);
	    pip = NULL;
	}
    }

    return pip;
}

pa_istr_t *
pa_istr_open (pa_mmap_t *pmp, const char *name, pa_shift_t shift,
		 uint16_t atom_shift, uint32_t max_atoms)
{
    pa_istr_info_t *piip = NULL;

    if (name) {
	piip = pa_mmap_header(pmp, name, PA_TYPE_ISTR, 0, sizeof(*piip));
	if (piip == NULL) {
	    pa_warning(0, "pa_istr header not found: %s", name);
	    return NULL;
	}
    }

    return pa_istr_setup(pmp, piip, name, shift, atom_shift, max_atoms);
}

void
pa_istr_close (pa_istr_t *pip)
{
    pa_mmap_close(pip->pi_mmap);

    free(pip);
}
