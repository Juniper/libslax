/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer <phil@>, May 2016
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
#include <libslax/pa_pat.h>

#define NEED_T_PAT
#include "pa_main.h"

pa_pat_root_t pat_root;

PATNODE_TO_STRUCT(test_pointer, test_t, t_pat);

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

void
test_open (void)
{
    pa_pat_root_init(&pat_root, PA_PAT_MAXKEY,
		     STRUCT_OFFSET(test_t, t_pat, t_val));
}

void
test_alloc (unsigned slot UNUSED, unsigned this_size UNUSED)
{
    return;
}

void
test_key (unsigned slot, const char *key)
{
    static unsigned id;
    size_t len = key ? strlen(key) : 0;

    if (len == 0)
	return;

    test_t *tp = calloc(1, sizeof(*tp) + len + 1);

    trec[slot] = tp;
    if (tp) {
	tp->t_magic = opt_magic;
	tp->t_id = id++;
	tp->t_slot = slot;
	memcpy(tp->t_val, key, len + 1);
	pa_pat_node_init_length(&tp->t_pat, len);

	if (!pa_pat_add(&pat_root, &tp->t_pat))
	    pa_warning(0, "duplicate key: %s", key);
    }

    if (!opt_quiet)
	printf("in %u (%lu) : %s -> %p\n", slot, len, key, tp);
}

void
test_dump (void)
{
    test_t *tp;
    pa_pat_node_t *node = NULL;

    while ((node = pa_pat_find_next(&pat_root, node)) != NULL) {
	tp = test_pointer(node);
	if (tp) {
	    printf("%u : %#x -> %p (%#x) '%s'\n",
		   tp->t_slot, tp->t_id, tp, tp->t_magic, (char *) tp->t_val);
	}
    }
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

	if (!pa_pat_delete(&pat_root, &tp->t_pat))
	    pa_warning(0, "delete failed for key: %u", slot);
	else free(tp);

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
    return;
}
