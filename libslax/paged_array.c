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

/*
 * Allocate the page to which the given item belongs; mark them all free
 */
void
pa_alloc_page (paged_array_t *pap, pa_item_t item)
{
    unsigned slot = item >> pap->pa_shift;
    PA_ASSERT((return), (pap->pa_base[slot] == NULL));

    pa_item_t count = 1 << pap->pa_shift;
    pa_item_t *addr = pap->pa_alloc_fn(pap->pa_mem_opaque,
				     count * pap->pa_item_size);
    if (addr == NULL)
	return;

    /* Fill in the 'free' value for each item, pointing to the next */
    unsigned i;
    unsigned mult = pap->pa_item_size / sizeof(addr[0]);
    pa_item_t first = slot << pap->pa_shift;
    for (i = 0; i < count - 1; i++) {
	addr[i * mult] = first + i + 1;
    }

    pap->pa_base[slot] = (uint8_t *) addr;

    /*
     * For the last entry, we find the next available page and use
     * the first entry for that page.  If there's no page free, we
     * use zero, which will trigger 'out of memory' behavior when
     * that item is used.
     *
     * We're taking advantage of the knowledge that we're going
     * to be allocing pages in incremental slot order, since that's
     * how we allocate them.  Don't break this behavior.
     */
    unsigned max_page = pap->pa_max_items >> pap->pa_shift;
    for (i = slot + 1; i < max_page; i++)
	if (pap->pa_base[i] == NULL)
	    break;

    i = (i < max_page) ? i << pap->pa_shift : PA_NULL_ITEM;
    addr[(count - 1) * mult] = i;
}

/*
 * The most brutal of the initializers: the caller has an existing
 * base and info block for our use.  We just take them.
 */
void
pa_init_from_block (paged_array_t *pap, uint8_t **base,
		    paged_array_info_t *infop,
		    pa_alloc_func_t alloc_fn, pa_free_func_t free_fn,
		    void *mem_opaque)
{
    pap->pa_base = base;
    pap->pa_infop = infop;

    pap->pa_alloc_fn = alloc_fn;
    pap->pa_free_fn = free_fn;
    pap->pa_mem_opaque = mem_opaque;
}

void
pa_init (paged_array_t *pap, uint8_t **base, uint8_t shift,
	 uint16_t item_size, uint32_t max_items,
	 pa_alloc_func_t alloc_fn, pa_free_func_t free_fn, void *mem_opaque)
{
    if (alloc_fn == NULL)
	alloc_fn = pa_default_alloc;

    if (free_fn == NULL)
	free_fn = pa_default_free;

    /* Use our internal info block */
    if (pap->pa_infop == NULL)
	pap->pa_infop = &pap->pa_info_block;

    /* The item must be able to hold a free node id */
    if (item_size < sizeof(pa_item_t))
	item_size = sizeof(pa_item_t);

    /* Must be even multiple of sizeof(pa_item_t) */
    if (item_size & (sizeof(pa_item_t) - 1))
	item_size = (item_size + ((1 << sizeof(pa_item_t)) - 1))
	    & ~((1 << sizeof(pa_item_t)) - 1);

    /* Round max_items up to the next page size */
    if (max_items & ((1 << shift) - 1))
	max_items = (max_items + ((1 << shift) - 1)) & ~((1 << shift) - 1);

    /* No base is NULL, allocate it, zero it and init the free list */
    pap->pa_base = base;
    if (base == NULL) {
	unsigned size = (max_items >> shift) * sizeof(uint8_t *);
	pap->pa_base = alloc_fn(mem_opaque, size);
	if (pap->pa_base == NULL)
	    return;
	bzero(pap->pa_base, size);

	/* Mark number 1 as our first free item */
	pap->pa_free = 1;
    }

    /* Fill in the rest of fhe fields from the argument list */
    pap->pa_shift = shift;
    pap->pa_item_size = item_size;
    pap->pa_max_items = max_items;
    pap->pa_alloc_fn = alloc_fn;
    pap->pa_free_fn = free_fn;
    pap->pa_mem_opaque = mem_opaque;
}

paged_array_t *
pa_create (uint8_t shift, uint16_t item_size, uint32_t max_items,
	   pa_alloc_func_t alloc_fn, pa_free_func_t free_fn, void *mem_opaque)
{
    if (alloc_fn == NULL)
	alloc_fn = pa_default_alloc;
    paged_array_t *pap = alloc_fn(mem_opaque, sizeof(*pap));

    if (pap) {
	bzero(pap, sizeof(*pap));
	pa_init(pap, NULL, shift, item_size, max_items,
		alloc_fn, free_fn, mem_opaque);
    }

    return pap;
}

#ifdef UNIT_TEST

#define MAX_VAL	100

typedef struct test_s {
    unsigned t_magic;
    pa_item_t t_id;
    int t_val[MAX_VAL];
} test_t;

int
main (int argc UNUSED, char **argv UNUSED)
{
    unsigned i;
    test_t *tp;
    pa_item_t item;
    unsigned max_items = 1 << 14, shift = 6, count = 100, magic = 0x5e5e5e5e;

    for (argc = 1; argv[argc]; argc++) {
	if (strcmp(argv[argc], "shift") == 0) {
	    if (argv[argc + 1])
		shift = atoi(argv[++argc]);
	} else if (strcmp(argv[argc], "max") == 0) {
	    if (argv[argc + 1]) 
		max_items = atoi(argv[++argc]);
	} else if (strcmp(argv[argc], "count") == 0) {
	    if (argv[argc + 1]) 
		count = atoi(argv[++argc]);
	}
    }

    test_t **trec = calloc(count, sizeof(*tp));
    if (trec == NULL)
	return -1;

    paged_array_t *pap = pa_create(shift, sizeof(test_t), max_items,
				   NULL, NULL, NULL);
    assert(pap);

    for (i = 0; i < count; i++) {
	item = pa_alloc_item(pap);
	tp = pa_item_addr(pap, item);
	trec[i] = tp;
	if (tp) {
	    tp->t_magic = magic;
	    tp->t_id = item;
	    memset(tp->t_val, -1, sizeof(tp->t_val));
	}

	printf("in %u : %u -> %p (%u)\n", i, item, tp, pap->pa_free);
    }

    for (i = 0; i < count; i++) {
	if (trec[i] == NULL)
	    continue;
	if (i & 2)
	    continue;


	item = trec[i]->t_id;
	printf("free %u : %u -> %p (%u)\n", i, item, trec[i], pap->pa_free);

	pa_free_item(pap, item);

	trec[i] = NULL;
    }

    for (i = 0; i < count; i++) {
	if (i & 4)
	    continue;

	if (trec[i] == NULL) {
	    item = pa_alloc_item(pap);
	    tp = pa_item_addr(pap, item);
	    trec[i] = tp;
	    if (tp) {
		tp->t_magic = magic;
		tp->t_id = item;
		memset(tp->t_val, -1, sizeof(tp->t_val));
	    }
	    printf("in2 %u : %u -> %p (%u)\n", i, item, tp, pap->pa_free);

	} else {
	    item = trec[i]->t_id;
	    printf("free2 %u : %u -> %p (%u)\n", i, item, trec[i], pap->pa_free);
	    pa_free_item(pap, item);
	    trec[i] = NULL;
	}


    }

    for (i = 0; i < count; i++) {
	if (trec[i] == NULL)
	    continue;

	item = trec[i]->t_id;
	tp = pa_item_addr(pap, item);

	printf("out %u : %u -> %p (%u)\n", i, item, tp, pap->pa_free);
    }

    return 0;
}

#endif /* UNIT_TEST */
