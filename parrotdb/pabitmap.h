/*
 * Copyright (c) 2016-2017, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer (phil@) June 2016
 *
 *
 * Bitmaps are arrays of bits, suitable for bulk booleans.  Built on
 * top fixed page arrays ("pa_fixed_t"), it uses blocks of bits as the
 * elements of the arrays, as well as using those blocks (of the exact
 * same size) for index tables, effectively adding a second layer of
 * indirection, in accordance with Wheeler's FTSE.  The pa_pmap_t gives
 * us a large fixed chunk, which pa_fixed_t divides into small chunks,
 * which we use at a table of atoms of small chunks that hold the actual
 * bits.
 *
 * The formula for max bits for any PA_BITMAP_BLOCK_SHIFT is:
 *    (((1 << shift) * PA_NBBY) * ((1 << shift) / sizeof(pa_atom_t)))
 * Counter that against needing 2 1<<shift chunks minimum per bitmap.
 * "10" is our current best guess, yielding 2 million bits for 2k.
 * Setting it to "12" yields 33.5 million bits for 8k.  For now, we
 * hardcode this at 10.
 */

#ifndef PARROTDB_PABITMAP_H
#define PARROTDB_PABITMAP_H

#include <libpsu/psualloc.h>

typedef uint32_t pa_bitnumber_t;  /* The bit number to test/set */
typedef uint32_t pa_bitunit_t;	  /* Bit testing unit (word) */

/* Declare our wrapper type */
PA_ATOM_TYPE(pa_bitmap_atom_t, pa_bitmap_atom_s, pba_atom,
	     pa_bitmap_is_null, pa_bitmap_atom, pa_bitmap_atom_of,
	     pa_bitmap_null_atom);

/*
 * An "id" is just an atom, but it's better for users to think of it
 * as an identifier.  And we use our wrapper so we've got type
 * protection.
 */
typedef pa_bitmap_atom_t pa_bitmap_id_t; /* An individual bitmap */

/* We are essentially a wrapper around a pa_fixed array */
typedef struct pa_bitmap_s {
    pa_fixed_t *pb_data;	/* Our real underlaying data */
} pa_bitmap_t;

/*
 * Blocks are the base size we allocate, for both bit contents and
 * chunk pages.  They must be the same size.
 */
#define PA_BITMAP_BLOCK_SHIFT	10
#define PA_BITMAP_BLOCK_SIZE	(1U << PA_BITMAP_BLOCK_SHIFT)

#define PA_BITMAP_CHUNK_SHIFT	(PA_BITMAP_BLOCK_SHIFT - PA_ATOM_SHIFT)
#define PA_BITMAP_CHUNK_SIZE	(1U << PA_BITMAP_CHUNK_SHIFT)

#define PA_BITMAP_BITS_PER_CHUNK (PA_BITMAP_BLOCK_SIZE * PA_NBBY)
#define PA_BITMAP_BITS_PER_UNIT	(sizeof(pa_bitunit_t) * PA_NBBY)
#define PA_BITMAP_UNITS_PER_CHUNK (PA_BITMAP_BLOCK_SIZE / sizeof(pa_bitunit_t))

#define PA_BITMAP_MAX_BIT \
    (PA_NBBY * PA_BITMAP_BLOCK_SIZE * PA_BITMAP_CHUNK_SIZE)

#define PA_BITMAP_MAX_ATOMS (1<<24) /* Mostly random number */

#define PA_BITMAP_FIND_START	((1U << 31) - 1) /* Start/end marker */
#define PA_BITMAP_FIND_DONE	(1U << 31) /* Start/end marker */

static inline uint32_t
pa_bitmap_bitnum (pa_bitmap_t *pfp UNUSED, pa_bitnumber_t num)
{
    return (num & (PA_BITMAP_BITS_PER_UNIT - 1));
}

static inline uint32_t
pa_bitmap_unitnum (pa_bitmap_t *pfp UNUSED, pa_bitnumber_t num)
{
    num /= PA_BITMAP_BITS_PER_UNIT;
    num &= PA_BITMAP_UNITS_PER_CHUNK - 1;
    return num;
}

static inline uint32_t
pa_bitmap_chunknum (pa_bitmap_t *pfp UNUSED, pa_bitnumber_t num)
{
    num /= PA_NBBY;		   /* Think in bytes */
    num >>= PA_BITMAP_BLOCK_SHIFT; /* Drop bit number */
    num &= PA_BITMAP_CHUNK_SIZE - 1; /* Should be a noop */
    return num;
}

static inline pa_bitmap_id_t
pa_bitmap_alloc (pa_bitmap_t *pfp)
{
    pa_fixed_atom_t atom = pa_fixed_alloc_atom(pfp->pb_data);
    return pa_bitmap_atom(pa_fixed_atom_of(atom));
}

static inline pa_fixed_atom_t
pa_bitmap_to_fixed (pa_bitmap_atom_t atom)
{
    return pa_fixed_atom(pa_bitmap_atom_of(atom));
}

static inline pa_bitmap_atom_t
pa_bitmap_from_fixed (pa_fixed_atom_t atom)
{
    return pa_bitmap_atom(pa_fixed_atom_of(atom));
}

static inline pa_fixed_atom_t *
pa_bitmap_chunk_addr (pa_bitmap_t *pfp, pa_bitmap_atom_t atom)
{
    return pa_fixed_atom_addr(pfp->pb_data, pa_bitmap_to_fixed(atom));
}

static inline uint8_t
pa_bitmap_test (pa_bitmap_t *pfp, pa_bitmap_id_t bitmap_id, pa_bitnumber_t num)
{
    if (num >= PA_BITMAP_MAX_BIT)
	return FALSE;

    uint32_t bitnum = pa_bitmap_bitnum(pfp, num);
    uint32_t unitnum = pa_bitmap_unitnum(pfp, num);
    uint32_t chunknum = pa_bitmap_chunknum(pfp, num);

    pa_fixed_atom_t *chunkp = pa_bitmap_chunk_addr(pfp, bitmap_id);
    if (chunkp == NULL)
	return FALSE;		/* Internal error */

    pa_fixed_atom_t atom = chunkp[chunknum];
    if (pa_fixed_is_null(atom))
	return FALSE;		/* Not allocated == never set */

    pa_bitunit_t *data = pa_fixed_atom_addr(pfp->pb_data, atom);
    if (data == NULL)
	return FALSE;		/* Should not occur */

    return (data[unitnum] & (1 << bitnum)) ? TRUE : FALSE;
}

static inline void
pa_bitmap_set (pa_bitmap_t *pfp, pa_bitmap_id_t bitmap_id, pa_bitnumber_t num)
{
    if (num >= PA_BITMAP_MAX_BIT)
	return;

    uint32_t bitnum = pa_bitmap_bitnum(pfp, num);
    uint32_t unitnum = pa_bitmap_unitnum(pfp, num);
    uint32_t chunknum = pa_bitmap_chunknum(pfp, num);

    pa_fixed_atom_t *chunkp = pa_bitmap_chunk_addr(pfp, bitmap_id);
    if (chunkp == NULL)
	return;			/* Internal error */

    pa_fixed_atom_t atom = chunkp[chunknum];
    if (pa_fixed_is_null(atom)) {
	/* Need to allocate the chunk */
	atom = pa_fixed_alloc_atom(pfp->pb_data);
	if (pa_fixed_is_null(atom))
	    return;
	chunkp[chunknum] = atom; /* Save into the chunk table */
    }

    pa_bitunit_t *data = pa_fixed_atom_addr(pfp->pb_data, atom);
    if (data == NULL)
	return;		/* Should not occur */

    data[unitnum] |= 1 << bitnum;
}

static inline void
pa_bitmap_clear (pa_bitmap_t *pfp, pa_bitmap_id_t bitmap_id, pa_bitnumber_t num)
{
    if (num >= PA_BITMAP_MAX_BIT)
	return;

    uint32_t bitnum = pa_bitmap_bitnum(pfp, num);
    uint32_t unitnum = pa_bitmap_unitnum(pfp, num);
    uint32_t chunknum = pa_bitmap_chunknum(pfp, num);

    pa_fixed_atom_t *chunkp = pa_bitmap_chunk_addr(pfp, bitmap_id);
    if (chunkp == NULL)
	return;			/* Internal error */

    pa_fixed_atom_t atom = chunkp[chunknum];
    if (pa_fixed_is_null(atom))
	return;			/* Not allocated yet */

    pa_bitunit_t *data = pa_fixed_atom_addr(pfp->pb_data, atom);
    if (data == NULL)
	return;		/* Should not occur */

    data[unitnum] &= ~(1 << bitnum);
}

static inline pa_bitnumber_t
pa_bitmap_find_next (pa_bitmap_t *pfp, pa_bitmap_id_t bitmap_id,
		     pa_bitnumber_t num)
{
    uint32_t bitnum, unitnum, chunknum, bitmask;
    pa_fixed_atom_t *chunkp, atom;
    pa_bitunit_t *data, value;

    if (num == PA_BITMAP_FIND_START) {
	num = 0;
	bitmask = 0;
    } else {
	num += 1;			/* Start looking at the next bit */
	if (num == PA_BITMAP_MAX_BIT)
	    return PA_BITMAP_FIND_DONE;

	/* We need to discard all the lower bits*/
	bitnum = pa_bitmap_bitnum(pfp, num);
	bitmask = (1 << bitnum) - 1;

    }

    /* Fetch the bitmap's chunk table */
    chunkp = pa_bitmap_chunk_addr(pfp, bitmap_id);
    if (chunkp == NULL)
	return PA_BITMAP_FIND_DONE; /* Should not occur */

 restart:
    chunknum = pa_bitmap_chunknum(pfp, num);

    atom = chunkp[chunknum];
    if (pa_fixed_is_null(atom)) {
	/*
	 * Since this chunk hasn't been allocated, we know that
	 * no bits in this range have been set, so we can skip to
	 * the next chunk.
	 */
	while (++chunknum < PA_BITMAP_CHUNK_SIZE) {
	    if (!pa_fixed_is_null(chunkp[chunknum])) {
		num = chunknum * PA_BITMAP_BITS_PER_CHUNK;
		goto restart;
	    }
	}

	return PA_BITMAP_FIND_DONE; /* End of bits */
    }

    data = pa_fixed_atom_addr(pfp->pb_data, atom);
    if (data == NULL)
	return PA_BITMAP_FIND_DONE; /* Should not occur */

    bitnum = pa_bitmap_bitnum(pfp, num);
    unitnum = pa_bitmap_unitnum(pfp, num);
    value = data[unitnum];

    /* Turn off all bits below the last one that we returned */
    value &= ~bitmask;
    bitmask = 0;		/* Don't repeat it next time thru */

    if (value == 0) {	/* All zeros */
	while (++unitnum < PA_BITMAP_UNITS_PER_CHUNK) {
	    if (data[unitnum] != 0) {
		num = (chunknum * PA_BITMAP_BITS_PER_CHUNK)
		    + (unitnum * PA_BITMAP_BITS_PER_UNIT);
		goto restart;
	    }
	}

	/* Move to next chunk */
	num = (chunknum + 1) * PA_BITMAP_BITS_PER_CHUNK;
	goto restart;
    }

    /* Current unit has something set */
    int found = ffs(value);
    if (found <= 0)
	return PA_BITMAP_FIND_DONE; /* Should not occur */

    found -= 1;			/* ffs() numbers bits from 1, not 0 */

    return (chunknum * PA_BITMAP_BITS_PER_CHUNK)
	+ (unitnum * PA_BITMAP_BITS_PER_UNIT) + found;
}

static inline pa_bitmap_t *
pa_bitmap_open (pa_mmap_t *pmp, const char *name)
{
    pa_bitmap_t *pbp =  psu_calloc(sizeof(*pbp));
    if (pbp) {
	pbp->pb_data = pa_fixed_open(pmp, name, PA_BITMAP_BLOCK_SHIFT,
			     PA_BITMAP_BLOCK_SIZE, PA_BITMAP_MAX_ATOMS);

	/* bitmaps require init-to-zero behavior */
	if (pbp != NULL)
	    pa_fixed_set_flags(pbp->pb_data, PFF_INIT_ZERO);
    }

    return pbp;
}

static inline void
pa_bitmap_close (pa_bitmap_t *pbp)
{
    pa_fixed_close(pbp->pb_data);
    psu_free(pbp);
}

#endif /* PARROTDB_PABITMAP_H */
