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
#include <ctype.h>
#include <limits.h>

#include <parrotdb/pacommon.h>
#include <parrotdb/paconfig.h>
#include <parrotdb/pammap.h>
#include <parrotdb/pafixed.h>

#include "pamain.h"

pa_mmap_t *pmp;
pa_fixed_t *pfp;

void
test_init (void)
{
    if (opt_size < sizeof(test_t))
	opt_size = sizeof(test_t);
}

void
test_open (void)
{
    pmp = pa_mmap_open(opt_filename, "pa03", 0, 0);
    assert(pmp != NULL);

    pfp = pa_fixed_open(pmp, "test", opt_shift, opt_size, opt_max_atoms);
    assert(pfp != NULL);
}

void
test_alloc (unsigned slot, unsigned size UNUSED)
{
    pa_fixed_atom_t atom = pa_fixed_alloc_atom(pfp);
    test_t *tp = pa_fixed_atom_addr(pfp, atom);
    trec[slot] = tp;
    if (tp) {
	tp->t_magic = opt_magic;
	tp->t_id = pa_fixed_atom_of(atom);
	tp->t_slot = slot;
	memset(tp->t_val, opt_value, opt_size - sizeof(*tp));
    }

    if (!opt_quiet)
	printf("in %u : %u -> %p (%u)\n", slot, pa_fixed_atom_of(atom),
	       tp, pa_fixed_atom_of(pfp->pf_free));
}

void
test_free (unsigned slot)
{
    test_t *tp = trec[slot];
    if (tp) {
	pa_fixed_atom_t atom = pa_fixed_atom(trec[slot]->t_id);
	if (!opt_quiet)
	    printf("free %u : %u -> %p (%u)\n",
		   slot, pa_fixed_atom_of(atom), trec[slot],
		   pa_fixed_atom_of(pfp->pf_free));
	pa_fixed_free_atom(pfp, atom);
	trec[slot] = NULL;
    } else {
	printf("%u : free already\n", slot);
    }
}

void
test_print (unsigned slot)
{
    test_t *tp = trec[slot];
    if (tp) {
	pa_fixed_atom_t atom = pa_fixed_atom(trec[slot]->t_id);
	if (!opt_quiet)
	    printf("%u : %u -> %p (%u)\n",
		   slot, pa_fixed_atom_of(atom), trec[slot],
		   pa_fixed_atom_of(pfp->pf_free));
    } else {
	printf("%u : free already\n", slot);
    }
}

void
test_dump (void)
{
    unsigned slot;
    test_t *tp;
    pa_fixed_atom_t atom;

    printf("dumping: (%u)\n", opt_count);
    for (slot = 0; slot < opt_count; slot++) {
	tp = trec[slot];
	if (tp) {
	    atom = pa_fixed_atom(trec[slot]->t_id);
	    printf("%u : %u -> %p (%u)%s%s%s\n",
		   slot, pa_fixed_atom_of(atom), trec[slot],
		   pa_fixed_atom_of(pfp->pf_free),
		   (tp->t_magic != opt_magic) ? " bad-magic" : "",
		   (tp->t_slot != slot) ? " bad-slot" : "",
		   (tp->t_val[5] != -1) ? " bad-value" : "");
	}
    }
}
	    
void
test_close (void)
{
    pa_fixed_close(pfp);
}
