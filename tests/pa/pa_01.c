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

#include "slaxconfig.h"
#include <libslax/slax.h>
#include <libslax/pa_common.h>
#include <libslax/pa_mmap.h>
#include <libslax/pa_fixed.h>

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
    const char *filename = NULL;
    int rm = 0;

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
	} else if (strcmp(argv[argc], "file") == 0) {
	    if (argv[argc + 1]) 
		filename = argv[++argc];
	} else if (strcmp(argv[argc], "clean") == 0) {
	    rm = 1;
	}
    }

    if (rm && filename)
	unlink(filename);

    test_t **trec = calloc(count, sizeof(*tp));
    if (trec == NULL)
	return -1;

    pa_mmap_t *pmp = pa_mmap_open(filename, 0, 0644);
    assert(pmp != NULL);

    pa_fixed_t *pfp = pa_fixed_open(pmp, "pa_01",
				    shift, sizeof(test_t), max_atoms);
    assert(pfp != NULL);

    for (i = 0; i < count; i++) {
	atom = pa_fixed_alloc_atom(pfp);
	tp = pa_fixed_atom_addr(pfp, atom);
	trec[i] = tp;
	if (tp) {
	    tp->t_magic = magic;
	    tp->t_id = atom;
	    memset(tp->t_val, -1, sizeof(tp->t_val));
	}

	printf("in %u : %u -> %p (%u)\n", i, atom, tp, pfp->pf_free);
    }

    for (i = 0; i < count; i++) {
	if (trec[i] == NULL)
	    continue;
	if (i & 2)
	    continue;


	atom = trec[i]->t_id;
	printf("free %u : %u -> %p (%u)\n", i, atom, trec[i], pfp->pf_free);

	pa_fixed_free_atom(pfp, atom);

	trec[i] = NULL;
    }

    for (i = 0; i < count; i++) {
	if (i & 4)
	    continue;

	if (trec[i] == NULL) {
	    atom = pa_fixed_alloc_atom(pfp);
	    tp = pa_fixed_atom_addr(pfp, atom);
	    trec[i] = tp;
	    if (tp) {
		tp->t_magic = magic;
		tp->t_id = atom;
		memset(tp->t_val, -1, sizeof(tp->t_val));
	    }
	    printf("in2 %u : %u -> %p (%u)\n", i, atom, tp, pfp->pf_free);

	} else {
	    atom = trec[i]->t_id;
	    printf("free2 %u : %u -> %p (%u)\n", i, atom, trec[i], pfp->pf_free);
	    pa_fixed_free_atom(pfp, atom);
	    trec[i] = NULL;
	}


    }

    uint8_t *check = calloc(count, 1);
    assert(check);

    for (i = 0; i < count; i++) {
	if (trec[i] == NULL)
	    continue;

	atom = trec[i]->t_id;
	tp = pa_fixed_atom_addr(pfp, atom);
	if (check[atom])
	    printf("EVIL: dup: %u : %u -> %p (%u)\n", i, atom, tp, pfp->pf_free);
	check[atom] = 1;

	printf("out %u : %u -> %p (%u)\n", i, atom, tp, pfp->pf_free);
    }

    return 0;
}
