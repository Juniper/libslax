/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer <phil@>, May 2016
 */

#ifndef LIBSLAX_PA_ISTR_H
#define LIBSLAX_PA_ISTR_H

/**
 * pa_istr is an immutable string, a string library based on paged
 * arrays. They are immutable in the sense that they can only be
 * allocated, not freed.  All allocations are recorded (in the page
 * table) so the _entire_ thing can be freed, but not individual
 * strings.  Refer to pa_fixed.h for additional information.
 *
 * In addition to variable strings, there are also a set of fixed
 * strings with fixed atom numbers, for one-character strings and
 * other common values.
 */

typedef pa_atom_t pa_istr_page_entry_t;

typedef struct pa_istr_info_s {
    pa_shift_t pii_shift;	/* Bits to shift to select the page */
    uint8_t pii_padding;	/* Padding this by hand */
    uint16_t pii_atom_shift;	/* Size of each atom */
    pa_atom_t pii_max_atoms;	/* Max number of atoms */
    pa_atom_t pii_free;		/* First atom that is free */
    pa_atom_t pii_left;		/* Number of atoms left at free */
    pa_matom_t pii_base; 	/* Offset of page table base (in mmap atoms) */
} pa_istr_info_t;

typedef struct pa_istr_s {
    pa_mmap_t *pi_mmap;		  /* Mmap pointer */
    pa_istr_info_t pi_info_block; /* Simple block (not used for mmap) */
    pa_istr_info_t *pi_infop;	   /* Pointer to real block */
    pa_istr_page_entry_t *pi_base; /* Base of page table */
} pa_istr_t;

/* Simplification macros, so we don't need to think about pi_infop */
#define pi_shift	pi_infop->pii_shift
#define pi_atom_shift	pi_infop->pii_atom_shift
#define pi_max_atoms	pi_infop->pii_max_atoms
#define pi_free		pi_infop->pii_free
#define pi_left		pi_infop->pii_left

static inline void
pa_istr_base_set (pa_istr_t *pip, pa_matom_t matom,
		   pa_istr_page_entry_t *base)
{
    pip->pi_base = base;
    pip->pi_infop->pii_base = matom;
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
pa_istr_page_set (pa_istr_t *pip, pa_page_t page, pa_matom_t matom)
{
    pip->pi_base[page] = matom;
}

/*
 * Return the address of an atom in a paged table array.  This
 * version takes all information as arguments, so one can truly
 * get inline access.  Suitable for wrapping in other inlines
 * with constants for any specific pa_istr_t's parameters.
 */
static inline void *
pa_istr_atom_addr_direct (pa_istr_t *pip, uint8_t shift, uint16_t atom_shift,
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
    return &addr[(atom & ((1 << shift) - 1)) << atom_shift];
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

    return pa_istr_atom_addr_direct(pip, pip->pi_shift, pip->pi_atom_shift,
			       pip->pi_max_atoms, atom);
}

static inline const char *
pa_istr_atom_string (pa_istr_t *pip, pa_atom_t atom)
{
    if (atom == PA_NULL_ATOM)
	return NULL;

    if (atom < PA_SHORT_STRINGS_MAX)
	return pa_short_string(atom);

    return pa_istr_atom_addr(pip, atom);
}

pa_atom_t
pa_istr_nstring_alloc (pa_istr_t *pip, const char *string, size_t len);

static inline pa_atom_t
pa_istr_nstring (pa_istr_t *pip, const char *string, size_t len)
{
    if (string == NULL)
	return PA_NULL_ATOM;

    /*
     * Strings that are length 1 are handled specifically
     */
    if (len <= 1)
	return pa_short_string_atom(string);

    unsigned num_atoms = pa_items_shift32(len + 1, pip->pi_atom_shift);
    if (num_atoms <= pip->pi_left) {
	/* Easy case */
	pip->pi_left -= num_atoms;
	pa_atom_t atom = pip->pi_free + pip->pi_left;
	char *data = pa_istr_atom_addr(pip, atom);
	if (data) {
	    memcpy(data, string, len);
	    data[len] = '\0';
	}
	return atom;
    }

    /* Pass it off to the real allocator */
    return pa_istr_nstring_alloc(pip, string, len);
}

static inline pa_atom_t
pa_istr_string (pa_istr_t *pip, const char *string)
{
    return pa_istr_nstring(pip, string, string ? strlen(string) : 0);
}

/*
 * Shortcut to return both atom and addr
 */
static inline const char *
pa_istr_new (pa_istr_t *pip, const char *string, pa_atom_t *atomp)
{
    pa_atom_t atom = pa_istr_nstring(pip, string, string ? strlen(string) : 0);
    if (atomp)
	*atomp = atom;
    return pa_istr_atom_string(pip, atom);
}

void
pa_istr_init_from_block (pa_istr_t *pip, void *base,
			  pa_istr_info_t *infop);

void
pa_istr_init (pa_mmap_t *pmp, pa_istr_t *pip, pa_shift_t shift,
	       uint16_t atom_shift, uint32_t max_atoms);

pa_istr_t *
pa_istr_setup (pa_mmap_t *pmp, pa_istr_info_t *piip, pa_shift_t shift,
		uint16_t atom_shift, uint32_t max_atoms);

pa_istr_t *
pa_istr_open (pa_mmap_t *pmp, const char *name, pa_shift_t shift,
	       uint16_t atom_shift, uint32_t max_atoms);

void
pa_istr_close (pa_istr_t *pip);

#endif /* LIBSLAX_PA_ISTR_H */
