/*
 * Copyright (c) 2016-2017, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer <phil@>, May 2016
 */

#ifndef PARROTDB_PAISTR_H
#define PARROTDB_PAISTR_H

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
 *
 * We use an index to give us a compact contiguous number space.  We
 * have also support short strings, where small numbers represent
 * strings of length zero or one.  
 */

/* Declare our wrapper type: istr is the external-facing type */
PA_ATOM_TYPE(pa_istr_atom_t, pa_istr_atom_s, pia_atom,
	     pa_istr_is_null, pa_istr_atom, pa_istr_null_atom);

/* Declare our wrapper type: istr_data is the internal-only data atom */
PA_ATOM_TYPE(pa_istr_data_atom_t, pa_istr_data_atom_s, pida_atom,
	     pa_istr_data_is_null, pa_istr_data_atom, pa_istr_data_null_atom);

/*
 * Since pa_istr_atom_t are really pa_fixed_atom_t, we need an easy way
 * to convert between them.
 */
static inline pa_fixed_atom_t
pa_istr_to_fixed (pa_istr_atom_t atom)
{
    return pa_fixed_atom(pa_istr_atom_of(atom));
}

static inline pa_istr_atom_t
pa_istr_from_fixed (pa_fixed_atom_t atom)
{
    return pa_istr_atom(pa_fixed_atom_of(atom));
}

typedef struct pa_istr_data_info_s {
    pa_shift_t pid_shift;	/* Bits to shift to select the page */
    uint8_t pid_padding;	/* Padding this by hand */
    uint16_t pid_atom_shift;	/* Size of each atom */
    pa_atom_t pid_max_atoms;	/* Max number of atoms */
    pa_istr_data_atom_t pid_free; /* First atom that is free */
    pa_atom_t pid_left;		/* Number of atoms left at free */
    pa_mmap_atom_t pid_base; 	/* Offset of page table base (in mmap atoms) */
} pa_istr_data_info_t;

/*
 * Our info contains both the info for our strings and the index
 */
typedef struct pa_istr_info_s {
    pa_fixed_info_t pii_index;	/* Packed array of string atoms */
    pa_istr_data_info_t pii_data; /* String data */
} pa_istr_info_t;

typedef struct pa_istr_s {
    pa_mmap_t *pi_mmap;		   /* Mmap pointer */
    pa_istr_info_t *pi_infop;	   /* Pointer to header */
    pa_istr_data_info_t *pi_datap; /* Data header (for pii_data) */
    pa_fixed_t *pi_index;	   /* Index of strings (for pii_index) */
    pa_mmap_atom_t *pi_base;	   /* Base of page table (in mmap atoms) */
} pa_istr_t;

/* Simplification macros, so we don't need to think about pi_datap */
#define pi_shift	pi_datap->pid_shift
#define pi_atom_shift	pi_datap->pid_atom_shift
#define pi_max_atoms	pi_datap->pid_max_atoms
#define pi_free		pi_datap->pid_free
#define pi_left		pi_datap->pid_left

/*
 * Record the page table as both an atom (in the info) and a pointer
 * (in the pa_istr_t).
 * @param[in] pip base istr structure
 * @param[in] matom mmap atom
 * @param[in] base pointer to base memory
 */
static inline void
pa_istr_base_set (pa_istr_t *pip, pa_mmap_atom_t matom,
		   pa_mmap_atom_t *base)
{
    pip->pi_base = base;
    pip->pi_datap->pid_base = matom;
}

/*
 * Our pages are directly allocated from mmap, so we can use the mmap
 * address function to get a real address.
 */
static inline void *
pa_istr_page_get (pa_istr_t *pip, pa_page_t page)
{
    if (pa_mmap_is_null(pip->pi_base[page]))
	return NULL;

    return pa_mmap_addr(pip->pi_mmap, pip->pi_base[page]);
}

static inline void
pa_istr_page_set (pa_istr_t *pip, pa_page_t page, pa_mmap_atom_t matom)
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
pa_istr_data_atom_addr_direct (pa_istr_t *pip, uint8_t shift,
			       uint16_t atom_shift, uint32_t max_atoms,
			       pa_istr_data_atom_t atom)
{
    pa_atom_t raw = pa_istr_data_atom_of(atom);
    if (pa_istr_data_is_null(atom) || raw >= max_atoms)
	return NULL;

    psu_byte_t *addr;
    addr = pa_istr_page_get(pip, raw >> shift);
    if (addr == NULL)
	return NULL;

    /*
     * We have the base address of the page, and need to find the
     * address of our atom inside the page.
     */
    return &addr[(raw & ((1 << shift) - 1)) << atom_shift];
}

/*
 * Return the address of an atom in a paged table array using the info
 * recorded in our header.
 */
static inline void *
pa_istr_data_atom_addr (pa_istr_t *pip, pa_istr_data_atom_t atom)
{
    if (pip->pi_base == NULL)
	return NULL;

    return pa_istr_data_atom_addr_direct(pip, pip->pi_shift,
					 pip->pi_atom_shift,
					 pip->pi_max_atoms, atom);
}

static inline pa_istr_data_atom_t
pa_istr_index_to_atom (pa_istr_t *pip, pa_istr_atom_t iatom)
{
    pa_fixed_atom_t fa = pa_istr_to_fixed(iatom);

    /*
     * Since we know we've already skipped the "short strings" (one
     * byte long ones), we reduce our atom number since we're not
     * keeping these string in the pi_index array.
     */
    fa.pfa_atom -= PA_SHORT_STRINGS_MAX; /* Skip over short strings */

    pa_istr_data_atom_t *ap = pa_fixed_atom_addr(pip->pi_index, fa);
    return ap ? *ap : pa_istr_data_null_atom();
}

static inline pa_istr_atom_t
pa_istr_atom_to_index (pa_istr_t *pip, pa_istr_data_atom_t atom)
{
    pa_fixed_atom_t fa = pa_fixed_alloc_atom(pip->pi_index);
    pa_istr_data_atom_t *ap = pa_fixed_atom_addr(pip->pi_index, fa);
    if (ap == NULL)
	return pa_istr_null_atom();

    *ap = atom;			/* Record in index table */

    pa_istr_atom_t iatom = pa_istr_from_fixed(fa);

    /* We return an istr atom number that skips over the short strings */
    iatom.pia_atom += PA_SHORT_STRINGS_MAX;
    return iatom;
}

static inline const char *
pa_istr_atom_string (pa_istr_t *pip, pa_istr_atom_t iatom)
{
    if (pa_istr_is_null(iatom))
	return NULL;

    /* Handle "short strings" (one byte strings) in optimized manner */
    if (iatom.pia_atom < PA_SHORT_STRINGS_MAX)
	return pa_short_string(iatom.pia_atom);

    pa_istr_data_atom_t atom = pa_istr_index_to_atom(pip, iatom);

    return pa_istr_data_atom_addr(pip, atom);
}

pa_istr_atom_t
pa_istr_nstring_alloc (pa_istr_t *pip, const char *string, size_t len);

static inline pa_istr_atom_t
pa_istr_nstring (pa_istr_t *pip, const char *string, size_t len)
{
    if (string == NULL)
	return pa_istr_null_atom();

    /*
     * Strings that are length 1 are handled specifically
     */
    if (len <= 1)
	return pa_istr_atom(pa_short_string_atom(string));

    unsigned num_atoms = pa_items_shift32(len + 1, pip->pi_atom_shift);
    if (num_atoms <= pip->pi_left) {
	/* Easy case */
	pip->pi_left -= num_atoms;

	pa_istr_data_atom_t atom = pip->pi_free;
	atom.pida_atom += pip->pi_left;

	char *data = pa_istr_data_atom_addr(pip, atom);
	if (data) {
	    memcpy(data, string, len);
	    data[len] = '\0';
	}

	/* Allocate an istr to hold our istr_data */
	return pa_istr_atom_to_index(pip, atom);
    }

    /* Pass it off to the real allocator */
    return pa_istr_nstring_alloc(pip, string, len);
}

static inline pa_istr_atom_t
pa_istr_string (pa_istr_t *pip, const char *string)
{
    return pa_istr_nstring(pip, string, string ? strlen(string) : 0);
}

/*
 * Shortcut to return both atom and addr
 */
static inline const char *
pa_istr_new (pa_istr_t *pip, const char *string, pa_istr_atom_t *atomp)
{
    pa_istr_atom_t atom;

    atom = pa_istr_nstring(pip, string, string ? strlen(string) : 0);
    if (atomp)
	*atomp = atom;

    return pa_istr_atom_string(pip, atom);
}

void
pa_istr_init_from_block (pa_istr_t *pip, void *base, pa_istr_info_t *infop);

void
pa_istr_init (pa_mmap_t *pmp, pa_istr_t *pip, const char *name,
	      pa_shift_t shift, uint16_t atom_shift, uint32_t max_atoms);

pa_istr_t *
pa_istr_setup (pa_mmap_t *pmp, pa_istr_info_t *pidp, const char *name,
	       pa_shift_t shift, uint16_t atom_shift, uint32_t max_atoms);

pa_istr_t *
pa_istr_open (pa_mmap_t *pmp, const char *name, pa_shift_t shift,
	       uint16_t atom_shift, uint32_t max_atoms);

void
pa_istr_close (pa_istr_t *pip);

void
pa_istr_dump (pa_istr_t *pip, psu_boolean_t full);

#endif /* PARROTDB_PAISTR_H */
