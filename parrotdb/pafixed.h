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

#ifndef PARROTDB_PAFIXED_H
#define PARROTDB_PAFIXED_H

/**
 * Paged arrays are fixed size arrays, allocated piecemeal
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
 * the atoms, which are recorded in the base pa_fixed_t structure.
 * Since the caller knows these values, they can either let us get
 * them from the struct or pass them directly to the inline functions
 * (e.g. pa_atom_addr_direct) for better performance.
 *
 * Be aware that since we're layering our "atoms" on top of mmap's
 * "atoms", we need to keep our thinking clear in terms of whose atoms
 * are whose.  Our "pages" are their "atoms".  We then divide each
 * page into our fixed-sized atoms.
 */

/* Declare our wrapper type */
PA_ATOM_TYPE(pa_fixed_atom_t, pa_fixed_atom_s, pfa_atom,
	     pa_fixed_is_null, pa_fixed_atom, pa_fixed_atom_of,
	     pa_fixed_null_atom);

typedef uint8_t pa_fixed_flags_t;

typedef struct pa_fixed_info_s {
    pa_shift_t pfi_shift;	/* Bits to shift to select the page */
    pa_fixed_flags_t pfi_flags;	/* Flags (in the padding) */
    uint16_t pfi_atom_size;	/* Size of each atom */
    pa_atom_t pfi_max_atoms;	/* Max number of atoms */
    pa_fixed_atom_t pfi_free;	/* First atom that is free */
    pa_mmap_atom_t pfi_base; 	/* Offset of page table base (in mmap atoms) */
} pa_fixed_info_t;

/* Flags for pfi_flags: */
#define PFF_INIT_ZERO	(1<<0)	/* Initialize memory to zeroes */

typedef struct pa_fixed_s {
    pa_mmap_t *pf_mmap;		   /* Mmap overhead declarations */
    pa_fixed_info_t *pf_infop;	   /* Pointer to real block */
    pa_mmap_atom_t *pf_base;	   /* Pointer to base of page table */
} pa_fixed_t;

/* Simplification macros, so we don't need to think about pf_infop */
#define pf_shift	pf_infop->pfi_shift
#define pf_atom_size	pf_infop->pfi_atom_size
#define pf_max_atoms	pf_infop->pfi_max_atoms
#define pf_free		pf_infop->pfi_free
#define pf_flags	pf_infop->pfi_flags

static inline pa_atom_t
pa_fixed_max_atoms (pa_fixed_t *pfp)
{
    return pfp->pf_max_atoms;
}

/*
 * Our pages are directly allocated from mmap, so we can use the mmap
 * address function to get a real address.
 */
static inline void *
pa_fixed_page_get (pa_fixed_t *pfp, pa_page_t page)
{
    if (pa_mmap_is_null(pfp->pf_base[page]))
	return NULL;

    return pa_mmap_addr(pfp->pf_mmap, pfp->pf_base[page]);
}

static inline void
pa_fixed_page_set (pa_fixed_t *pfp, pa_page_t page,
		   pa_mmap_atom_t matom, void *addr UNUSED)
{
    pfp->pf_base[page] = matom;
}

/*
 * Return the address of an atom in a paged table array.  This
 * version takes all information as arguments, so one can truly
 * get inline access.  Suitable for wrapping in other inlines
 * with constants for any specific pa_fixed_t's parameters.
 */
static inline void *
pa_fixed_atom_addr_direct (pa_fixed_t *pfp, uint8_t shift, uint16_t atom_size,
			   uint32_t max_atoms, pa_fixed_atom_t atom)
{
    uint8_t *addr;
    if (pa_fixed_is_null(atom) || atom.pfa_atom >= max_atoms)
	return NULL;

    addr = pa_fixed_page_get(pfp, atom.pfa_atom >> shift);
    if (addr == NULL)
	return NULL;

    /*
     * We have the base address of the page, and need to find the
     * address of our atom inside the page.
     */
    return &addr[(atom.pfa_atom & ((1 << shift) - 1)) * atom_size];
}

/*
 * Return the address of an atom in a paged table array using the info
 * recorded in our header.
 */
static inline void *
pa_fixed_atom_addr (pa_fixed_t *pfp, pa_fixed_atom_t atom)
{
    if (pfp->pf_base == NULL)
	return NULL;

    return pa_fixed_atom_addr_direct(pfp, pfp->pf_shift, pfp->pf_atom_size,
			       pfp->pf_max_atoms, atom);
}

void
pa_fixed_alloc_setup_page (pa_fixed_t *pfp, pa_fixed_atom_t atom);

void
pa_fixed_element_setup_page (pa_fixed_t *pfp, pa_fixed_atom_t atom);

/*
 * Allocate a new atom, returning the atom number
 */
static inline pa_fixed_atom_t
pa_fixed_alloc_atom (pa_fixed_t *pfp)
{
    if (pfp->pf_base == NULL)
	return pa_fixed_null_atom();

    /* free == PA_NULL_ATOM -> nothing available */
    pa_fixed_atom_t atom = pfp->pf_free;
    if (pa_fixed_is_null(atom))
	return pa_fixed_null_atom();

    /* Take the next atom off the free list and return it */
    void *addr = pa_fixed_atom_addr(pfp, atom);
    if (addr == NULL) {
	/*
	 * The atom number was on the free list but we got NULL as the
	 * address.  The only way this happens is when the page is
	 * freshly unallocated.  So we force it to be allocated and
	 * re-fetch the address, which may fail again if the underlaying
	 * allocator (mmap) can't allocate memory.
	 */
	pa_fixed_alloc_setup_page(pfp, atom);
	addr = pa_fixed_atom_addr(pfp, atom);
    }

    /*
     * If we failed, we don't want to change the free atom, since it
     * might be a transient memory issue.
     */
    if (addr == NULL) {
	pa_alloc_failed(__FUNCTION__);
	return pa_fixed_null_atom();
    }

    /* Fetch the next free atom, which is stored at the start of this atom */
    pfp->pf_free = *(pa_fixed_atom_t *) addr;

    /* If needed, initialize the new memory to zero */
    if (pfp->pf_flags & PFF_INIT_ZERO)
	bzero(addr, pfp->pf_atom_size);

    return atom;
}

/*
 * Return the address of a given element on the paged array.  This
 * is for callers that don't want to use the "alloc/free" style,
 * but just want a paged array they can treat as a paged array.
 */
static inline void *
pa_fixed_element (pa_fixed_t *pfp, uint32_t num)
{
    /* The index is the atom number */
    pa_fixed_atom_t atom = pa_fixed_atom(num);
    void *addr = pa_fixed_atom_addr(pfp, atom);
    if (addr == NULL) {
	/*
	 * Force an allocation and re-fetch the address, which may
	 * fail again if the underlaying allocator (mmap) can't
	 * allocate memory.
	 */
	pa_fixed_element_setup_page(pfp, atom);
	addr = pa_fixed_atom_addr(pfp, atom);
    }

    if (addr == NULL)
	pa_alloc_failed(__FUNCTION__);

    return addr;
}

/*
 * Return the address of a given element on the paged array, if and only
 * if that page is already allocated.  This allows the caller to avoid
 * spurious creation/allocation if they are just checking for existance.
 */
static inline void *
pa_fixed_element_if_exists (pa_fixed_t *pfp, uint32_t num)
{
    /* The index is the atom number */
    return pa_fixed_atom_addr(pfp, pa_fixed_atom(num));
}

/*
 * Put a fixed atom on our free list
 */
static inline void
pa_fixed_free_atom (pa_fixed_t *pfp, pa_fixed_atom_t atom)
{
    if (pa_fixed_is_null(atom))
	return;

    /* The free list is a single-linked list of atoms */
    pa_fixed_atom_t *addr = pa_fixed_atom_addr(pfp, atom);
    if (addr == NULL)
	return;

    /* Add the atom to the front of the free list */
    *addr = pfp->pf_free;
    pfp->pf_free = atom;
}

void
pa_fixed_init_from_block (pa_fixed_t *pfp, void *base,
			  pa_fixed_info_t *infop);

void
pa_fixed_init (pa_mmap_t *pmp, pa_fixed_t *pfp, const char *name,
	       pa_shift_t shift, uint16_t atom_size, uint32_t max_atoms);

pa_fixed_t *
pa_fixed_setup (pa_mmap_t *pmp, pa_fixed_info_t *pfip, const char *name,
		pa_shift_t shift, uint16_t atom_size, uint32_t max_atoms);

pa_fixed_t *
pa_fixed_open (pa_mmap_t *pmp, const char *name, pa_shift_t shift,
	       uint16_t atom_size, uint32_t max_atoms);

void
pa_fixed_close (pa_fixed_t *pfp);

static inline void
pa_fixed_set_flags (pa_fixed_t *pfp, pa_fixed_flags_t flags)
{
    pfp->pf_flags |= flags;
}

static inline void
pa_fixed_clear_flags (pa_fixed_t *pfp, pa_fixed_flags_t flags)
{
    pfp->pf_flags &= ~flags;
}

static inline pa_boolean_t
pa_fixed_test_flags (pa_fixed_t *pfp, pa_fixed_flags_t flags)
{
    return (pfp->pf_flags & flags) ? 1 : 0;
}

/*
 * Define a set of functions for a PA_ATOM_TYPE that call allocate
 * a free atoms, as well as turn the atom into a typed pointer.
 */
#define PA_FIXED_FUNCTIONS(_atom_type, _type, _base, _field,		\
	   _alloc_fn, _free_fn, _addr_fn, _build_fn, _is_null_fn)	\
static inline _type *							\
_alloc_fn (_base *basep, _atom_type *atomp)				\
{									\
    if (atomp == NULL)		/* Should not occur */			\
	return NULL;							\
									\
    pa_fixed_atom_t atom = pa_fixed_alloc_atom(basep->_field);		\
    _type *datap = pa_fixed_atom_addr(basep->_field, atom);		\
									\
    *atomp = _build_fn(atom);						\
    return datap;							\
}									\
									\
static inline void							\
_free_fn (_base *basep, _atom_type atom)				\
{									\
    if (_is_null_fn(atom))		/* Should not occur */		\
	return;								\
									\
    pa_fixed_free_atom(basep->_field, _build_fn##_of(atom));		\
}									\
									\
static inline _type *							\
_addr_fn (_base *basep, _atom_type atom)				\
{									\
    return pa_fixed_atom_addr(basep->_field, _build_fn##_of(atom));	\
}

#endif /* PARROTDB_PAFIXED_H */
