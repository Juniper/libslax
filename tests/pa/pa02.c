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
#include <stddef.h>
#include <ctype.h>
#include <limits.h>

#include <parrotdb/pacommon.h>
#include <parrotdb/paconfig.h>
#include <parrotdb/pammap.h>

#define NEED_T_SIZE
#define TEST_PRINT_DULL
#include "pamain.h"

pa_mmap_t *pmp;

void
test_init (void)
{
    return;
}

void
test_open (void)
{
    pmp = pa_mmap_open(opt_filename, "pa02", 0, 0644);
    assert(pmp);
}

void
test_alloc (unsigned slot, unsigned size)
{
    pa_mmap_atom_t atom = pa_mmap_alloc(pmp, size);
    test_t *tp = pa_mmap_addr(pmp, atom);

    trec[slot] = tp;
    if (tp) {
	tp->t_magic = opt_magic;
	tp->t_id = pa_mmap_atom_of(atom);
	tp->t_size = size;
	tp->t_slot = slot;
	memset(tp->t_val, opt_value, opt_size - sizeof(*tp));
    }

    if (!opt_quiet)
	printf("in %u : %u -> %p\n", slot, pa_mmap_atom_of(atom), tp);
}

void
test_free (unsigned slot)
{
    pa_mmap_atom_t atom = pa_mmap_atom(trec[slot]->t_id);

    if (!opt_quiet)
	printf("free %u : %u -> %p\n",
	       slot, pa_mmap_atom_of(atom), trec[slot]);

    pa_mmap_free(pmp, atom, trec[slot]->t_size);

    trec[slot] = NULL;
}

void
test_close (void)
{
    pa_mmap_close(pmp);
}

