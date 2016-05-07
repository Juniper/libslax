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

#ifndef LIBSLAX_PA_COMMON_H
#define LIBSLAX_PA_COMMON_H

/*
 * "atom" is a generic term.  Each memory allocator can (and does)
 * redefine the size and content of an "atom", but it's the smallest
 * size that can be allocated from that allocator.  It must be a
 * power-of-two in size.  Some allocators will have a fixed size,
 * while others use parameters.  All should be power-of-two based.
 *
 * "page" is a unit of higher allocation; it doesn't have to be the
 * machine's page, but should be a reasonable size.
 *
 * The page table is a table of atom numbers, so you mask off the
 * "page" bits, index into the table table for a page of atoms and
 * then mask off the "atom" bits to find the atom within that page.
 * The atom number can then be converted into a real address.
 *
 * Note that addresses cannot be tracked backward to page entries.
 *
 * Specific allocators can bend and break these rules as needed, with
 * custom definitions of the "page entry" type.
 */

typedef uint32_t pa_atom_t;	/* Type for atom numbers */
#define PA_NULL_ATOM	((pa_atom_t) 0)

/*
 * To distinquish between pa_mmap atoms and higher level atoms, we
 * call the former "matoms".  The types are equivalent, but the
 * shifting and meaning are different enough that I need distinct
 * type to keep them clear.
 */

typedef uint32_t pa_matom_t;	/* Identical to pa_atom_t; for mmap atoms */
#define PA_NULL_MATOM	((pa_matom_t) 0)

typedef uint32_t pa_page_t;	/* Type for page numbers */

/* An offset within the database */
typedef ptrdiff_t pa_offset_t;	/* Byte offset in the memory segment */

typedef uint8_t pa_shift_t;	/* Shift value */

#define PA_ASSERT(_a, _b) assert(_b)

static inline uint32_t
pa_roundup_shift32 (uint32_t val, pa_shift_t shift)
{
    return (val + (1 << shift) - 1) & ~((1 << shift) - 1);
}

static inline uint32_t
pa_roundup32 (uint32_t val, uint32_t rnd)
{
    return (val + rnd - 1) & ~(rnd - 1);
}

static inline void *
pa_pointer (void *base, pa_atom_t atom, pa_shift_t shift)
{
    uint8_t *cp = base;

    return &cp[atom << shift];
}

static inline pa_atom_t
pa_atom (void *base, void *cur, pa_shift_t shift)
{
    ptrdiff_t delta = (uint8_t *) cur - (uint8_t *) base;

    return delta >> shift;
}

/*
 * Cheesy breakpoint for memory allocation failure
 */
void
pa_alloc_failed (const char *msg);

/*
 * Generate a warning
 */
void
pa_warning (int errnum, const char *fmt, ...);

#endif /* LIBSLAX_PA_COMMON_H */
