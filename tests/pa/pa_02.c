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

#define MAX_VAL	100

typedef struct test_s {
    unsigned t_magic;
    pa_offset_t t_id;
    size_t t_size;
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
    unsigned len = 1 << 20;
    unsigned size = 1 << 10;
    unsigned rm = 0;

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
	} else if (strcmp(argv[argc], "len") == 0) {
	    if (argv[argc + 1]) 
		len = atoi(argv[++argc]);
	} else if (strcmp(argv[argc], "size") == 0) {
	    if (argv[argc + 1]) 
		size = atoi(argv[++argc]);
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
    assert(pmp);

    for (i = 0; i < count; i++) {
	size_t msize = size;
	atom = pa_mmap_alloc(pmp, msize);
	tp = pa_mmap_addr(pmp, atom);
	trec[i] = tp;
	if (tp) {
	    tp->t_magic = magic;
	    tp->t_id = atom;
	    tp->t_size = msize;
	    memset(tp->t_val, -1, sizeof(tp->t_val));
	}

	printf("in %u : %u -> %p\n", i, atom, tp);
    }

    for (i = 0; i < count; i++) {
	if (trec[i] == NULL)
	    continue;
	if (i & 2)
	    continue;

	atom = trec[i]->t_id;
	printf("free %u : %u -> %p\n", i, atom, trec[i]);

	pa_mmap_free(pmp, atom, trec[i]->t_size);

	trec[i] = NULL;
    }

    for (i = 0; i < count; i++) {
	if (i & 4)
	    continue;

	if (trec[i] == NULL) {
	    size_t msize = size;
	    atom = pa_mmap_alloc(pmp, msize);
	    tp = pa_mmap_addr(pmp, atom);
	    trec[i] = tp;
	    if (tp) {
		tp->t_magic = magic;
		tp->t_id = atom;
		memset(tp->t_val, -1, sizeof(tp->t_val));
	    }
	    printf("in2 %u : %u -> %p\n", i, atom, tp);

	} else {
	    atom = trec[i]->t_id;
	    printf("free2 %u : %u -> %p\n", i, atom, trec[i]);
	    pa_mmap_free(pmp, atom, trec[i]->t_size);
	    trec[i] = NULL;
	}


    }

    return 0;
}
