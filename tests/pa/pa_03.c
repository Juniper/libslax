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

static char *
scan_uint32 (char *cp, uint32_t *valp)
{
    unsigned long val;

    while (isspace((int) *cp))
	cp += 1;

    val = strtoul(cp, &cp, 0);
    *valp = val;
    return (val == ULONG_MAX) ? NULL : cp;
}

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
    const char *input = NULL;
    int rm = 0, quiet = 0;

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
	} else if (strcmp(argv[argc], "quiet") == 0) {
	    quiet = 1;
	} else if (strcmp(argv[argc], "input") == 0) {
	    if (argv[argc + 1]) 
		input = argv[++argc];
	}
    }

    rm = 1;
    quiet = 1;
    input = "/tmp/1";
    filename = "/tmp/foo.db";

    if (rm && filename)
	unlink(filename);

    test_t **trec = calloc(count, sizeof(*tp));
    if (trec == NULL)
	return -1;

    pa_mmap_t *pmp = pa_mmap_open(filename, 0, 0644);
    assert(pmp);

    pa_fixed_info_t *pfip = pa_mmap_header(pmp, "fix1", sizeof(*pfip));
    assert(pfip);

    pa_fixed_t *pfp = pa_fixed_setup(pmp, pfip, shift,
				     sizeof(test_t), max_atoms);
    assert(pfp);

    FILE *infile = input ? fopen(input, "r") : stdin;
    assert(infile);

    char buf[128];
    char *cp;
    uint32_t slot;

    for (;;) {
	cp = fgets(buf, sizeof(buf), infile);
	if (cp == NULL)
	    break;

	switch (*cp++) {
	case '#':
	    continue;

	case 'a':
	    cp = scan_uint32(cp, &slot);
	    if (cp == NULL)
		break;

	    atom = pa_fixed_alloc_atom(pfp);
	    tp = pa_fixed_atom_addr(pfp, atom);
	    trec[slot] = tp;
	    if (tp) {
		tp->t_magic = magic;
		tp->t_id = atom;
		memset(tp->t_val, -1, sizeof(tp->t_val));
	    }

	    if (!quiet)
		printf("in %u : %u -> %p (%u)\n", slot, atom, tp, pfp->pf_free);
	    break;

	case 'd':
	    if (quiet)
		break;

	    printf("dumping: (%u)\n", count);
	    for (slot = 0; slot < count; slot++) {
		tp = trec[slot];
		if (tp) {
		    atom = trec[slot]->t_id;
		    printf("%u : %u -> %p (%u)\n",
			   slot, atom, trec[slot], pfp->pf_free);
		}
	    }
	    break;

	case 'f':
	    cp = scan_uint32(cp, &slot);
	    if (cp == NULL)
		break;

	    tp = trec[slot];
	    if (tp) {
		atom = trec[slot]->t_id;
		if (!quiet)
		    printf("free %u : %u -> %p (%u)\n",
			   slot, atom, trec[slot], pfp->pf_free);
		pa_fixed_free_atom(pfp, atom);
		trec[slot] = NULL;
	    } else {
		printf("%u : free\n", slot);
	    }
	    break;

	case 'p':
	    cp = scan_uint32(cp, &slot);
	    if (cp == NULL)
		break;

	    tp = trec[slot];
	    if (tp) {
		atom = trec[slot]->t_id;
		if (!quiet)
		    printf("%u : %u -> %p (%u)\n",
			   slot, atom, trec[slot], pfp->pf_free);
	    } else {
		printf("%u : free\n", slot);
	    }
	    break;
	    
	}
    }

    pa_fixed_close(pfp);

    return 0;
}
