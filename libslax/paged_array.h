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
 * The paged array works like a page table; part of the identifier
 * selects a page of values, while the remainder selects the item off
 * that page.  Note that it's an item array, not a byte array.  The
 * caller sets the bit shift and the number and size of the items,
 * which are recorded in the base paged_array_t structure.  Since the
 * caller knows these values, they can either let us get them from the
 * struct or pass them directly to the inline functions
 * (e.g. pa_item_addr_direct) for better performance.
 *
 * These functions are designed to point into memory mapped addresses,
 * so they allow the caller to give us not only allocation and free
 * functions, but an opaque 'void *' parameter that we pass on to
 * these functions.  We also allow the call to either use the info
 * block that is built into the structure (pa_info_block) or to set
 * their own (see pa_init_from_block).
 */

#define PA_ASSERT(_a, _b) assert(_b)

typedef void *(*pa_alloc_func_t)(void *, size_t);
typedef void (*pa_free_func_t)(void *, void *);

typedef uint32_t pa_item_t;	/* Type for item numbers */
#define PA_NULL_ITEM (pa_item_t) 0

typedef struct paged_array_info_s {
    uint8_t pai_shift;		/* Bits to shift to select the page */
    uint16_t pai_item_size;	/* Size of each item */
    pa_item_t pai_max_items;	/* Max number of items */
    pa_item_t pai_free;		/* First item that is free */
} paged_array_info_t;

typedef struct paged_array_s {
    paged_array_info_t pa_info_block;
    paged_array_info_t *pa_infop;
    uint8_t **pa_base;		/* Address of page table */
    pa_alloc_func_t pa_alloc_fn; /* Allocate memory */
    pa_free_func_t pa_free_fn;	 /* Free memory */
    void *pa_mem_opaque;	 /* Opaque data passed to pa_{alloc,free}_fn */
} paged_array_t;

/* Simplification macros */
#define pa_shift	pa_infop->pai_shift
#define pa_item_size	pa_infop->pai_item_size
#define pa_max_items	pa_infop->pai_max_items
#define pa_free		pa_infop->pai_free

/*
 * Return the address of an item in a paged table array
 */
static inline void *
pa_item_addr_direct (uint8_t **base, uint8_t shift, uint16_t item_size,
	      uint32_t max_items, pa_item_t item)
{
    uint8_t *page;
    if (item == 0 || item >= max_items)
	return NULL;

    page = base[item >> shift];
    if (page == NULL)
	return NULL;

    return &page[(item & ((1 << shift) - 1)) * item_size];
}

/*
 * Return the address of an item in a paged table array
 */
static inline void *
pa_item_addr (paged_array_t *pap, uint32_t item)
{
    if (pap->pa_base == NULL)
	return NULL;

    return pa_item_addr_direct(pap->pa_base, pap->pa_shift, pap->pa_item_size,
			       pap->pa_max_items, item);
}

void
pa_alloc_page (paged_array_t *pap, pa_item_t item);

/*
 * Allocate a new item, returning the item number
 */
static inline pa_item_t
pa_alloc_item (paged_array_t *pap)
{
    /* free == PA_NULL_ITEM -> nothing available */
    if (pap->pa_free == PA_NULL_ITEM || pap->pa_base == NULL)
	return PA_NULL_ITEM;

    /* Take the next item off the free list and return it */
    pa_item_t item = pap->pa_free;
    void *addr = pa_item_addr(pap, item);
    if (addr == NULL) {
	pa_alloc_page(pap, item);
	addr = pa_item_addr(pap, item);
    }

    /*
     * Fetch the next free item from the start of this item.  If we
     * failed, we don't want to change the free item, since it might
     * be a transient memory issue.
     */
    if (addr)
	pap->pa_free = *(pa_item_t *) addr;

    return item;
}

static inline void
pa_free_item (paged_array_t *pap, pa_item_t item)
{
    if (item == PA_NULL_ITEM)
	return;

    pa_item_t *addr = pa_item_addr(pap, item);
    if (addr == NULL)
	return;

    /* Add the item to the front of the free list */
    *addr = pap->pa_free;
    pap->pa_free = item;
}

void
pa_init_from_block (paged_array_t *pap, uint8_t **base,
		    paged_array_info_t *infop,
		    pa_alloc_func_t alloc_fn, pa_free_func_t free_fn,
		    void *mem_opaque);

void
pa_init (paged_array_t *pap, uint8_t **base, uint8_t shift,
	 uint16_t item_size, uint32_t max_items,
	 pa_alloc_func_t alloc_fn, pa_free_func_t free_fn, void *mem_opaque);

paged_array_t *
pa_create (uint8_t shift, uint16_t item_size, uint32_t max_items,
	   pa_alloc_func_t alloc_fn, pa_free_func_t free_fn, void *mem_opaque);

#endif /* LIBSLAX_PAGED_ARRAY_H */
