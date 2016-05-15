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
#include <libslax/pa_arb.h>

#include "pa_main.h"

void
test_init (void)
{
#if 0
    opt_clean = 1;
    opt_quiet = 1;
    opt_input = "/tmp/2";
    opt_count = 1000;
    opt_filename = "/tmp/foo.db";
#endif
}

pa_mmap_t *pmp;
pa_arb_info_t *prip;
pa_arb_t *prp;

void
test_open (void)
{
    pmp = pa_mmap_open(opt_filename, 0, 0644);
    assert(pmp);

    prip = pa_mmap_header(pmp, "arb1", sizeof(*prip));
    assert(prip);

    prp = pa_arb_setup(pmp, prip);
    assert(prp);
}

void
test_alloc (unsigned slot, unsigned this_size)
{
    pa_atom_t atom = pa_arb_alloc(prp, this_size);
    test_t *tp = pa_arb_atom_addr(prp, atom);

    trec[slot] = tp;
    if (tp) {
	memset(tp, opt_value, this_size);
	if (this_size > sizeof(*tp)) {
	    tp->t_magic = opt_magic;
	    tp->t_id = atom;
	    tp->t_slot = slot;
	    memset(tp->t_val, opt_value, opt_size - sizeof(*tp));
	}
    }

    if (!opt_quiet)
	printf("in %u (%u) : %#x -> %p\n", slot, this_size, atom, tp);
}

void
test_dump (void)
{
    unsigned slot;
    test_t *tp;
    pa_atom_t atom;

    printf("dumping: (%u) len:%lu\n", opt_count, prp->pr_mmap->pm_len);
    for (slot = 0; slot < opt_count; slot++) {
	tp = trec[slot];
	if (tp) {
	    atom = trec[slot]->t_id;
	    printf("%u : %#x -> %p%s%s%s\n",
		   slot, atom, trec[slot],
		   (tp->t_magic != opt_magic) ? " bad-magic" : "",
		   (tp->t_slot != slot) ? " bad-slot" : "",
		   (tp->t_val[2] != opt_value) ? " bad-value" : "");
	}
    }

    pa_arb_dump(prp);
}

void
test_free (unsigned slot)
{
    pa_atom_t atom;
    test_t *tp = trec[slot];
    if (tp) {
	atom = tp->t_id;
	if (!opt_quiet)
	    printf("free %u : %#x -> %p\n",
		   slot, atom, tp);
	pa_arb_free(prp, tp);
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
	    printf("%u : %#x -> %p\n",
		   slot, atom, trec[slot]);
    } else {
	printf("%u : free\n", slot);
    }
}

void
test_close (void)
{
    pa_arb_close(prp);
}
