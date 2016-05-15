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

#ifndef LIBSLAX_PA_ISTR_H
#define LIBSLAX_PA_ISTR_H

/**
 * Paged arrays are istr size arrays, allocated piecemeal
 * to reduce their initial memory impact.
 *
 * Glossary:
 * atom = smallest addressable unit.  You are making an arrray of
 *     these atoms.  Atom size is determined by the user by
 *     in the atom_size parameter.
 * page = a set of atoms.  The number of atom per page is given in
 *     the shift parameter: a page hold 1<<shift atoms.
 * page_arrray = an array of atoms, split into pages to avoid
 *     forcing a large memory footprint for an array that _might_
 *     be large.
 * base = the array of pointers to pages
 *
 * The paged array works like a page table; part of the identifier
 * selects a page of values, while the remainder selects an atom off
 * that page.  Note that it's an atom array, not a byte array.  The
 * caller sets the bit shift (page size) and the number and size of
 * the atoms, which are recorded in the base pa_istr_t structure.
 * Since the caller knows these values, they can either let us get
 * them from the struct or pass them directly to the inline functions
 * (e.g. pa_atom_addr_direct) for better performance.
 *
 * Be aware that since we're layering our "atoms" on top of mmap's
 * "atoms", we need to keep our thinking clear in terms of whose atoms
 * are whose.  Our "pages" are their "atoms".  We then divide each
 * page into our istr-sized atoms.
 */

/*
 * These functions are designed to point into memory mapped addresses,
 * but can be used without one, since I wanted them to be useful in
 * more broad circumstances.  The macros in this section allow us
 * to make both sets of functions off one source file.
 */

#ifdef PA_NO_MMAP
#define PA_ISTR_MMAP_FIELD_DECLS /* Nothing */

typedef void *pa_istr_page_entry_t;

#else /* PA_NO_MMAP */

#define PA_ISTR_MMAP_FIELD_DECLS pa_mmap_t *pi_mmap

typedef pa_atom_t pa_istr_page_entry_t;

#endif /* PA_NO_MMAP */

typedef struct pa_istr_info_s {
    pa_shift_t pfi_shift;	/* Bits to shift to select the page */
    uint8_t pfi_padding;	/* Padding this by hand */
    uint16_t pfi_atom_size;	/* Size of each atom */
    pa_atom_t pfi_max_atoms;	/* Max number of atoms */
    pa_atom_t pfi_free;		/* First atom that is free */
    pa_matom_t pfi_base; 	/* Offset of page table base (in mmap atoms) */
} pa_istr_info_t;

typedef struct pa_istr_s {
    PA_ISTR_MMAP_FIELD_DECLS;	   /* Mmap overhead declarations */
    pa_istr_info_t pi_info_block; /* Simple block (not used for mmap) */
    pa_istr_info_t *pi_infop;	   /* Pointer to real block */
    pa_istr_page_entry_t *pi_base; /* Base of page table */
} pa_istr_t;

/* Simplification macros, so we don't need to think about pi_infop */
#define pi_shift	pi_infop->pfi_shift
#define pi_atom_size	pi_infop->pfi_atom_size
#define pi_max_atoms	pi_infop->pfi_max_atoms
#define pi_free		pi_infop->pfi_free

#ifdef PA_NO_MMAP

static inline void
pa_istr_set_base (pa_istr_t *pip, pa_atom_t atom UNUSED,
		   pa_istr_page_entry_t *base)
{
    pip->pi_base = base;
}

static inline void *
pa_istr_page_get (pa_istr_t *pip, pa_page_t page)
{
    return pip->pi_base[page];
}

static inline void
pa_istr_page_set (pa_istr_t *pip, pa_page_t page, void *addr)
{
    pip->pi_base[page] = addr;
}

#else /* PA_NO_MMAP */

static inline void
pa_istr_base_set (pa_istr_t *pip, pa_matom_t matom,
		   pa_istr_page_entry_t *base)
{
    pip->pi_base = base;
    pip->pi_infop->pfi_base = matom;
}

/*
 * Our pages are directly allocated from mmap, so we can use the mmap
 * address function to get a real address.
 */
static inline void *
pa_istr_page_get (pa_istr_t *pip, pa_page_t page)
{
    if (pip->pi_base[page] == PA_NULL_ATOM)
	return NULL;

    return pa_mmap_addr(pip->pi_mmap, pip->pi_base[page]);
}

static inline void
pa_istr_page_set (pa_istr_t *pip, pa_page_t page,
		   pa_matom_t matom, void *addr UNUSED)
{
    pip->pi_base[page] = matom;
}

#endif /* PA_NO_MMAP */

/*
 * Return the address of an atom in a paged table array.  This
 * version takes all information as arguments, so one can truly
 * get inline access.  Suitable for wrapping in other inlines
 * with constants for any specific pa_istr_t's parameters.
 */
static inline void *
pa_istr_atom_addr_direct (pa_istr_t *pip, uint8_t shift, uint16_t atom_size,
			   uint32_t max_atoms, pa_atom_t atom)
{
    uint8_t *addr;
    if (atom == 0 || atom >= max_atoms)
	return NULL;

    addr = pa_istr_page_get(pip, atom >> shift);
    if (addr == NULL)
	return NULL;

    /*
     * We have the base address of the page, and need to find the
     * address of our atom inside the page.
     */
    return &addr[(atom & ((1 << shift) - 1)) * atom_size];
}

/*
 * Return the address of an atom in a paged table array using the info
 * recorded in our header.
 */
static inline void *
pa_istr_atom_addr (pa_istr_t *pip, pa_atom_t atom)
{
    if (pip->pi_base == NULL)
	return NULL;

    return pa_istr_atom_addr_direct(pip, pip->pi_shift, pip->pi_atom_size,
			       pip->pi_max_atoms, atom);
}

void
pa_istr_alloc_setup_page (pa_istr_t *pip, pa_atom_t atom);

/*
 * Allocate a new atom, returning the atom number
 */
static inline pa_atom_t
pa_istr_alloc_atom (pa_istr_t *pip)
{
    /* free == PA_NULL_ATOM -> nothing available */
    if (pip->pi_free == PA_NULL_ATOM || pip->pi_base == NULL)
	return PA_NULL_ATOM;

    /* Take the next atom off the free list and return it */
    pa_atom_t atom = pip->pi_free;
    void *addr = pa_istr_atom_addr(pip, atom);
    if (addr == NULL) {
	/*
	 * The atom number was on the free list but we got NULL as the
	 * address.  The only way this happens is when the page is
	 * freshly unallocated.  So we force it to be allocated and
	 * re-fetch the address, which may fail again if the underlaying
	 * allocator (mmap) can't allocate memory.
	 */
	pa_istr_alloc_setup_page(pip, atom);
	addr = pa_istr_atom_addr(pip, atom);
    }

    /*
     * Fetch the next free atom from the start of this atom.  If we
     * failed, we don't want to change the free atom, since it might
     * be a transient memory issue.
     */
    if (addr)
	pip->pi_free = *(pa_atom_t *) addr; /* Fetch next item on free list */
    else
	pa_alloc_failed(__FUNCTION__);

    return atom;
}

/*
 * Put a istr atom on our free list
 */
static inline void
pa_istr_free_atom (pa_istr_t *pip, pa_atom_t atom)
{
    if (atom == PA_NULL_ATOM)
	return;

    pa_atom_t *addr = pa_istr_atom_addr(pip, atom);
    if (addr == NULL)
	return;

    /* Add the atom to the front of the free list */
    *addr = pip->pi_free;
    pip->pi_free = atom;
}

void
pa_istr_init_from_block (pa_istr_t *pip, void *base,
			  pa_istr_info_t *infop);

void
pa_istr_init (pa_mmap_t *pmp, pa_istr_t *pip, pa_shift_t shift,
	       uint16_t atom_size, uint32_t max_atoms);

pa_istr_t *
pa_istr_setup (pa_mmap_t *pmp, pa_istr_info_t *pfip, pa_shift_t shift,
		uint16_t atom_size, uint32_t max_atoms);

pa_istr_t *
pa_istr_open (pa_mmap_t *pmp, const char *name, pa_shift_t shift,
	       uint16_t atom_size, uint32_t max_atoms);

void
pa_istr_close (pa_istr_t *pip);

#endif /* LIBSLAX_PA_ISTR_H */
