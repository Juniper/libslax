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

#include "slaxconfig.h"
#include <libslax/slax.h>
#include <libslax/pa_common.h>
#include <libslax/pa_mmap.h>
#include <libslax/pa_fixed.h>

#include "pa_main.h"

pa_mmap_t *pmp;
pa_fixed_info_t *pfip;
pa_fixed_t *pfp;

void
test_init (void)
{
#if 0
    opt_clean = 1;
    opt_quiet = 1;
    opt_input = "/tmp/1";
    opt_filename = "/tmp/foo.db";
#endif
}

void
test_open (void)
{
    pmp = pa_mmap_open(opt_filename, 0, 0644);
    assert(pmp);

    pfip = pa_mmap_header(pmp, "fix1", sizeof(*pfip));
    assert(pfip);

    pfp = pa_fixed_setup(pmp, pfip, opt_shift, sizeof(test_t), opt_max_atoms);
    assert(pfp);
}

void
test_alloc (unsigned slot, unsigned size UNUSED)
{
    pa_atom_t atom = pa_fixed_alloc_atom(pfp);
    test_t *tp = pa_fixed_atom_addr(pfp, atom);
    trec[slot] = tp;
    if (tp) {
	tp->t_magic = opt_magic;
	tp->t_id = atom;
	tp->t_slot = slot;
	memset(tp->t_val, opt_value, opt_size - sizeof(*tp));
    }

    if (!opt_quiet)
	printf("in %u : %u -> %p (%u)\n", slot, atom, tp, pfp->pf_free);
}

void
test_free (unsigned slot)
{
    test_t *tp = trec[slot];
    if (tp) {
	pa_atom_t atom = trec[slot]->t_id;
	if (!opt_quiet)
	    printf("free %u : %u -> %p (%u)\n",
		   slot, atom, trec[slot], pfp->pf_free);
	pa_fixed_free_atom(pfp, atom);
	trec[slot] = NULL;
    } else {
	printf("%u : free\n", slot);
    }
}

void
test_print (unsigned slot)
{
    test_t *tp = trec[slot];
    if (tp) {
	pa_atom_t atom = trec[slot]->t_id;
	if (!opt_quiet)
	    printf("%u : %u -> %p (%u)\n",
		   slot, atom, trec[slot], pfp->pf_free);
    } else {
	printf("%u : free\n", slot);
    }
}

void
test_dump (void)
{
    unsigned slot;
    test_t *tp;
    pa_atom_t atom;

    printf("dumping: (%u)\n", opt_count);
    for (slot = 0; slot < opt_count; slot++) {
	tp = trec[slot];
	if (tp) {
	    atom = trec[slot]->t_id;
	    printf("%u : %u -> %p (%u)%s%s%s\n",
		   slot, atom, trec[slot], pfp->pf_free,
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
