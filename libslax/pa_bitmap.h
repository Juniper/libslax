/*
 * $Id$
 *
 * Copyright (c) 2016, Juniper Networks, Inc.
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
 */

#ifndef LIBSLAX_PA_BITMAP_H
#define LIBSLAX_PA_BITMAP_H

typedef pa_fixed_t pa_bitmap_t;	/* A pool of bitmaps */
typedef uint32_t pa_bitnumber_t;  /* The bit number to test/set */
typedef pa_atom_t pa_bitmap_id_t; /* An individual bitmap */

#define PA_BITMAP_BLOCK_SHIFT	10
#define PA_BITMAP_BLOCK_SIZE	(1 << PA_BITMAP_BLOCK_SHIFT)
#define PA_BITMAP_CHUNK_SHIFT	(PA_BITMAP_BLOCK_SHIFT - PA_ATOM_SHIFT)
#define PA_BITMAP_CHUNK_SIZE	(1 << PA_BITMAP_CHUNK_SHIFT)

#define PA_BITMAP_MAX_ATOMS \
    (1 << (NBBY + PA_BITMAP_BLOCK_SHIFT + PA_BITMAP_CHUNK_SHIFT))

static inline uint32_t
pa_bitmap_bitnum (pa_bitmap_t *pfp UNUSED, pa_bitnumber_t num)
{
    return (num & (PA_BITMAP_BLOCK_SIZE - 1));
}

static inline uint32_t
pa_bitmap_chunknum (pa_bitmap_t *pfp UNUSED, pa_bitnumber_t num)
{
    num >>= PA_BITMAP_BLOCK_SHIFT; /* Drop bit number */
    num &= PA_BITMAP_CHUNK_SIZE;
    return num;
}

static inline pa_bitmap_id_t
pa_bitmap_alloc (pa_bitmap_t *pfp)
{
    return pa_fixed_alloc_atom(pfp);
}

static inline uint8_t
pa_bitmap_test (pa_bitmap_t *pfp, pa_atom_t bitmap_id, pa_bitnumber_t num)
{
    uint32_t bitnum = pa_bitmap_bitnum(pfp, num);
    uint32_t chunknum = pa_bitmap_chunknum(pfp, num);

    pa_atom_t *chunkp = pa_fixed_atom_addr(pfp, bitmap_id);
    if (chunkp == NULL)
	return FALSE;		/* Internal error */

    pa_atom_t atom = chunkp[chunknum];
    if (atom == PA_NULL_ATOM)
	return FALSE;		/* Not allocated == never set */

    uint8_t *data = pa_fixed_atom_addr(pfp, atom);
    if (data == NULL)
	return FALSE;		/* Should not occur */

    return (data[bitnum >> NBBY] & (1 << (bitnum & (1 << NBBY))))
	? TRUE : FALSE;
}

static inline void
pa_bitmap_set (pa_bitmap_t *pfp, pa_atom_t bitmap_id, pa_bitnumber_t num)
{
    uint32_t bitnum = pa_bitmap_bitnum(pfp, num);
    uint32_t chunknum = pa_bitmap_chunknum(pfp, num);

    pa_atom_t *chunkp = pa_fixed_atom_addr(pfp, bitmap_id);
    if (chunkp == NULL)
	return;			/* Internal error */

    pa_atom_t atom = chunkp[chunknum];
    if (atom == PA_NULL_ATOM) {
	/* Need to allocate the chunk */
	atom = pa_fixed_alloc_atom(pfp);
	if (atom == PA_NULL_ATOM)
	    return;
	chunkp[chunknum] = atom; /* Save into the chunk table */
    }

    uint8_t *data = pa_fixed_atom_addr(pfp, atom);
    if (data == NULL)
	return;			/* Should not occur */

    data[bitnum >> NBBY] |= 1 << (bitnum & ((1 << NBBY) - 1));
}

static inline void
pa_bitmap_clear (pa_bitmap_t *pfp, pa_atom_t bitmap_id, pa_bitnumber_t num)
{
    uint32_t bitnum = pa_bitmap_bitnum(pfp, num);
    uint32_t chunknum = pa_bitmap_chunknum(pfp, num);

    pa_atom_t *chunkp = pa_fixed_atom_addr(pfp, bitmap_id);
    if (chunkp == NULL)
	return;			/* Internal error */

    pa_atom_t atom = chunkp[chunknum];

    uint8_t *data = pa_fixed_atom_addr(pfp, atom);
    if (data == NULL)
	return;			/* Should not occur */

    data[bitnum >> NBBY] &= ~(1 << (bitnum & (1 << NBBY)));
}

static inline pa_bitmap_t *
pa_bitmap_open (pa_mmap_t *pmp, const char *name)
{
    return pa_fixed_open(pmp, name, PA_BITMAP_BLOCK_SHIFT,
			 PA_BITMAP_BLOCK_SIZE, PA_BITMAP_MAX_ATOMS);
}

#endif /* LIBSLAX_PA_BITMAP_H */
