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

#include "slaxconfig.h"
#include <libslax/slax.h>
#include <libslax/paged_array.h>

static void *
pa_default_alloc (void *opaque UNUSED, size_t size)
{
    return malloc(size);
}

/* We don't use free (yet), but .... */
static void
pa_default_free (void *opaque UNUSED, void *ptr)
{
    free(ptr);
}

static void *
pa_default_page_get (void *opaque UNUSED, void *base, pa_page_t slot)
{
    pa_atom_t **real_base = base;

    return real_base[slot];
}

static void
pa_default_page_set (void *opaque UNUSED, void *base, pa_page_t slot,
		     void *addr)
{
    pa_atom_t **real_base = base;

    real_base[slot] = addr;
}

static void
pa_default_base_set (void *opaque UNUSED, void *base UNUSED)
{
    return;			/* Nothing extra we need to do */
}

/*
 * Allocate the page to which the given atom belongs; mark them all free
 */
void
pa_alloc_page (paged_array_t *pap, pa_atom_t atom)
{
    unsigned slot = atom >> pap->pa_shift;

    pa_atom_t count = 1 << pap->pa_shift;
    pa_atom_t *addr = pap->pa_alloc_fn(pap->pa_mem_opaque,
				     count * pap->pa_atom_size);
    if (addr == NULL)
	return;

    /* Fill in the 'free' value for each atom, pointing to the next */
    unsigned i;
    unsigned mult = pap->pa_atom_size / sizeof(addr[0]);
    pa_atom_t first = slot << pap->pa_shift;
    for (i = 0; i < count - 1; i++) {
	addr[i * mult] = first + i + 1;
    }

    pa_page_set(pap, slot, addr);

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
    unsigned max_page = pap->pa_max_atoms >> pap->pa_shift;
    for (i = slot + 1; i < max_page; i++)
	if (pa_page_get(pap, i) == NULL)
	    break;

    i = (i < max_page) ? i << pap->pa_shift : PA_NULL_ATOM;
    addr[(count - 1) * mult] = i;
}

/*
 * The most brutal of the initializers: the caller has an existing
 * base and info block for our use.  We just take them.
 */
void
pa_init_from_block (paged_array_t *pap, void *base,
		    paged_array_info_t *infop,
		    pa_base_set_func_t base_set_fn,
		    pa_page_get_func_t page_get_fn,
		    pa_page_set_func_t page_set_fn, 
		    pa_alloc_func_t alloc_fn, pa_free_func_t free_fn,
		    void *mem_opaque)
{
    pap->pa_base = base;
    pap->pa_infop = infop;

    /* Set all our functions */
    pap->pa_base_set_fn = base_set_fn;
    pap->pa_page_get_fn = page_get_fn;
    pap->pa_page_set_fn = page_set_fn;
    pap->pa_alloc_fn = alloc_fn;
    pap->pa_free_fn = free_fn;
    pap->pa_mem_opaque = mem_opaque;
}

void
pa_init (paged_array_t *pap, void *base, uint8_t shift,
	 uint16_t atom_size, uint32_t max_atoms,
	 pa_base_set_func_t base_set_fn,
	 pa_page_get_func_t page_get_fn, pa_page_set_func_t page_set_fn, 
	 pa_alloc_func_t alloc_fn, pa_free_func_t free_fn, void *mem_opaque)
{
    if (alloc_fn == NULL)
	alloc_fn = pa_default_alloc;

    if (free_fn == NULL)
	free_fn = pa_default_free;

    if (base_set_fn == NULL)
	base_set_fn = pa_default_base_set;

    if (page_get_fn == NULL)
	page_get_fn = pa_default_page_get;

    if (page_set_fn == NULL)
	page_set_fn = pa_default_page_set;

    /* Use our internal info block */
    if (pap->pa_infop == NULL)
	pap->pa_infop = &pap->pa_info_block;

    /* The atom must be able to hold a free node id */
    if (atom_size < sizeof(pa_atom_t))
	atom_size = sizeof(pa_atom_t);

    /* Must be even multiple of sizeof(pa_atom_t) */
    if (atom_size & (sizeof(pa_atom_t) - 1))
	atom_size = (atom_size + ((1 << sizeof(pa_atom_t)) - 1))
	    & ~((1 << sizeof(pa_atom_t)) - 1);

    /* Round max_atoms up to the next page size */
    if (max_atoms & ((1 << shift) - 1))
	max_atoms = (max_atoms + ((1 << shift) - 1)) & ~((1 << shift) - 1);

    /* No base is NULL, allocate it, zero it and init the free list */
    pap->pa_base = base;
    if (base == NULL) {
	unsigned size = (max_atoms >> shift) * sizeof(uint8_t *);
	uint8_t *real_base = alloc_fn(mem_opaque, size);
	if (real_base == NULL)
	    return;
	pap->pa_base = real_base;
	base_set_fn(pap->pa_mem_opaque, pap->pa_base);
	bzero(real_base, size);

	/* Mark number 1 as our first free atom */
	pap->pa_free = 1;
    }

    /* Fill in the rest of fhe fields from the argument list */
    pap->pa_shift = shift;
    pap->pa_atom_size = atom_size;
    pap->pa_max_atoms = max_atoms;

    /* Set all our functions */
    pap->pa_base_set_fn = base_set_fn;
    pap->pa_page_get_fn = page_get_fn;
    pap->pa_page_set_fn = page_set_fn;
    pap->pa_alloc_fn = alloc_fn;
    pap->pa_free_fn = free_fn;
    pap->pa_mem_opaque = mem_opaque;
}

paged_array_t *
pa_create (uint8_t shift, uint16_t atom_size, uint32_t max_atoms,
	   pa_base_set_func_t base_set_fn,
	   pa_page_get_func_t page_get_fn, pa_page_set_func_t page_set_fn, 
	   pa_alloc_func_t alloc_fn, pa_free_func_t free_fn, void *mem_opaque)
{
    if (alloc_fn == NULL)
	alloc_fn = pa_default_alloc;

    paged_array_t *pap = alloc_fn(mem_opaque, sizeof(*pap));

    if (pap) {
	bzero(pap, sizeof(*pap));
	pa_init(pap, NULL, shift, atom_size, max_atoms,
		base_set_fn, page_get_fn, page_set_fn,
		alloc_fn, free_fn, mem_opaque);
    }

    return pap;
}

/*
 * Cheesy breakpoint for memory allocation failure
 */
void
pa_alloc_failed (paged_array_t *pap UNUSED)
{
    return;			/* Just a place to breakpoint */
}

#ifdef UNIT_TEST

#define MAX_VAL	100

typedef struct test_s {
    unsigned t_magic;
    pa_atom_t t_id;
    int t_val[MAX_VAL];
} test_t;

int
main (int argc UNUSED, char **argv UNUSED)
{
    unsigned i;
    test_t *tp;
    pa_atom_t atom;
    unsigned max_atoms = 1 << 14, shift = 6, count = 100, magic = 0x5e5e5e5e;

    for (argc = 1; argv[argc]; argc++) {
	if (strcmp(argv[argc], "shift") == 0) {
	    if (argv[argc + 1])
		shift = atoi(argv[++argc]);
	} else if (strcmp(argv[argc], "max") == 0) {
	    if (argv[argc + 1]) 
		max_atoms = atoi(argv[++argc]);
	} else if (strcmp(argv[argc], "count") == 0) {
	    if (argv[argc + 1]) 
		count = atoi(argv[++argc]);
	}
    }

    test_t **trec = calloc(count, sizeof(*tp));
    if (trec == NULL)
	return -1;

    paged_array_t *pap = pa_create_simple(shift, sizeof(test_t), max_atoms);
    assert(pap);

    for (i = 0; i < count; i++) {
	atom = pa_alloc_atom(pap);
	tp = pa_atom_addr(pap, atom);
	trec[i] = tp;
	if (tp) {
	    tp->t_magic = magic;
	    tp->t_id = atom;
	    memset(tp->t_val, -1, sizeof(tp->t_val));
	}

	printf("in %u : %u -> %p (%u)\n", i, atom, tp, pap->pa_free);
    }

    for (i = 0; i < count; i++) {
	if (trec[i] == NULL)
	    continue;
	if (i & 2)
	    continue;


	atom = trec[i]->t_id;
	printf("free %u : %u -> %p (%u)\n", i, atom, trec[i], pap->pa_free);

	pa_free_atom(pap, atom);

	trec[i] = NULL;
    }

    for (i = 0; i < count; i++) {
	if (i & 4)
	    continue;

	if (trec[i] == NULL) {
	    atom = pa_alloc_atom(pap);
	    tp = pa_atom_addr(pap, atom);
	    trec[i] = tp;
	    if (tp) {
		tp->t_magic = magic;
		tp->t_id = atom;
		memset(tp->t_val, -1, sizeof(tp->t_val));
	    }
	    printf("in2 %u : %u -> %p (%u)\n", i, atom, tp, pap->pa_free);

	} else {
	    atom = trec[i]->t_id;
	    printf("free2 %u : %u -> %p (%u)\n", i, atom, trec[i], pap->pa_free);
	    pa_free_atom(pap, atom);
	    trec[i] = NULL;
	}


    }

    uint8_t *check = calloc(count, 1);
    assert(check);

    for (i = 0; i < count; i++) {
	if (trec[i] == NULL)
	    continue;

	atom = trec[i]->t_id;
	tp = pa_atom_addr(pap, atom);
	if (check[atom])
	    printf("EVIL: dup: %u : %u -> %p (%u)\n", i, atom, tp, pap->pa_free);
	check[atom] = 1;

	printf("out %u : %u -> %p (%u)\n", i, atom, tp, pap->pa_free);
    }

    return 0;
}

#endif /* UNIT_TEST */
