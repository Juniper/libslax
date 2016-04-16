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

#ifndef LIBSLAX_PAGED_ARRAY_H
#define LIBSLAX_PAGED_ARRAY_H

/**
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
 * the atoms, which are recorded in the base paged_array_t structure.
 * Since the caller knows these values, they can either let us get
 * them from the struct or pass them directly to the inline functions
 * (e.g. pa_atom_addr_direct) for better performance.
 *
 * These functions are designed to point into memory mapped addresses,
 * so they allow the caller to give us not only allocation and free
 * functions, but an opaque 'void *' parameter that we pass on to
 * these functions.  We also allow the call to either use the info
 * block that is built into the structure (pa_info_block) or to set
 * their own (see pa_init_from_block).
 */

#define PA_ASSERT(_a, _b) assert(_b)

typedef uint32_t pa_atom_t;	/* Type for atom numbers */
#define PA_NULL_ATOM (pa_atom_t) 0
typedef uint32_t pa_page_t;	/* Type for page numbers */

typedef void *(*pa_alloc_func_t)(void *, size_t);
typedef void (*pa_free_func_t)(void *, void *);
typedef void *(*pa_page_get_func_t)(void *, void *, pa_page_t);
typedef void (*pa_page_set_func_t)(void *, void *, pa_page_t, void *);
typedef void (*pa_base_set_func_t)(void *, void *);

typedef struct paged_array_info_s {
    uint8_t pai_shift;		/* Bits to shift to select the page */
    uint16_t pai_atom_size;	/* Size of each atom */
    pa_atom_t pai_max_atoms;	/* Max number of atoms */
    pa_atom_t pai_free;		/* First atom that is free */
} paged_array_info_t;

typedef struct paged_array_s {
    paged_array_info_t pa_info_block;
    paged_array_info_t *pa_infop;
    void *pa_base;		/* Address of page table */
    pa_base_set_func_t pa_base_set_fn; /* Called after setting base address */
    pa_page_get_func_t pa_page_get_fn; /* Get a page's address */
    pa_page_set_func_t pa_page_set_fn; /* Get a page's address */
    pa_alloc_func_t pa_alloc_fn; /* Allocate memory */
    pa_free_func_t pa_free_fn;	 /* Free memory */
    void *pa_mem_opaque;	 /* Opaque data passed to pa_{alloc,free}_fn */
} paged_array_t;

/* Simplification macros */
#define pa_shift	pa_infop->pai_shift
#define pa_atom_size	pa_infop->pai_atom_size
#define pa_max_atoms	pa_infop->pai_max_atoms
#define pa_free		pa_infop->pai_free

/* Inlines for common and annoying operations */

static inline void *
pa_page_get (paged_array_t *pap, pa_page_t page)
{
    return pap->pa_page_get_fn(pap->pa_mem_opaque, pap->pa_base, page);
}

static inline void
pa_page_set (paged_array_t *pap, pa_page_t page, void *addr)
{
    pap->pa_page_set_fn(pap->pa_mem_opaque, pap->pa_base, page, addr);
}

/*
 * Return the address of an atom in a paged table array
 */
static inline void *
pa_atom_addr_direct (paged_array_t *pap, uint8_t shift, uint16_t atom_size,
		     uint32_t max_atoms, pa_atom_t atom)
{
    uint8_t *page;
    if (atom == 0 || atom >= max_atoms)
	return NULL;

    page = pap->pa_page_get_fn(pap->pa_mem_opaque, pap->pa_base, atom >> shift);
    if (page == NULL)
	return NULL;

    return &page[(atom & ((1 << shift) - 1)) * atom_size];
}

/*
 * Return the address of an atom in a paged table array
 */
static inline void *
pa_atom_addr (paged_array_t *pap, uint32_t atom)
{
    if (pap->pa_base == NULL)
	return NULL;

    return pa_atom_addr_direct(pap, pap->pa_shift, pap->pa_atom_size,
			       pap->pa_max_atoms, atom);
}

void
pa_alloc_page (paged_array_t *pap, pa_atom_t atom);

/*
 * Cheesy breakpoint for memory allocation failure
 */
void
pa_alloc_failed (paged_array_t *pap);

/*
 * Allocate a new atom, returning the atom number
 */
static inline pa_atom_t
pa_alloc_atom (paged_array_t *pap)
{
    /* free == PA_NULL_ATOM -> nothing available */
    if (pap->pa_free == PA_NULL_ATOM || pap->pa_base == NULL)
	return PA_NULL_ATOM;

    /* Take the next atom off the free list and return it */
    pa_atom_t atom = pap->pa_free;
    void *addr = pa_atom_addr(pap, atom);
    if (addr == NULL) {
	pa_alloc_page(pap, atom);
	addr = pa_atom_addr(pap, atom);
    }

    /*
     * Fetch the next free atom from the start of this atom.  If we
     * failed, we don't want to change the free atom, since it might
     * be a transient memory issue.
     */
    if (addr)
	pap->pa_free = *(pa_atom_t *) addr;
    else
	pa_alloc_failed(pap);

    return atom;
}

static inline void
pa_free_atom (paged_array_t *pap, pa_atom_t atom)
{
    if (atom == PA_NULL_ATOM)
	return;

    pa_atom_t *addr = pa_atom_addr(pap, atom);
    if (addr == NULL)
	return;

    /* Add the atom to the front of the free list */
    *addr = pap->pa_free;
    pap->pa_free = atom;
}

void
pa_init_from_block (paged_array_t *pap, void *base,
		    paged_array_info_t *infop,
		    pa_base_set_func_t base_set_fn,
		    pa_page_get_func_t page_get_fn,
		    pa_page_set_func_t page_set_fn, 
		    pa_alloc_func_t alloc_fn, pa_free_func_t free_fn,
		    void *mem_opaque);

void
pa_init (paged_array_t *pap, void *base, uint8_t shift,
	 uint16_t atom_size, uint32_t max_atoms,
	 pa_base_set_func_t base_set_fn,
	 pa_page_get_func_t page_get_fn, pa_page_set_func_t page_set_fn, 
	 pa_alloc_func_t alloc_fn, pa_free_func_t free_fn, void *mem_opaque);

paged_array_t *
pa_create (uint8_t shift, uint16_t atom_size, uint32_t max_atoms,
	   pa_base_set_func_t base_set_fn,
	   pa_page_get_func_t page_get_fn, pa_page_set_func_t page_set_fn, 
	   pa_alloc_func_t alloc_fn, pa_free_func_t free_fn, void *mem_opaque);

static inline paged_array_t *
pa_create_simple (uint8_t shift, uint16_t atom_size, uint32_t max_atoms)
{
    return pa_create(shift, atom_size, max_atoms,
		     NULL, NULL, NULL, NULL, NULL, NULL);
}

#endif /* LIBSLAX_PAGED_ARRAY_H */
